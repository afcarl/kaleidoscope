#include "lexer.h"

std::string IdentifierStr;
double NumVal;

int gettok() {
  static int LastChar = ' ';

  // Skip whitespace.
  while (isspace(LastChar))
    LastChar = getchar();

  // identifier: [a-zA-Z][a-zA-Z0-9]*
  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum(LastChar = getchar()))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def") return tok_def;
    if (IdentifierStr == "extern") return tok_extern;
    if (IdentifierStr == "if") return tok_if;
    if (IdentifierStr == "then") return tok_then;
    if (IdentifierStr == "else") return tok_else;
    if (IdentifierStr == "for") return tok_for;
    if (IdentifierStr == "in") return tok_in;

    return tok_identifier;
  }

  // number: [0-9.]+
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
