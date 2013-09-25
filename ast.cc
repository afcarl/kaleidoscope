#include "ast.h"

#include <cassert>

std::ostream& operator<<(std::ostream& stream, const FunctionAST& node) {
  return stream << "(FunctionAST " << *node.Proto << " " << *node.Body << ")";
}

std::ostream& operator<<(std::ostream& stream, const PrototypeAST& node) {
  stream << "(PrototypeAST \"" << node.Name << "\"";
  std::vector<std::string>::const_iterator iter = node.Args.begin();
  while (iter != node.Args.end())
    stream << " \"" << *iter++ << "\"";
  if (node.isBinaryOp())
    stream << " " << node.getBinaryPrecedence();
  return stream << ")";
}

char PrototypeAST::getOperatorName() const {
  assert(isUnaryOp() || isBinaryOp());
  return Name[Name.size()-1];
}

std::ostream& CallExprAST::print(std::ostream& stream) const {
  stream << "(CallExprAST \"" << Callee << "\"";
  std::vector<ExprAST*>::const_iterator iter = Args.begin();
  while (iter != Args.end())
    stream << " " << **iter++;
  return stream << ")";
}

std::ostream& BinaryExprAST::print(std::ostream& stream) const {
  return stream << "(BinaryExprAST " << Op << " " << *LHS << " " << *RHS << ")";
}

std::ostream& UnaryExprAST::print(std::ostream& stream) const {
  return stream << "(UnaryExprAST " << Opcode << " " << *Operand << ")";
}

std::ostream& VariableExprAST::print(std::ostream& stream) const {
  return stream << "(VariableExprAST " << Name << ")";
}

std::ostream& NumberExprAST::print(std::ostream& stream) const {
  return stream << "(NumberExprAST " << Val << ")";
}

std::ostream& IfExprAST::print(std::ostream& stream) const {
  return stream << "(IfExprAST " << Cond << " " << Then << " " << Else << ")";
}

std::ostream& ForExprAST::print(std::ostream& stream) const {
  return stream << "(ForExprAST " << VarName
                << " " << Start
                << " " << End
                << " " << Step << ")";
}

std::ostream& VarExprAST::print(std::ostream& stream) const {
  stream << "(VarExprAST (";
  for (std::vector<VarAssign>::const_iterator iter = VarNames.begin();
       iter != VarNames.end();
       ++iter) {
    stream << "(" << iter->first << " " << *iter->second << ")";
    if (iter+1 != VarNames.end()) stream << " ";
  }
  return stream << ") " << *Body << ")";
}

std::ostream& operator<<(std::ostream& stream, const ExprAST& node) {
  return node.print(stream);
}

