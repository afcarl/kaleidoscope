#include <cstdio>
#include <map>

#include "llvm/Analysis/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

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

// LLVM instruction generator.
static IRBuilder<> Builder(getGlobalContext());

// Symbol table.
static std::map<std::string, Value*> NamedValues;

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
    default: return ErrorValue("invalid binary operator");
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
    NamedValues[Args[Idx]] = AI;
  }

  return F;
}

Function* FunctionAST::Codegen() {
  NamedValues.clear();

  Function* TheFunction = Proto->Codegen();
  if (!TheFunction)
    return NULL;

  // Create a basic block to start insertion.
  BasicBlock* BB = BasicBlock::Create(getGlobalContext(), "entry", TheFunction);
  Builder.SetInsertPoint(BB);

  // No control flow yet; one basic block with the return value.
  if (Value* RetVal = Body->Codegen()) {
    Builder.CreateRet(RetVal);
    verifyFunction(*TheFunction);
    return TheFunction;
  }

  // Error reading the body; remove function.
  // TODO: this can delete a forward declaration.
  TheFunction->eraseFromParent();
  return NULL;
}
