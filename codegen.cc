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
static std::map<std::string, Value*> NamedValues;

// Optimization pipeline.
FunctionPassManager* TheFPM;

Value* NumberExprAST::Codegen() {
  return ConstantFP::get(getGlobalContext(), APFloat(Val));
}

Value* VariableExprAST::Codegen() {
  Value* V = NamedValues[Name];
  return V ? V : ErrorValue("Unknown variable name");
}

Value* BinaryExprAST::Codegen() {
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
  Value* StartVal = Start->Codegen();
  if (StartVal == NULL) return NULL;

  // Create the basic block for the loop header and insert it after the
  // current block.
  Function* TheFunction = Builder.GetInsertBlock()->getParent();
  BasicBlock* PreHeaderBB = Builder.GetInsertBlock();
  BasicBlock* LoopBB =
      BasicBlock::Create(getGlobalContext(), "loop", TheFunction);

  // Explicit fallthrough into LoopBB from PreHeaderBB.
  Builder.CreateBr(LoopBB);

  // Start insertion into LoopBB.
  Builder.SetInsertPoint(LoopBB);

  // Start the PHI node with an entry for Start.
  PHINode* Variable = Builder.CreatePHI(
      Type::getDoubleTy(getGlobalContext()), 2, VarName.c_str());
  Variable->addIncoming(StartVal, PreHeaderBB);

  // Remember the old value of ValName in case we shadow it.
  Value* OldVal = NamedValues[VarName];
  NamedValues[VarName] = Variable;

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

  Value* NextVar = Builder.CreateFAdd(Variable, StepVal, "nextvar");

  // Compute the end condition.
  Value* EndCond = End->Codegen();
  if (EndCond == NULL) return EndCond;

  // Convert to a bool by comparing with 0.0.
  EndCond = Builder.CreateFCmpONE(
      EndCond, ConstantFP::get(getGlobalContext(), APFloat(0.0)), "loopcond");
  
  // Create the "after loop" block and insert it.
  BasicBlock* LoopEndBB = Builder.GetInsertBlock();
  BasicBlock* AfterBB =
      BasicBlock::Create(getGlobalContext(), "afterloop", TheFunction);

  // Insert the conditional branch into the end of LoopEndBB.
  Builder.CreateCondBr(EndCond, LoopBB, AfterBB);

  // New code goes in AfterBB.
  Builder.SetInsertPoint(AfterBB);

  // Add an entry point to the PHI node for the loop back-edge.
  Variable->addIncoming(NextVar, LoopEndBB);

  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for loop always returns 0.0.
  return Constant::getNullValue(Type::getDoubleTy(getGlobalContext()));
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
    NamedValues[Args[Idx]] = AI;
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
  return NULL;
}
