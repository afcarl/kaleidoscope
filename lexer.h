#include <string>

enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
};

// The string corresponding to the last token if it was tok_identifier.
extern std::string IdentifierStr;

// The number corresponding to the last token if it was tok_number.
extern double NumVal;

// gettok: Returns the next token from standard input.
int gettok();
