#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <cstdio>
#include <cstdlib>

#include "llvm/Analysis/Passes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"

#include "ast.h"
#include "codegen.h"
#include "lexer.h"

using namespace llvm;

// CurTok: Holds the current token.
static int CurTok;

// getNextToken: Reads the next token from the lexer and stores it in CurTok.
static int getNextToken() {
  return CurTok = gettok();
}

// Rudimentary error handling routines.
static ExprAST* Error(const char* Str) {
  fprintf(stderr, "Error: %s\n", Str);
  return NULL;
}
static PrototypeAST* ErrorPrototype(const char* Str) {
  Error(Str);
  return NULL;
}
static FunctionAST* ErrorFunction(const char* Str) {
  Error(Str);
  return NULL;
}

static ExprAST* ParseExpression();

// numberexpr ::= number
static ExprAST* ParseNumberExpr() {
  // Eat and return the token.
  ExprAST* Result = new NumberExprAST(NumVal);
  getNextToken();
  return Result;
}

// parenexpr ::= '(' expression ')'
static ExprAST* ParseParenExpr() {
  getNextToken();  // Eat '('
  ExprAST* V = ParseExpression();
  if (!V) return NULL;

  if (CurTok != ')')
    return Error("expected ')'");

  getNextToken();  // Eat ')'
  return V;
}

// identifierexpr ::= identifier | identifier '(' expression* ')'
static ExprAST* ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;

  getNextToken();  // Eat identifier.

  // If not a function call, return the identifier.
  if (CurTok != '(')
    return new VariableExprAST(IdName);

  // Function call.
  // Eat '('.
  getNextToken();
  std::vector<ExprAST*> Args;
  if (CurTok != ')') {
    while (true) {
      ExprAST* Arg = ParseExpression();
      if (!Arg) return NULL;
      Args.push_back(Arg);
      
      if (CurTok == ')')
        break;
      if (CurTok != ',')
        return Error("Expected ')' or ',' in argument list");

      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();

  return new CallExprAST(IdName, Args);
}

// ifexpr ::= 'if' expression 'then' expression 'else' expression
static ExprAST* ParseIfExpr() {
  // Eat the 'if'.
  getNextToken();
  ExprAST* Cond = ParseExpression();
  if (!Cond) return NULL;

  if (CurTok != tok_then)
    return Error("expected then");
  getNextToken(); // Eat the 'then'
  ExprAST* Then = ParseExpression();
  if (!Then) return NULL;

  if (CurTok != tok_else)
    return Error("Expected else");
  getNextToken(); // Eat the 'else'
  ExprAST* Else = ParseExpression();
  if (!Else) return NULL;

  return new IfExprAST(Cond, Then, Else);
}

// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
static ExprAST* ParseForExpr() {
  // Eat the 'for'.
  getNextToken();

  if (CurTok != tok_identifier)
    return Error("Expected identifier after 'for'");

  std::string IdName = IdentifierStr;
  // Eat the identifier.
  getNextToken();

  if (CurTok != '=')
    return Error("Expected '=' in for loop");
  // Eat the '='.
  getNextToken();

  ExprAST* Start = ParseExpression();
  if (Start == NULL) return NULL;
  if (CurTok != ',')
    return Error("Expected ',' after start value in for loop");
  getNextToken();

  ExprAST* End = ParseExpression();
  if (End == NULL) return NULL;

  // Get the optional increment.
  ExprAST* Step = NULL;
  if (CurTok == ',') {
    getNextToken();
    Step = ParseExpression();
    if (Step == NULL) return NULL;
  }

  if (CurTok != tok_in)
    return Error("Expected 'in' in for loop");
  getNextToken();

  ExprAST* Body = ParseExpression();
  if (Body == NULL) return NULL;

  return new ForExprAST(IdName, Start, End, Step, Body);
}

// varexpr ::= 'var' identifier ('=' expression)?
//                 (',' identifier ('=' expression)?)* 'in 'expression;
static ExprAST* ParseVarExpr() {
  // Get the 'var'.
  getNextToken();

  std::vector<VarExprAST::VarAssign> VarNames;

  if (CurTok != tok_identifier)
    return Error("expected identifier after var");

  // Parse list of VarAssigns.
  while (true) {
    std::string Name = IdentifierStr;
    getNextToken();  // Eat identifier.

    // Read the optional identifier.
    ExprAST* Init = NULL;
    if (CurTok == '=') {
      getNextToken();  // Eat the '='.
      Init = ParseExpression();
      if (Init == NULL) return NULL;
    }

    VarNames.push_back(VarExprAST::VarAssign(Name, Init));

    // End of var list, exit loop.
    if (CurTok != ',') break;

    getNextToken();  // Eat the ','.
    if (CurTok != tok_identifier)
      return Error("expected identifier list after comma");
  }

  if (CurTok != tok_in)
    return Error("expected 'in' keyword after 'var'");
  getNextToken();  // Eat 'in'.

  ExprAST* Body = ParseExpression();
  if (Body == NULL) return 0;

  return new VarExprAST(VarNames, Body);
}

// primary ::= identifierexpr | numberexpr | parenexpr
static ExprAST* ParsePrimary() {
  switch (CurTok) {
    case tok_if: return ParseIfExpr();
    case tok_for: return ParseForExpr();
    case tok_var: return ParseVarExpr();
    case tok_identifier: return ParseIdentifierExpr();
    case tok_number: return ParseNumberExpr();
    case '(': return ParseParenExpr();
    default: return Error("unknown token when expecting an expression");
  }
}

// unary ::= primary | '!' unary
static ExprAST* ParseUnary() {
  // If the current token is not an operator, it must be a primary.
  if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
    return ParsePrimary();

  // If this is a unary operator, read it.
  int Opc = CurTok;
  getNextToken();
  if (ExprAST* Operand = ParseUnary())
    return new UnaryExprAST(Opc, Operand);

  return NULL;
}

// GetTokPrecedence: Returns the precedence of the pending binop token.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;
  
  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}

// binoprhs ::= ('+' primary)*
// ExprPrec defines the lowest precedence that can be consumed.
static ExprAST* ParseBinOpRHS(int ExprPrec, ExprAST* LHS) {
  // If a binop, find its precedence.
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If this binop has lower precedence than the current binop,
    // then we're done.
    if (TokPrec < ExprPrec)
      return LHS;

    // Definitely a binop or the TokPrec would have been zero.
    // Current state: [LHS op rest], where TokPrec is op's precedence
    // and op is stored in CurTok.
    int BinOp = CurTok;
    getNextToken();

    // Parse the next primary after the binop.
    ExprAST* RHS = ParseUnary();
    if (!RHS) return NULL;

    // Which way do we associate?
    // [(LHS op RHS) op rest] or [LHS op (RHS op rest)]
    // Check the next precedence to determine.
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      // [LHS op (RHS op rest)]
      // Find the right-hand side of RHS with the next op.
      RHS = ParseBinOpRHS(TokPrec + 1, RHS);
      if (!RHS) return NULL;
    }

    // Merge LHS/RHS and get the rest of the expression.
    // [(LHS op RHS) op rest]
    LHS = new BinaryExprAST(BinOp, LHS, RHS);
  }
}

// expression ::= primary binoprhs
static ExprAST* ParseExpression() {
  ExprAST* LHS = ParseUnary();
  if (!LHS) return NULL;

  return ParseBinOpRHS(0, LHS);
}

// prototype ::= identifier '(' identifier* ')'
//             | binary LETTER number? (identifier, identifier)
//             | unary LETTER (identifier)
static PrototypeAST* ParsePrototype() {
  std::string FnName;
  unsigned Kind = 0;  // 0 = identifier, 1 = unary, 2 = binary
  unsigned BinaryPrecedence = 30;

  switch (CurTok) {
  case tok_identifier:
    FnName = IdentifierStr;
    Kind = 0;
    getNextToken();
    break;
  case tok_unary:
    getNextToken();
    if (!isascii(CurTok))
      return ErrorPrototype("Expected unary operator");
    FnName = "unary";
    FnName += (char)CurTok;
    Kind = 1;
    getNextToken();
    break;
  case tok_binary:
    getNextToken();
    if (!isascii(CurTok))
      return ErrorPrototype("Expected binary operator");
    FnName = "binary";
    FnName += (char)CurTok;
    Kind = 2;
    getNextToken();

    // Read the optional precedence.
    if (CurTok == tok_number) {
      if (NumVal < 1 || NumVal > 100)
        return ErrorPrototype("Invalid precedence: must be 1..100");
      BinaryPrecedence = (unsigned)NumVal;
      getNextToken();
    }
    break;
  default:
    return ErrorPrototype("Expected function name in prototype");
  }

  if (CurTok != '(')
    return ErrorPrototype("Expected '(' in prototype");

  // Read the argument names.
  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);
  if (CurTok != ')')
    return ErrorPrototype("Expected ')' in prototype");

  // Eat ')'.
  getNextToken();

  if (Kind != 0 && ArgNames.size() != Kind)
    return ErrorPrototype("Invalid number of args for operator");

  return new PrototypeAST(FnName, ArgNames, Kind != 0, BinaryPrecedence);
}

// definition ::= 'def' prototype expression
static FunctionAST* ParseDefinition() {
  // Eat "def".
  getNextToken();
  PrototypeAST* Proto = ParsePrototype();
  if (Proto == NULL) return NULL;
  if (ExprAST* E = ParseExpression())
    return new FunctionAST(Proto, E);
  return NULL;
}

// external ::= "extern" prototype
static PrototypeAST* ParseExtern() {
  // Eat "extern".
  getNextToken();
  return ParsePrototype();
}

// toplevelexpr ::= expression
static FunctionAST* ParseTopLevelExpr() {
  if (ExprAST* E = ParseExpression()) {
    // Make an anonymous prototype.
    PrototypeAST* Proto =
        new PrototypeAST("", std::vector<std::string>(), false, 0);
    return new FunctionAST(Proto, E);
  }
  return NULL;
}

// -----------------------------------------------------------------------------
// Drivers

static ExecutionEngine *TheExecutionEngine;

static void HandleDefinition() {
  if (FunctionAST* F = ParseDefinition()) {
    if (Function* LF = F->Codegen()) {
      fprintf(stderr, "Read function definition:");
      LF->dump();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (PrototypeAST* P = ParseExtern()) {
    if (Function* F = P->Codegen()) {
      fprintf(stderr, "Read extern: ");
      F->dump();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (FunctionAST* F = ParseTopLevelExpr()) {
    if (Function* LF = F->Codegen()) {
      LF->dump();
      // JIT the function and get a pointer to the generated code.
      void* FPtr = TheExecutionEngine->getPointerToFunction(LF);
      // Cast it to the right type so we can call it.
      double (*FP)() = (double (*)())(intptr_t)FPtr;
      fprintf(stderr, "Evaluated to %f\n", FP());
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

// top ::= definition | external | expression | ';'
static void MainLoop() {
  while (true) {
    fprintf(stderr, "k> ");
    switch (CurTok) {
      case tok_eof: return;
      case ';': getNextToken(); break;  // Ignore top-level semicolons.
      case tok_def: HandleDefinition(); break;
      case tok_extern: HandleExtern(); break;
      default: HandleTopLevelExpression(); break;
    }
  }
}

// -----------------------------------------------------------------------------
// Main

// Native function implementation example.
/// putchard - putchar that takes a double and returns 0.
extern "C"
double putchard(double X) {
  putchar((char)X);
  return 0;
}
/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" 
double printd(double X) {
  printf("%f\n", X);
  return 0;
}

int main() {
  InitializeNativeTarget();
  LLVMContext& Context = getGlobalContext();

  // Install standard binary operators.
  BinopPrecedence['='] = 2;
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  // Prime the first token.
  fprintf(stderr, "k> ");
  getNextToken();

  // Make the code module.
  TheModule = new Module("my cool jit", Context);

  // Set up the JIT, which takes ownership of the module.
  std::string ErrStr;
  TheExecutionEngine = EngineBuilder(TheModule).setErrorStr(&ErrStr).create();
  if (!TheExecutionEngine) {
    fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
    exit(EXIT_FAILURE);
  }

  // Set up the optimizer pipeline.
  FunctionPassManager OurFPM(TheModule);
  // Register data structure layout.
  OurFPM.add(new DataLayout(*TheExecutionEngine->getDataLayout()));
  // Alias analysis for GVN.
  OurFPM.add(createBasicAliasAnalysisPass());
  // Promote allocas to registers.
  OurFPM.add(createPromoteMemoryToRegisterPass());
  // Peephole optimizations, bit-twiddling.
  OurFPM.add(createInstructionCombiningPass());
  // Expression reassociation.
  OurFPM.add(createReassociatePass());
  // GVN and Common Subexpression Elimination.
  OurFPM.add(createGVNPass());
  // CFG optimizations: delete unreachable blocks, etc.
  OurFPM.add(createCFGSimplificationPass());
  OurFPM.doInitialization();
  TheFPM = &OurFPM;

  // Run the interpreter loop.
  MainLoop();

  TheFPM = NULL;

  // Print all generated code.
  TheModule->dump();
  
  return EXIT_SUCCESS;
}
