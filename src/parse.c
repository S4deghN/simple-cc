#include "cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static Node *expr(Token **mark);
static Node *expr_stmt(Token **mark);
static Node *assign(Token **mark);
static Node *equality(Token **mark);
static Node *relational(Token **mark);
static Node *add(Token **mark);
static Node *mul(Token **mark);
static Node *unary(Token **mark);
static Node *primary(Token **mark);

char *
nd_kind_str(NodeKind kind)
{
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
        case ND_VAR: return "VAR";
        case ND_ASSIGN: return "ASSIGN";
        case ND_EXPR_STMT:  return "EXPR_STMT";
        default: return "???";
    }
}

static void
get_stmt_str(Token *tok, char **str, int *len)
{
    char *line_start = str_find_prev(tok->file->str, tok->str, ';');
    if (line_start != tok->str) line_start += 1; // skip previous ';'
    char *line_end   = str_find_next(tok->file->str + tok->file->len, tok->str, ';') + 1;

    *str = line_start;
    *len = line_end - line_start;
}


static void
_print_tree(const Node *node, char *prefix_buff, int prefix_cursor, int is_right)
{
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
    if (node->kind == ND_NUM || node->kind == ND_VAR) {
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
    _print_tree(node->rhs, prefix_buff, prefix_cursor, 1);
    _print_tree(node->lhs, prefix_buff, prefix_cursor, 0);
}

void
print_tree(const Node *root, char *prefix)
{
    char prefix_buff[256]; // must be big enough or segfault
    memcpy(prefix_buff, prefix, strlen(prefix));

    char *line;
    int line_len;
    get_stmt_str(root->tok, &line, &line_len);
    printf("%s%.*s\n", prefix, line_len, line);

    _print_tree(root, prefix_buff, strlen(prefix), -1);
}


static Node *
new_node(NodeKind kind, Token *tok)
{
    Node *node = calloc(1, sizeof(*node));
    node->kind = kind;
    node->tok = tok;
    return node;
}

static Node *
new_unary(NodeKind kind, Node *expr, Token *tok)
{
    Node *node = new_node(kind, tok);
    node->lhs = expr;
    return node;
}

static Node *
new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok)
{
    Node *node = new_node(kind, tok);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

static Node *
new_num(Token *tok)
{
    Node *node = new_node(ND_NUM, tok);
    node->val = strtoul(tok->str, NULL, 10);
    return node;
}

static Node *
new_var(Token *tok)
{
    Node *node = new_node(ND_VAR, tok);
    if (tok->len > 1) error_tok(tok, "Var name is too long!");
    node->name = *tok->str;
    return node;
}

void
expect_node(Node *node, NodeKind kind)
{
    if (node->kind != kind)
        error_tok(node->tok, "Expected '%s', got '%s'", nd_kind_str(kind), nd_kind_str(node->kind));
}


static void
expect(Token *tok, TokenKind kind)
{
    if (tok->kind != kind)
        error_tok(tok, "Expected '%s', got '%s'", tk_kind_str(kind), tk_kind_str(tok->kind));
}

static bool
skip(Token **mark, TokenKind kind)
{
    Token *tk = *mark;
    if (tk->kind == kind) {
        tk = tk->next;
        *mark = tk;
        return true;
    }
    return false;
}

static void
expect_skip(Token **mark, TokenKind kind)
{
    if (!skip(mark, kind))
        error_tok(*mark, "Expected '%s', got '%s'", tk_kind_str(kind), tk_kind_str((*mark)->kind));
}

static Node *
stmt(Token **tok)
{
    Token *mark = *tok;
    Node *node;

    if (skip(tok, ';')) {
        node = new_node(ND_EXPR_STMT, mark);
    } else {
        node = expr_stmt(tok);
    }

    return node;
}

static Node *
expr_stmt(Token **mark) {
    Node *node = new_unary(ND_EXPR_STMT, expr(mark), *mark);
    expect_skip(mark, ';');
    return node;
}

static Node *
expr(Token **mark)
{
    return assign(mark);
}

static Node *
assign(Token **mark)
{
    Token *tk = *mark;
    Node *node = equality(&tk);
    if (skip(&tk, '=')) {
        if (node->kind != ND_VAR) error_tok(node->tok, "Not an lvalue!");
        node = new_binary(ND_ASSIGN, node, assign(&tk), tk);
    }
    *mark = tk;
    return node;
}

static Node *
equality(Token **mark)
{
    Token *tk = *mark;

    Node *node = relational(&tk);
    for (;;) {
        if (skip(&tk, TK_EQ)) {
            node = new_binary(ND_EQ, node, relational(&tk), tk);
            continue;
        }
        if (skip(&tk, TK_NOEQ)) {
            node = new_binary(ND_NE, node, relational(&tk), tk);
            continue;
        }
        *mark = tk;
        return node;
    }
}

static Node *
relational(Token **mark)
{
    Token *tk = *mark;

    Node *node = add(&tk);
    for (;;) {
        if (skip(&tk, '<')) {
            node = new_binary(ND_LT, node, add(&tk), tk);
            continue;
        }
        if (skip(&tk, '>')) {
            node = new_binary(ND_LT, add(&tk), node, tk);
            continue;
        }
        if (skip(&tk, TK_LTEQ)) {
            node = new_binary(ND_LTE, node, add(&tk), tk);
            continue;
        }
        if (skip(&tk, TK_GREQ)) {
            node = new_binary(ND_LTE, add(&tk), node, tk);
            continue;
        }
        *mark = tk;
        return node;
    }
}

static Node *
add(Token **mark)
{
    Token *tk = *mark;

    Node *node = mul(&tk);
    for (;;) {
        if (skip(&tk, '+')) {
            node = new_binary(ND_ADD, node, mul(&tk), tk);
            continue;
        }
        if (skip(&tk, '-')) {
            node = new_binary(ND_SUB, node, mul(&tk), tk);
            continue;
        }
        *mark = tk;
        return node;
    }
}

static Node *
mul(Token **mark)
{
    Token *tk = *mark;

    Node *node = unary(&tk);
    for (;;) {
        if (skip(&tk, '*')) {
            node = new_binary(ND_MUL, node, unary(&tk), tk);
            continue;
        }
        if (skip(&tk, '/')) {
            node = new_binary(ND_DIV, node, unary(&tk), tk);
            continue;
        }
        *mark = tk;
        return node;
    }
}

static Node *
unary(Token **mark)
{
    Node *node;
    Token *tk = *mark;

    if (skip(&tk, '+')) {
        node = unary(&tk);
    } else if (skip(&tk, '-')) {
        node = new_unary(ND_NEG, unary(&tk), tk);
    } else {
        node = primary(&tk);
    }

    *mark = tk;
    return node;
}

static Node *
primary(Token **mark)
{
    Node *node;
    Token *tk = *mark;

    if (tk->kind == TK_NUM) {
        node = new_num(tk);
        tk = tk->next;
    } else if (tk->kind == TK_ID) {
        node = new_var(tk);
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

Node *
parse(Token *tok)
{
    Node head = {0};
    Node *node = &head;

    while (tok->kind != TK_EOF)
        node = node->next = stmt(&tok);

    return head.next;
}
