#include "codegen.h"

#include <cstdio>
#include <map>

#include "llvm/Analysis/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"

#include "ast.h"

using namespace llvm;

static Value* ErrorValue(const char* Str) {
  fprintf(stderr, "Error: %s\n", Str);
  return NULL;
}

static Function* ErrorFunction(const char* Str) {
  ErrorValue(Str);
  return NULL;
}

// Top-level IR container.
Module* TheModule;

std::map<char, int> BinopPrecedence;

// LLVM instruction generator.
static IRBuilder<> Builder(getGlobalContext());

// Symbol table.
static std::map<std::string, AllocaInst*> NamedValues;

// Optimization pipeline.
FunctionPassManager* TheFPM;

// -----------------------------------------------------------------------------
// Code generation.

static AllocaInst* CreateEntryBlockAlloca(Function* TheFunction,
                                          const std::string& VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(
      Type::getDoubleTy(getGlobalContext()), 0, VarName.c_str());
}

Value* NumberExprAST::Codegen() {
  return ConstantFP::get(getGlobalContext(), APFloat(Val));
}

Value* VariableExprAST::Codegen() {
  Value* V = NamedValues[Name];
  if (V == NULL) ErrorValue("Unknown variable name");
  // Load the value.
  return Builder.CreateLoad(V, Name.c_str());
}

Value* BinaryExprAST::Codegen() {
  // Special case '=' because we don't want to emit the LHS as an expression.
  if (Op == '=') {
    // Assignment requires the LHS to be an identifier.
    VariableExprAST* LHSE = dynamic_cast<VariableExprAST*>(LHS);
    if (!LHSE)
      return ErrorValue("destination of '=' must be a variable");

    // Codegen the RHS.
    Value* Val = RHS->Codegen();
    if (Val == NULL) return NULL;

    // Look up the name.
    Value* Variable = NamedValues[LHSE->getName()];
    if (Variable == NULL) return ErrorValue("Unknown variable name");

    Builder.CreateStore(Val, Variable);
    return Val;
  }

  Value* L = LHS->Codegen();
  Value* R = RHS->Codegen();
  if (!L || !R) return NULL;

  switch (Op) {
    case '+': return Builder.CreateFAdd(L, R, "addtmp");
    case '-': return Builder.CreateFSub(L, R, "subtmp");
    case '*': return Builder.CreateFMul(L, R, "multmp");
    case '<':
      L = Builder.CreateFCmpULT(L, R, "cmptmp");
      // We only support floating point, so convert bool 0/1 to float.
      return Builder.CreateUIToFP(
          L, Type::getDoubleTy(getGlobalContext()), "booltmp");
    default: break;
  }

  // Generate a call to a user-defined operator.
  Function* F = TheModule->getFunction(std::string("binary") + Op);
  if (!F)
    return ErrorValue("invalid binary operator");

  Value* Ops[2] = { L, R };
  return Builder.CreateCall(F, Ops, "binop");
}

Value* UnaryExprAST::Codegen() {
  Value* OperandV = Operand->Codegen();
  if (OperandV == NULL) return NULL;
  Function* F = TheModule->getFunction(std::string("unary")+Opcode);
  if (F == NULL)
    return ErrorValue("Unknown unary operator");
  return Builder.CreateCall(F, OperandV, "unop");
}

Value* IfExprAST::Codegen() {
  Value* CondV = Cond->Codegen();
  if (CondV == NULL) return NULL;

  // Convert the condition to a bool by comparing to 0.0.
  CondV = Builder.CreateFCmpONE(
      CondV, ConstantFP::get(getGlobalContext(), APFloat(0.0)), "ifcond");

  Function* TheFunction = Builder.GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block
  // at the end of the function.
  BasicBlock* ThenBB =
      BasicBlock::Create(getGlobalContext(), "then", TheFunction);
  BasicBlock* ElseBB = BasicBlock::Create(getGlobalContext(), "else");
  BasicBlock* MergeBB = BasicBlock::Create(getGlobalContext(), "ifcont");

  Builder.CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then value.
  Builder.SetInsertPoint(ThenBB);
  Value* ThenV = Then->Codegen();
  if (ThenV == NULL) return NULL;
  Builder.CreateBr(MergeBB);
  // Update ThenBB as the block can be changed by the Then codegen.
  ThenBB = Builder.GetInsertBlock();

  // Emit else block.
  // We only added ThenBB to the function before, so now add the ElseBB.
  TheFunction->getBasicBlockList().push_back(ElseBB);
  Builder.SetInsertPoint(ElseBB);
  Value* ElseV = Else->Codegen();
  if (ElseV == NULL) return NULL;
  Builder.CreateBr(MergeBB);
  // Update ElseBB as the block can be changed by the Else codegen.
  ElseBB = Builder.GetInsertBlock();

  // Emit merge block.
  // Remember to add MergeBB to the function.
  TheFunction->getBasicBlockList().push_back(MergeBB);
  Builder.SetInsertPoint(MergeBB);
  PHINode* PN =
      Builder.CreatePHI(Type::getDoubleTy(getGlobalContext()), 2, "iftmp");
  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
}

Value* ForExprAST::Codegen() {
  Function* TheFunction = Builder.GetInsertBlock()->getParent();

  // Create an alloca for the variable in the entry block.
  AllocaInst* Alloca = CreateEntryBlockAlloca(TheFunction, VarName);

  // Emit the start code first, without 'variable' in scope.
  Value* StartVal = Start->Codegen();
  if (StartVal == NULL) return NULL;

  // Store the value into the alloc.
  Builder.CreateStore(StartVal, Alloca);

  // Create the basic block for the loop header and insert it after the
  // current block.
  BasicBlock* LoopBB =
      BasicBlock::Create(getGlobalContext(), "loop", TheFunction);

  // Explicit fallthrough into LoopBB from the current block.
  Builder.CreateBr(LoopBB);

  // Start insertion into LoopBB.
  Builder.SetInsertPoint(LoopBB);

  // Remember the old value of ValName in case we shadow it.
  AllocaInst* OldVal = NamedValues[VarName];
  NamedValues[VarName] = Alloca;

  // Emit the body of the loop.  We ignore the value, but don't allow errors.
  if (Body->Codegen() == NULL) return NULL;

  // Emit the step value.
  Value* StepVal;
  if (Step) {
    StepVal = Step->Codegen();
    if (StepVal == NULL) return NULL;
  } else {
    // If not specified, use 1.0.
    StepVal = ConstantFP::get(getGlobalContext(), APFloat(1.0));
  }

  // Compute the end condition.
  Value* EndCond = End->Codegen();
  if (EndCond == NULL) return EndCond;

  // Reload, increment, and restore the alloca.  This handles the case where
  // the body of the loop mutates the variable.
  Value* CurVar = Builder.CreateLoad(Alloca, VarName.c_str());
  Value* NextVar = Builder.CreateFAdd(CurVar, StepVal, "nextvar");
  Builder.CreateStore(NextVar, Alloca);

  // Convert to a bool by comparing with 0.0.
  EndCond = Builder.CreateFCmpONE(
      EndCond, ConstantFP::get(getGlobalContext(), APFloat(0.0)), "loopcond");
  
  // Create the "after loop" block and insert it.
  BasicBlock* AfterBB =
      BasicBlock::Create(getGlobalContext(), "afterloop", TheFunction);

  // Insert the conditional branch into the end of LoopEndBB.
  Builder.CreateCondBr(EndCond, LoopBB, AfterBB);

  // New code goes in AfterBB.
  Builder.SetInsertPoint(AfterBB);

  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for loop always returns 0.0.
  return Constant::getNullValue(Type::getDoubleTy(getGlobalContext()));
}

void PrototypeAST::CreateArgumentAllocas(Function* F) {
  Function::arg_iterator AI = F->arg_begin();
  for (unsigned Idx = 0; Idx != Args.size(); ++Idx, ++AI) {
    // Create an alloca for this variable.
    AllocaInst* Alloca = CreateEntryBlockAlloca(F, Args[Idx]);
    Builder.CreateStore(AI, Alloca);
    NamedValues[Args[Idx]] = Alloca;
  }
}

Value* CallExprAST::Codegen() {
  // Functions live in the module table.
  Function* CalleeF = TheModule->getFunction(Callee);
  if (!CalleeF)
    return ErrorValue("Unknown function referenced");

  // Check argument count.
  if (CalleeF->arg_size() != Args.size())
    return ErrorValue("Incorrect number of arguments passed");

  std::vector<Value*> ArgsV;
  for (size_t i = 0, e = Args.size(); i != e; i++) {
    ArgsV.push_back(Args[i]->Codegen());
    if (!ArgsV.back()) return NULL;
  }

  return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

Function* PrototypeAST::Codegen() {
  std::vector<Type*> Doubles(
      Args.size(), Type::getDoubleTy(getGlobalContext()));
  FunctionType* FT =
      FunctionType::get(Type::getDoubleTy(getGlobalContext()), Doubles, false);
  Function* F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule);

  // If the name we assigned didn't stick, then there's already a function
  // named Name, so erase ours and get that one.
  if (F->getName() != Name) {
    F->eraseFromParent();
    F = TheModule->getFunction(Name);
    
    // We don't allow externs or redefinitions afer the function is defined.
    if (!F->empty())
      return ErrorFunction("Redefinition of function");
    
    // If the function was previously declared with a different number of
    // arguments, reject.
    if (F->arg_size() != Args.size())
      return ErrorFunction("Redefinition of function with different # args");
  }

  // Name the arguments.
  // TODO: check for conflicting argument names, e.g. extern foo(a b a).
  size_t Idx = 0;
  for (Function::arg_iterator AI = F->arg_begin();
       Idx != Args.size();
       ++AI, ++Idx) {
    AI->setName(Args[Idx]);
  }

  return F;
}

Function* FunctionAST::Codegen() {
  NamedValues.clear();

  Function* TheFunction = Proto->Codegen();
  if (!TheFunction)
    return NULL;

  // If a binop, install it.
  if (Proto->isBinaryOp())
    BinopPrecedence[Proto->getOperatorName()] = Proto->getBinaryPrecedence();

  // Create a basic block to start insertion.
  BasicBlock* BB = BasicBlock::Create(getGlobalContext(), "entry", TheFunction);
  Builder.SetInsertPoint(BB);

  // Add all arguments to the symbol table and create their allocas.
  Proto->CreateArgumentAllocas(TheFunction);

  // No control flow yet; one basic block with the return value.
  if (Value* RetVal = Body->Codegen()) {
    Builder.CreateRet(RetVal);
    verifyFunction(*TheFunction);
    TheFPM->run(*TheFunction);
    return TheFunction;
  }

  // Error reading the body; remove function.
  // TODO: this can delete a forward declaration.
  TheFunction->eraseFromParent();
  if (Proto->isBinaryOp())
    BinopPrecedence.erase(Proto->getOperatorName());
  return NULL;
}
