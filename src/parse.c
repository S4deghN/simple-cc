#include "cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

Var *locals;

static Node *compound_stmt(Token **tok, Token *mark);
static Node *expr(Token **tok);
static Node *expr_stmt(Token **tok);
static Node *assign(Token **tok);
static Node *equality(Token **tok);
static Node *relational(Token **tok);
static Node *add(Token **tok);
static Node *mul(Token **tok);
static Node *unary(Token **tok);
static Node *primary(Token **tok);

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
        case ND_RETURN: return "RETURN";
        case ND_EXPR_STMT:  return "EXPR_STMT";
        case ND_BLOCK:  return "BLOCK";
        default: return "???";
    }
}

static void
get_stmt_str(Token *tok, char **str, int *len)
{
    char *line_start;
    if (tok->str == tok->file->str) line_start = tok->str;
    else line_start = str_find_prev(tok->file->str, tok->str - 1, ';'); // Go back one step in case we are on a ';'
    if (*line_start == ';') line_start += 1; // skip previous ';'
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

static Var *
find_var(Token *tok)
{
    for (Var *var = locals; var; var = var->next) {
        if (var->tok->len == tok->len &&
            strncmp(var->tok->str,  tok->str, tok->len) == 0) return var;
    }
    return NULL;
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
new_var_node(Token *tok)
{
    Var *var = find_var(tok);
    if (!var) {
        var = calloc(1, sizeof(*var));
        var->tok = tok;
        var->next = locals;
        locals = var;
    }
    Node *node = new_node(ND_VAR, tok);
    node->var = var;
    return node;
}

void
expect_node(Node *node, NodeKind kind)
{
    if (node->kind != kind)
        error_tok(node->tok, "Expected '%s', got '%s'", nd_kind_str(kind), nd_kind_str(node->kind));
}


void
expect_node_many(Node *node, int n, ...)
{
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; ++i) {
        if (node->kind == va_arg(ap, NodeKind)) {
            va_end(ap);
            return;
        }
    }

    diag_tok(node->tok, "Expected ");

    va_start(ap, n);
    fprintf(stderr, "'%s'", nd_kind_str(va_arg(ap, NodeKind)));
    for (int i = 1; i < n; ++i) {
        fprintf(stderr, " or '%s'", nd_kind_str(va_arg(ap, NodeKind)));
    }
    fprintf(stderr, ", got '%s'\n", nd_kind_str(node->kind));
    error("");
}


static void
expect(Token *tok, TokenKind kind)
{
    if (tok->kind != kind)
        error_tok(tok, "Expected '%s', got '%s'", tk_kind_str(kind), tk_kind_str(tok->kind));
}

static bool
skip(Token **tok, TokenKind kind)
{
    Token *tk = *tok;
    if (tk->kind == kind) {
        tk = tk->next;
        *tok = tk;
        return true;
    }
    return false;
}

static bool
skip_id(Token **tok, char *name)
{
    Token *tk = *tok;

    if (tk->kind != TK_ID) return false;
    if (strlen(name) == (size_t)tk->len && strncmp(name, tk->str, tk->len) == 0) {
        tk = tk->next;
        *tok = tk;
        return true;
    }
    return false;
}

static void
expect_skip(Token **tok, TokenKind kind)
{
    if (!skip(tok, kind)) {
        error_tok(*tok, "Expected '%s', got '%s'", tk_kind_str(kind), tk_kind_str((*tok)->kind));
    }
}

// stmt = "return" expr ";"
//      | "{" compound_stmt
//      | expr-stmt
//      | ";"
static Node *
stmt(Token **tok)
{
    Token *mark = *tok;
    Node *node;

    if (skip(tok, ';')) {
        node = new_node(ND_BLOCK, mark);
    } else if (skip(tok, '{')) {
        node = compound_stmt(tok, mark);
    } else if (skip_id(tok, "return")) {
        node = new_unary(ND_RETURN, expr(tok), mark);
        expect_skip(tok, ';');
    } else {
        node = expr_stmt(tok);
    }

    return node;
}

// compound-stmt = stmt* "}"
static Node *
compound_stmt(Token **tok, Token *mark)
{
    Node head;
    Node *node = &head;

    while (!skip(tok, '}'))
        node = node->next = stmt(tok);

    Node *block = new_node(ND_BLOCK, mark);
    block->body = head.next;

    return block;
}

// expr-stmt = expr ";"
static Node *
expr_stmt(Token **tok) {
    Node *node;
    node = expr(tok);
    node = new_unary(ND_EXPR_STMT, node, *tok);
    expect_skip(tok, ';');
    return node;
}

// expr = assign
static Node *
expr(Token **tok)
{
    return assign(tok);
}

// assign = equality (= assign)
static Node *
assign(Token **tok)
{
    Node *node = equality(tok);

    Token *mark = *tok;
    if (skip(tok, '=')) {
        if (node->kind != ND_VAR) error_tok(node->tok, "Not an lvalue!");
        node = new_binary(ND_ASSIGN, node, assign(tok), mark);
    }

    return node;
}

static Node *
equality(Token **tok)
{

    Node *node = relational(tok);

    for (;;) {
        Token *mark = *tok;
        if (skip(tok, TK_EQ)) {
            node = new_binary(ND_EQ, node, relational(tok), mark);
            continue;
        }
        if (skip(tok, TK_NOEQ)) {
            node = new_binary(ND_NE, node, relational(tok), mark);
            continue;
        }
        return node;
    }
}

static Node *
relational(Token **tok)
{
    Node *node = add(tok);
    for (;;) {
        Token *mark = *tok;
        if (skip(tok, '<')) {
            node = new_binary(ND_LT, node, add(tok), mark);
            continue;
        }
        if (skip(tok, '>')) {
            node = new_binary(ND_LT, add(tok), node, mark);
            continue;
        }
        if (skip(tok, TK_LTEQ)) {
            node = new_binary(ND_LTE, node, add(tok), mark);
            continue;
        }
        if (skip(tok, TK_GREQ)) {
            node = new_binary(ND_LTE, add(tok), node, mark);
            continue;
        }
        return node;
    }
}

static Node *
add(Token **tok)
{
    Node *node = mul(tok);
    for (;;) {
        Token *mark = *tok;
        if (skip(tok, '+')) {
            node = new_binary(ND_ADD, node, mul(tok), mark);
            continue;
        }
        if (skip(tok, '-')) {
            node = new_binary(ND_SUB, node, mul(tok), mark);
            continue;
        }
        return node;
    }
}

static Node *
mul(Token **tok)
{
    Node *node = unary(tok);
    for (;;) {
        Token *mark = *tok;
        if (skip(tok, '*')) {
            node = new_binary(ND_MUL, node, unary(tok), mark);
            continue;
        }
        if (skip(tok, '/')) {
            node = new_binary(ND_DIV, node, unary(tok), mark);
            continue;
        }
        return node;
    }
}

static Node *
unary(Token **tok)
{
    Node *node;
    Token *mark = *tok;
    if (skip(tok, '+')) {
        node = unary(tok);
    } else if (skip(tok, '-')) {
        node = new_unary(ND_NEG, unary(tok), mark);
    } else {
        node = primary(tok);
    }

    return node;
}

static Node *
primary(Token **tok)
{
    Node *node;
    Token *mark = *tok;
    if (skip(tok, TK_NUM)) {
        node = new_num(mark);
    } else if (skip(tok, TK_ID)) {
        node = new_var_node(mark);
    } else if (skip(tok, '(')) {
        node = expr(tok);
        expect_skip(tok, ')');
    } else {
        error_tok(mark, "Expected primary token!");
    }

    return node;
}

Function *
parse(Token *tok)
{
    Token *mark = tok;
    expect_skip(&tok, '{');
    Function *func = calloc(1, sizeof(*func));
    func->body = compound_stmt(&tok, mark);
    func->locals = locals;

    return func;
}
