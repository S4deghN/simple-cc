#ifndef CC_H
#define CC_H

#include <stddef.h>

//
// utils.c
//

typedef struct {
    char *path;
    char *str;
    size_t len;
} File;

char *str_find_next(char *str_end, char *cursor, char c);
char *str_find_prev(char *str_start, char *cursor, char c);

//
// tokenize.c
//

typedef enum {
    TK_EOF = 0,
    // skip litteral ascii tokens
    TK_ID = 128,
    TK_NUM,
    TK_GREQ,
    TK_LTEQ,
    TK_EQ,
    TK_NOEQ,
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
void get_tok_line(Token *tok, char **str, int *len);
void error_at(File *file, size_t line_nr, size_t offset, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
void error(char *fmt, ...);

//
// parser.c
//

typedef enum {
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_NEG,
    ND_EQ,
    ND_NE,
    ND_LT,
    ND_LTE,
    ND_ASSIGN,
    ND_EXPR_STMT,
    ND_NUM,
    ND_VAR,
} NodeKind;
char *nd_kind_str(NodeKind kind);

typedef struct Node Node;
struct Node {
    NodeKind kind;
    Token *tok;
    Node *next;
    Node *lhs;
    Node *rhs;

    int val;
    char name;
};

Node *parse(Token *tok);
void expect_node(Node *node, NodeKind kind);
void print_tree(const Node *root, char *prefix);

//
// codegen.c
//

void codegen(Node *node);

#endif
