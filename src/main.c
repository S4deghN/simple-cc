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
    ND_NEG,
    ND_LT,
    ND_LTE,
    ND_EQ,
    ND_NE,
} NodeKind;

char *nd_kind_str(NodeKind kind) {
    switch (kind) {
        case ND_ADD: return "ADD";
        case ND_SUB: return "SUB";
        case ND_MUL: return "MUL";
        case ND_DIV: return "DIV";
        case ND_NUM: return "NUM";
        case ND_NEG: return "NEG";
        case ND_LT:  return "LT";
        case ND_LTE: return "LTE";
        case ND_EQ:  return "EQ";
        case ND_NE:  return "NE";
        default: return "???";
    }
}

typedef struct Node Node;
struct Node {
    NodeKind kind;
    Token *tok;
    Node *lhs;
    Node *rhs;

    int val;
};

void print_tree(const Node *node, char *prefix_buff, int prefix_cursor, int is_right) {
    if (!node) return;

    char *add;
    char *fmt;
    if (is_right == -1) { // root
        fmt = "%.*s%.*s\n";
        add = "";
    } else if (is_right) { // right branch
        fmt = "%.*s├─ %.*s\n";
        add = "│  ";
    } else { // left branch
        fmt = "%.*s└─ %.*s\n";
        add = "   ";
    }

    char *node_str;
    int node_str_len;
    if (node->kind == ND_NUM) {
        node_str = node->tok->str;
        node_str_len = node->tok->len;
    } else {
        node_str = nd_kind_str(node->kind);
        node_str_len = strlen(node_str);
    }
    printf(fmt, prefix_cursor, prefix_buff, node_str_len, node_str);

    memcpy(&prefix_buff[prefix_cursor], add, strlen(add));
    prefix_cursor += strlen(add);

    // Recurse: right then left so right appears above left
    print_tree(node->rhs, prefix_buff, prefix_cursor, 1);
    print_tree(node->lhs, prefix_buff, prefix_cursor, 0);
}

void print_tree_root(const Node *root, char *prefix) {
    char prefix_buff[256]; // must be big enough or segfault
    memcpy(prefix_buff, prefix, strlen(prefix));
    print_tree(root, prefix_buff, strlen(prefix), -1);
}


Node *new_node(NodeKind kind) {
    Node *node = calloc(1, sizeof(*node));
    node->kind = kind;
    return node;
}

Node *new_unary(NodeKind kind, Node *expr) {
    Node *node = new_node(kind);
    node->lhs = expr;
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
Node *equality(Token **mark);
Node *relational(Token **mark);
Node *add(Token **mark);
Node *mul(Token **mark);
Node *unary(Token **mark);
Node *primary(Token **mark);

Node *expr(Token **mark)
{
    return equality(mark);
}

Node *equality(Token **mark)
{
    Token *tk = *mark;

    Node *node = relational(&tk);
    for (;;) {
        if (skip(&tk, TK_EQ)) {
            node = new_binary(ND_EQ, node, relational(&tk));
            continue;
        }
        if (skip(&tk, TK_NOEQ)) {
            node = new_binary(ND_NE, node, relational(&tk));
            continue;
        }
        *mark = tk;
        return node;
    }
}

Node *relational(Token **mark)
{
    Token *tk = *mark;

    Node *node = add(&tk);
    for (;;) {
        if (skip(&tk, '<')) {
            node = new_binary(ND_LT, node, add(&tk));
            continue;
        }
        if (skip(&tk, '>')) {
            node = new_binary(ND_LT, add(&tk), node);
            continue;
        }
        if (skip(&tk, TK_LTEQ)) {
            node = new_binary(ND_LTE, node, add(&tk));
            continue;
        }
        if (skip(&tk, TK_GREQ)) {
            node = new_binary(ND_LTE, add(&tk), node);
            continue;
        }
        *mark = tk;
        return node;
    }
}

Node *add(Token **mark)
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

Node *mul(Token **mark)
{
    Token *tk = *mark;

    Node *node = unary(&tk);
    for (;;) {
        if (skip(&tk, '*')) {
            node = new_binary(ND_MUL, node, unary(&tk));
            continue;
        }
        if (skip(&tk, '/')) {
            node = new_binary(ND_DIV, node, unary(&tk));
            continue;
        }
        *mark = tk;
        return node;
    }
}

Node *unary(Token **mark)
{
    Node *node;
    Token *tk = *mark;

    if (skip(&tk, '+')) {
        node = unary(&tk);
    } else if (skip(&tk, '-')) {
        node = new_unary(ND_NEG, unary(&tk));
    } else {
        node = primary(&tk);
    }

    *mark = tk;
    return node;
}

Node *primary(Token **mark)
{
    Node *node;
    Token *tk = *mark;

    if (tk->kind == TK_NUM) {
        node = new_num(tk);
        node->tok = tk;
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


static int depth;

static void push(void)
{
    printf("  push\t%%rax\n");
    depth++;
}

static void pop(char *arg)
{
    printf("  pop\t%s\n", arg);
    depth--;
}

static void gen_expr(Node *node)
{
    if (node->kind == ND_NUM) {
        printf("  mov\t$%d, %%rax\n", node->val);
        return;
    } else if (node->kind == ND_NEG) {
        gen_expr(node->lhs);
        printf("  neg\t%%rax\n");
        return;
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");

    switch (node->kind) {
    case ND_ADD:
        printf("  add\t%%rdi, %%rax\n");
        return;
    case ND_SUB:
        printf("  sub\t%%rdi, %%rax\n");
        return;
    case ND_MUL:
        printf("  imul\t%%rdi, %%rax\n");
        return;
    case ND_DIV:
        printf("  cqo\n");
        printf("  idiv\t%%rdi\n");
        return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LTE:
        printf("  cmp\t%%rdi, %%rax\n");
        if (node->kind == ND_EQ) {
            printf("  sete\t%%al\n");
        } else if (node->kind == ND_NE) {
            printf("  setne\t%%al\n");
        } else if (node->kind == ND_LT) {
            printf("  setl\t%%al\n");
        } else if (node->kind == ND_LTE) {
            printf("  setle\t%%al\n");
        }
        printf("  movzb\t%%al, %%rax\n");
        return;
    default:
        error("invalid expression");
    }
}

int main(int argc, char *argv[]) {

    if (argc != 2) error("%s: invalid number of arguments!", argv[0]);

    Token *tk = tokenize(&(File){ .str = argv[1], .len = strlen(argv[1]), .path = "argv[1]" });

    Node *node = expr(&tk);
    expect(tk, TK_EOF);

    printf(".global main\n");
    printf("main:\n");

    print_tree_root(node, "  // ");
    gen_expr(node);

    assert(depth == 0);

    printf("  ret\n");

    return 0;
}
