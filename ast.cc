#include "ast.h"

std::ostream& operator<<(std::ostream& stream, const FunctionAST& node) {
  return stream << "(FunctionAST " << *node.Proto << " " << *node.Body << ")";
}

std::ostream& operator<<(std::ostream& stream, const PrototypeAST& node) {
  stream << "(PrototypeAST \"" << node.Name << "\"";
  std::vector<std::string>::const_iterator iter = node.Args.begin();
  while (iter != node.Args.end())
    stream << " \"" << *iter++ << "\"";
  return stream << ")";
}

std::ostream& CallExprAST::print(std::ostream& stream) const {
  stream << "(CallExprAST \"" << Callee << "\"";
  std::vector<ExprAST*>::const_iterator iter = Args.begin();
  while (iter != Args.end())
    stream << " " << **iter++;
  return stream << ")";
}

std::ostream& BinaryExprAST::print(std::ostream& stream) const {
  return stream << "(BinaryExprAST " << Op << " " << *LHS << " " << *RHS;
}

std::ostream& VariableExprAST::print(std::ostream& stream) const {
  return stream << "(VariableExprAST " << Name << ")";
}

std::ostream& NumberExprAST::print(std::ostream& stream) const {
  return stream << "(NumberExprAST " << Val << ")";
}

std::ostream& operator<<(std::ostream& stream, const ExprAST& node) {
  return node.print(stream);
}

