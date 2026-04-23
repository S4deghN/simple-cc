#ifndef CC_H
#define CC_H

#include <stddef.h>
#include <stdbool.h>

//
// utils.c
//

#define swap(a, b) do { typeof(a) tmp = (a); (a) = (b); (b) = tmp; } while(0)
#define double_case(a, b) ((((int)(a)) << 8) | (int)(b))

typedef struct {
    char *path;
    char *str;
    size_t len;
} File;

char *str_find_next(char *str_end, char *cursor, char c);
char *str_find_prev(char *str_start, char *cursor, char c);
int align_to(int n, int align);

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
void diag_tok(Token* tok, char *fmt, ...);
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
    ND_ASSIGN,
    ND_EQ,
    ND_NE,
    ND_LT,
    ND_LTE,
    ND_NUM,
    ND_VAR,
    ND_FUNCALL,
    ND_ADDR,
    ND_DEREF,
    ND_EXPR_STMT,
    ND_RETURN,
    ND_BLOCK,
    ND_IF,
    ND_FOR, // "for" or "while"
    ND_DO, // "do while"
} NodeKind;
char *nd_kind_str(NodeKind kind);

typedef struct Type Type;
typedef struct Node Node;

typedef struct Var Var;
struct Var {
    Var *next;
    Token *tok;
    Type *ty;
    int stack_offset;
};

typedef struct Function Function;
struct Function {
    Node *body;
    Var *locals;
    int stack_size;
};

struct Node {
    NodeKind kind;
    Token *tok;
    Type *ty;
    Node *next;

    // Ordered expr
    Node *lhs;
    Node *rhs;

    // If and For statement
    Node *cond;
    Node *then;
    Node *els;
    Node *init;
    Node *iter;

    // Block
    Node *body;

    // Number
    int val;

    // Variable
    Var *var;
};

Function *parse(Token *tok);
void expect_node(Node *node, NodeKind kind);
void expect_node_many(Node *node, int n, ...);
void print_tree(const Node *root, char *prefix);

//
// type.c
//

typedef enum {
    TY_INT,
    TY_PTR,
} TypeKind;
char *ty_kind_str(TypeKind kind);

struct Type {
    TypeKind kind;
    Type *base; // pointer
    Token *name;
};

extern Type *ty_int;

bool type_is(Node *node, TypeKind kind);
Type *pointer_to(Type *base);
void add_type(Node *node);

//
// codegen.c
//

void codegen(Function *prog);

#endif
