#include <string>
#include <vector>
#include <ostream>

namespace llvm {
class Function;
class Value;
}  // namespace llvm

// ExprAST: Base class for all expression nodes.
class ExprAST {
 public:
  virtual ~ExprAST() {}
  virtual llvm::Value* Codegen() = 0;

 private:
  friend std::ostream& operator<<(std::ostream& stream, const ExprAST& node);
  virtual std::ostream& print(std::ostream& stream) const = 0;
};

// NumberExprAST: Expression class for numeric literals.
class NumberExprAST : public ExprAST {
 public:
  NumberExprAST(double val) : Val(val) {}
  virtual llvm::Value* Codegen() override;

 private:
  virtual std::ostream& print(std::ostream& stream) const override;
  double Val;
};

// VariableExprAST: Expression class for referencing a variable.
class VariableExprAST : public ExprAST {
 public:
  VariableExprAST(const std::string& name) : Name(name) {}
  std::string getName() const { return Name; }
  virtual llvm::Value* Codegen() override;

 private:
  virtual std::ostream& print(std::ostream& stream) const override;
  std::string Name;
};

// BinaryExprAST: Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
 public:
  BinaryExprAST(char op, ExprAST* lhs, ExprAST* rhs)
      : Op(op), LHS(lhs), RHS(rhs) {}
  virtual llvm::Value* Codegen() override;

 private:
  virtual std::ostream& print(std::ostream& stream) const override;
  char Op;
  ExprAST* LHS;
  ExprAST* RHS;
};

// UnaryExprAST: Expression class for a unary operator.
class UnaryExprAST : public ExprAST {
 public:
  UnaryExprAST(char opcode, ExprAST* operand)
      : Opcode(opcode), Operand(operand) {}
  virtual llvm::Value* Codegen() override;

 private:
  virtual std::ostream& print(std::ostream& stream) const override;
  char Opcode;
  ExprAST* Operand;
};

// CallExprAST: Expression class for function calls.
class CallExprAST : public ExprAST {
 public:
  CallExprAST(const std::string& callee, std::vector<ExprAST*>& args)
    : Callee(callee), Args(args) {}
  virtual llvm::Value* Codegen() override;

 private:
  virtual std::ostream& print(std::ostream& stream) const override;
  std::string Callee;
  std::vector<ExprAST*> Args;
};

// IfExprAST: Expression class for if/then/else.
class IfExprAST : public ExprAST {
 public:
  IfExprAST(ExprAST* condNode, ExprAST* thenNode, ExprAST* elseNode)
      : Cond(condNode), Then(thenNode), Else(elseNode) {}
  virtual llvm::Value* Codegen() override;

 private:
  virtual std::ostream& print(std::ostream& stream) const override;
  ExprAST* Cond;
  ExprAST* Then;
  ExprAST* Else;
};

// ForExprAST: Expression class for for loops.
class ForExprAST : public ExprAST {
  public:
   ForExprAST(const std::string& varname,
              ExprAST* start,
              ExprAST* end,
              ExprAST* step,
              ExprAST* body)
       : VarName(varname), Start(start), End(end), Step(step), Body(body) {}
   virtual llvm::Value* Codegen();
  private:
   virtual std::ostream& print(std::ostream& stream) const override;
   std::string VarName;
   ExprAST* Start;
   ExprAST* End;
   ExprAST* Step;
   ExprAST* Body;
};

// VarExprAST: Expression class for var/in.
class VarExprAST : public ExprAST {
 public:
  typedef std::pair<std::string, ExprAST*> VarAssign;
  VarExprAST(const std::vector<VarAssign>& varnames,
             ExprAST* body)
      : VarNames(varnames), Body(body) {}
  virtual llvm::Value* Codegen();

 private:
  virtual std::ostream& print(std::ostream& stream) const override;
  std::vector<VarAssign> VarNames;
  ExprAST* Body;
};

// PrototypeAST: Represents a function signature (name and arity) as well
// as its argument names.
class PrototypeAST {
 public:
  PrototypeAST(const std::string& name,
               const std::vector<std::string>& args,
               bool isOperator,
               unsigned precedence)
      : Name(name),
        Args(args),
        IsOperator(isOperator),
        Precedence(precedence) {}
  llvm::Function* Codegen();
  bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
  bool isBinaryOp() const { return IsOperator && Args.size() == 2; }
  unsigned getBinaryPrecedence() const { return Precedence; }
  char getOperatorName() const;
  void CreateArgumentAllocas(llvm::Function* F);

 private:
  friend std::ostream& operator<<(std::ostream& stream,
                                  const PrototypeAST& node);
  std::string Name;
  std::vector<std::string> Args;
  bool IsOperator;
  unsigned Precedence;
};

// FunctionAST: Represents a function definition.
class FunctionAST {
 public:
  FunctionAST(PrototypeAST* proto, ExprAST* body) : Proto(proto), Body(body) {}
  llvm::Function* Codegen();

 private:
  friend std::ostream& operator<<(std::ostream& stream,
                                  const FunctionAST& node);
  PrototypeAST* Proto;
  ExprAST* Body;
};
