#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "cc.h"

// Reports an error and exit.
static void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

typedef enum {
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_NUM,
} NodeKind;

char *nd_kind_str(NodeKind kind) {
    switch (kind) {
        case ND_ADD: return "ADD";
        case ND_SUB: return "SUB";
        case ND_MUL: return "MUL";
        case ND_DIV: return "DIV";
        case ND_NUM: return "NUM";
        default: return "???";
    }
}

typedef struct Node Node;
struct Node {
    NodeKind kind;
    Node *lhs;
    Node *rhs;

    int val;
};

Node *new_node(NodeKind kind) {
    Node *node = calloc(1, sizeof(*node));
    node->kind = kind;
    return node;
}

Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = new_node(kind);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

Node *new_num(Token tk) {
    Node *node = new_node(ND_NUM);
    node->val = strtoul(tk.str, NULL, 10);
    return node;
}

void travers(Node *node, int depth) {
    printf("%*.s%s\n", depth, "", nd_kind_str(node->kind));
    if (node->lhs) travers(node->lhs, depth + 1);
    if (node->rhs) travers(node->rhs, depth + 1);
}

void expect(Token *tok, TokenKind kind)
{
    if (tok->kind != kind)
        error_tok(tok, "Expected '%s', got '%s'", tk_kind_str(kind), tk_kind_str(tok->kind));
}

int main(int argc, char *argv[]) {

    if (argc != 2) error("%s: invalid number of arguments!", argv[0]);

    Token *tok = tokenize(&(File){ .str = argv[1], .len = strlen(argv[1]), .path = "argv[1]" });

    printf(".global main\n");
    printf("main:\n");

    expect(tok, TK_NUM);
    printf("    mov $%.*s, %%rax\n", tok->len, tok->str);
    tok = tok->next;

    for (; tok; tok = tok->next) {
        // fprintf(stderr, "%s:\t%.*s\n", tk_kind_str(tok->kind), tok->len, tok->str);
        if (tok->kind == '+') {
            expect(tok = tok->next, TK_NUM);
            printf("    add $%.*s, %%rax\n", tok->len, tok->str);
        } else if (tok->kind == '-') {
            expect(tok = tok->next, TK_NUM);
            printf("    sub $%.*s, %%rax\n", tok->len, tok->str);
        } else if (tok->kind != TK_EOF) {
            error_tok(tok, "Unexpected token!");
        }
    }

    printf("    ret\n");

    return 0;
}
