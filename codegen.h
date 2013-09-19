#include <map>

namespace llvm {
class FunctionPassManager;
class Module;
}  // namespace llvm

extern llvm::Module* TheModule;
extern llvm::FunctionPassManager* TheFPM;

// BinopPrecedence: Maintains the precedence for each binary operator.
// 1 is the lowest precedence.
extern std::map<char, int> BinopPrecedence;
