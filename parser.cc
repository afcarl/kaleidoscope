#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

// -----------------------------------------------------------------------------
// Lexer

enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
};

static std::string IdentifierStr;  // Filled in if tok_identifier
static double NumVal;              // Filled in if tok_number

// gettok: Returns the next token from standard input.
static int gettok() {
  static int LastChar = ' ';

  // Skip whitespace.
  while (isspace(LastChar))
    LastChar = getchar();

  // Identifier: [a-zA-Z][a-zA-Z0-9]*
  if (isalpha(LastChar)) {
    IdentifierStr += LastChar;
    while (isalnum(LastChar = getchar()))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    return tok_identifier;
  }

  // Number: [0-9.]+
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  // Comment goes until the end of the line.
  if (LastChar == '#') {
    do {
      LastChar = getchar();
    } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }

  // Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Nothing matches -- eat and return the character.
  int ThisChar = LastChar;
  LastChar = getchar();
  return ThisChar;
}

// -----------------------------------------------------------------------------
// Parser

// ExprAST: Base class for all expression nodes.
class ExprAST {
 public:
  virtual ~ExprAST() {}
};

// NumberExprAST: Expression class for numeric literals.
class NumberExprAST : public ExprAST {
 public:
  NumberExprAST(double val) : Val(val) {}

 private:
  double Val;
};

// VariableExprAST: Expression class for referencing a variable.
class VariableExprAST : public ExprAST {
 public:
  VariableExprAST(const std::string& name) : Name(name) {}

 private:
  std::string Name;
};

// BinaryExprAST: Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
 public:
  BinaryExprAST(char op, ExprAST* lhs, ExprAST* rhs)
      : Op(op), LHS(lhs), RHS(rhs) {}

 private:
  char Op;
  ExprAST* LHS;
  ExprAST* RHS;
};

// CallExprAST: Expression class for function calls.
class CallExprAST : public ExprAST {
 public:
  CallExprAST(const std::string& callee, std::vector<ExprAST*>& args)
    : Callee(callee), Args(args) {}

 private:
  std::string Callee;
  std::vector<ExprAST*> Args;
};

// PrototypeAST: Represents a function signature (name and arity) as well
// as its argument names.
class PrototypeAST {
 public:
  PrototypeAST(const std::string& name, const std::vector<std::string>& args)
      : Name(name), Args(args) {}

 private:
  std::string Name;
  std::vector<std::string> Args;
};

// FunctionAST: Represents a function definition.
class FunctionAST {
 public:
  FunctionAST(PrototypeAST* proto, ExprAST* body) : Proto(proto), Body(body) {}

 private:
  PrototypeAST* Proto;
  ExprAST* Body;
};
