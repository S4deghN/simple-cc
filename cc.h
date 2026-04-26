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
    TK_KEYWORD,
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
int get_number(Token *tok);
void error_at(File *file, size_t line_nr, size_t offset, char *fmt, ...);
void diag_tok(Token* tok, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
void error(char *fmt, ...);

//
// parser.c
//

typedef enum {
    // expression unists/ primary expressions
    ND_NUM,
    ND_VAR,
    ND_FUNCALL,

    // binary operators
    ND_ASSIGN,
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_NEG,
    ND_EQ,
    ND_NE,
    ND_GT,
    ND_GTE,
    ND_LT,
    ND_LTE,

    // unary operators
    ND_ADDR,
    ND_DEREF,
    ND_RETURN,

    // code blocks
    ND_BLOCK,      // Used in case a language constructs requires a list of operations to be executed in sequence like assignment in declaration or body of a function or the program itself. The `body` field is used as linked list of those operations.
    ND_IF,
    ND_FOR,        // "for" or "while"
    ND_DO,         // "do while"
    ND_EXPR_STMT,
} NodeKind;
char *nd_kind_str(NodeKind kind);

typedef struct Type Type;
typedef struct Node Node;

typedef struct Ident Ident;
struct Ident {
    Ident *next;
    Token *tok;
    Type *ty;

    bool is_function;
    bool is_local;

    // Function
    Ident *parameters;
    size_t parameters_count;
    Ident *locals;
    int stack_size;

    // Local variable
    int stack_offset;


    Node *body;
};

struct Node {
    NodeKind kind;
    Token *tok;
    Type *ty;
    Node *next;

    // Ordered expr
    Node *lhs;
    Node *rhs;

    // Function call args
    Node *args;

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

    // Variable or function call
    Ident *ident;
};

Ident *parse(Token *tok);
void expect_node(Node *node, NodeKind kind);
void expect_node_many(Node *node, int n, ...);
void print_tree(const Node *root, char *prefix);

//
// type.c
//

typedef enum {
    TY_INT,
    TY_PTR,
    TY_FUNC,
    TY_ARRAY,
} TypeKind;
char *ty_kind_str(TypeKind kind);

struct Type {
    TypeKind kind;
    int size;

    Token *ty_name;
    Token *id_name;
    bool no_id_name; // For unnamed function parameter.

    // Pointer-to or array-of
    Type *base;

    // Array
    int array_len;

    // Function
    Type *ret_ty;
    Type *params;
    Type *next;
    int param_count;
};

extern Type *ty_int;

bool type_is(Node *node, TypeKind kind);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *array_of(Type *base, int len);
Type *func_type(Type *ret_ty);
void add_type(Node *node);

//
// codegen.c
//

void codegen(Ident *program);

#endif
