#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define LEXER_IMPLEMENTATION
#include "lexer.h"
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

int main() {

    char *file_path = "example.c";

    char buff[1024 * 1024];
    FILE *file_stream = fopen(file_path, "r");
    int n = fread(buff, 1, sizeof(buff), file_stream);
    if ((uint)n >= sizeof(buff)) {
        fprintf(stderr, "ERROR: Input is too big\n");
        return 1;
    }

    Token *tok = tokenize(buff, n, file_path);
    error_tok(tok, "ERROR!");
    for (; tok; tok = tok->next) {
        printf("%s:\t%.*s\n", tk_kind_str(tok->kind), (int)tok->len, tok->str);
    }

    return 0;
}
