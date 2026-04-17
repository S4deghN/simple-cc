#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

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

Node *new_num(Token *tk) {
    Node *node = new_node(ND_NUM);
    node->val = strtoul(tk->str, NULL, 10);
    return node;
}

void travers(Node *node, int depth) {
    if (node->kind == ND_NUM)
        printf("%*.s%d\n", depth*3, "", node->val);
    else
        printf("%*.s%s\n", depth*3, "", nd_kind_str(node->kind));

    if (node->rhs) travers(node->rhs, depth + 1);
    if (node->lhs) travers(node->lhs, depth + 1);
}

void expect(Token *tok, TokenKind kind)
{
    if (tok->kind != kind)
        error_tok(tok, "Expected '%s', got '%s'", tk_kind_str(kind), tk_kind_str(tok->kind));
}

bool skip(Token **mark, TokenKind kind) {
    Token *tk = *mark;
    if (tk->kind == kind) {
        tk = tk->next;
        *mark = tk;
        return true;
    }
    return false;
}

void expect_skip(Token **mark, TokenKind kind)
{
    if (!skip(mark, kind))
        error_tok(*mark, "Expected '%s', got '%s'", tk_kind_str(kind), tk_kind_str((*mark)->kind));
}

Node *expr(Token **mark);
Node *primary(Token **mark);
Node *mul(Token **mark);

Node *primary(Token **mark)
{
    Node *node;
    Token *tk = *mark;

    if (tk->kind == TK_NUM) {
        node = new_num(tk);
        tk = tk->next;
    } else if (skip(&tk, '(')) {
        node = expr(&tk);
        expect_skip(&tk, ')');
    } else {
        error_tok(tk, "Expected primary token!");
    }

    *mark = tk;
    return node;
}

Node *mul(Token **mark)
{
    Token *tk = *mark;

    Node *node = primary(&tk);
    for (;;) {
        if (skip(&tk, '*')) {
            node = new_binary(ND_MUL, node, primary(&tk));
            continue;
        }
        if (skip(&tk, '/')) {
            node = new_binary(ND_DIV, node, primary(&tk));
            continue;
        }
        *mark = tk;
        return node;
    }
}

Node *expr(Token **mark)
{
    Token *tk = *mark;

    Node *node = mul(&tk);
    for (;;) {
        if (skip(&tk, '+')) {
            node = new_binary(ND_ADD, node, mul(&tk));
            continue;
        }
        if (skip(&tk, '-')) {
            node = new_binary(ND_SUB, node, mul(&tk));
            continue;
        }
        *mark = tk;
        return node;
    }
}

static int depth;

static void push(void)
{
    printf("  push %%rax\n");
    depth++;
}

static void pop(char *arg)
{
    printf("  pop %s\n", arg);
    depth--;
}

static void gen_expr(Node *node)
{
    if (node->kind == ND_NUM) {
        printf("  mov $%d, %%rax\n", node->val);
        return;
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");

    switch (node->kind) {
    case ND_ADD:
        printf("  add %%rdi, %%rax\n");
        return;
    case ND_SUB:
        printf("  sub %%rdi, %%rax\n");
        return;
    case ND_MUL:
        printf("  imul %%rdi, %%rax\n");
        return;
    case ND_DIV:
        printf("  cqo\n");
        printf("  idiv %%rdi\n");
        return;
    }
    error("invalid expression");
}

int main(int argc, char *argv[]) {

    if (argc != 2) error("%s: invalid number of arguments!", argv[0]);

    Token *tk = tokenize(&(File){ .str = argv[1], .len = strlen(argv[1]), .path = "argv[1]" });

    Node *node = expr(&tk);
    expect(tk, TK_EOF);

    // travers(node, 0);

    printf(".global main\n");
    printf("main:\n");

    gen_expr(node);
    assert(depth == 0);

    printf("ret\n");

    return 0;
}
