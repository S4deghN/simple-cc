#ifndef CC_H
#define CC_H

#include <stddef.h>

typedef struct {
    char *path;
    char *str;
    size_t len;
} File;

//
// tokenize.c
//

typedef enum {
    TK_EOF = 0,
    // skip litteral ascii tokens
    TK_ID = 128,
    TK_NUM,
} TokenKind;
char *tk_kind_str(TokenKind kind);

typedef struct Token Token;
struct Token {
    TokenKind kind;
    Token *next;
    char *str;
    int len;
    size_t line_nr;
    File *file;
};

Token *tokenize(File *file);
Token *new_tok(TokenKind kind, char *str, size_t len, File *file, size_t line_nr);
void error_at(File *file, size_t line_nr, size_t offset, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);

#endif
