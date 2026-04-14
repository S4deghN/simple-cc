#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define LEXER_IMPLEMENTATION
#include "lexer.h"

void error(Lexer *l, char *fmt, ...) {
    lexer_report_line(l);
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    vfprintf(stderr, fmt, arg_ptr);
    fprintf(stderr, "\n");
    exit(1);
}

void expect(Lexer *l, Token tk , TokenKind kind) {
    if (tk.kind != kind) {
        error(l, "Expected: '%s', got '%s'",
            tk_kind_str(kind), tk_kind_str(tk.kind));
    }
}

Token expect_next(Lexer *l, TokenKind kind) {
    Token tk = lexer_next(l);
    expect(l, tk, kind);
    return tk;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "%s: invalid number of arguments\n", argv[0]);
    return 1;
  }

  Lexer l;
  lexer_init(&l, argv[1], strlen(argv[1]), "arg");

  printf("  .globl main\n");
  printf("main:\n");

  Token tk = expect_next(&l, TK_NUMBER);
  printf("  mov $%.*s, %%rax\n", tk.len, tk.str);

  for (Token tk = lexer_next(&l); tk.kind != TK_EOF; tk = lexer_next(&l)) {
      if (tk.kind == TK_PLUS) {
          Token tk = expect_next(&l, TK_NUMBER);
          printf("  add $%.*s, %%rax\n", tk.len, tk.str);
      } else if (tk.kind == TK_MINUS) {
          Token tk = expect_next(&l, TK_NUMBER);
          printf("  sub $%.*s, %%rax\n", tk.len, tk.str);
      } else {
          expect(&l, tk, TK_EOF);
      }
  }
  printf("  ret\n");

  return 0;
}
