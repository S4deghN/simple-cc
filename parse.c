#include "cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

const int MIN_PREC = -1;

Var *locals;

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
        case ND_GT:  return "GT";
        case ND_GTE: return "GTE";
        case ND_EQ:  return "EQ";
        case ND_NE:  return "NE";
        case ND_VAR: return "VAR";
        case ND_ASSIGN: return "ASSIGN";
        case ND_RETURN: return "RETURN";
        case ND_EXPR_STMT:  return "EXPR_STMT";
        case ND_BLOCK:  return "BLOCK";
        case ND_IF:  return "IF";
        case ND_FOR:  return "FOR";
        case ND_DO:  return "DO";
        case ND_ADDR:  return "ADDR";
        case ND_DEREF:  return "DEREF";
        case ND_FUNCALL:  return "FUNCALL";
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
_print_tree(const Node *node, char *prefix_buff, int prefix_cursor, int is_right, char *alt_root_name, char *wrap)
{
    if (!node) return;

    char *add;
    char *fmt;
    char *wrap_fmt;
    if (is_right == -1) { // root
        fmt = "%.*s%.*s: %s\n";
        wrap_fmt = "%.*s%s(%.*s): %s\n";
        add = "";
    } else if (is_right) { // right branch
        fmt = "%.*s├─ %.*s: %s\n";
        wrap_fmt = "%.*s├─ %s(%.*s): %s\n";
        add = "│  ";
    } else { // left branch
        fmt = "%.*s└─ %.*s: %s\n";
        wrap_fmt = "%.*s└─ %s(%.*s): %s\n";
        add = "   ";
    }

    char *node_str;
    int node_str_len;
    char *type_str = node->ty ? ty_kind_str(node->ty->kind) : "ø";
    if (node->kind == ND_NUM || node->kind == ND_VAR || node->kind == ND_FUNCALL) {
        node_str = node->tok->str;
        node_str_len = node->tok->len;
    } else if (alt_root_name) {
        node_str = alt_root_name;
        node_str_len = strlen(alt_root_name);
    } else {
        node_str = nd_kind_str(node->kind);
        node_str_len = strlen(node_str);
    }

    if (wrap)
        printf(wrap_fmt, prefix_cursor, prefix_buff, wrap, node_str_len, node_str, type_str);
    else
        printf(fmt, prefix_cursor, prefix_buff, node_str_len, node_str, type_str);

    memcpy(&prefix_buff[prefix_cursor], add, strlen(add));
    prefix_cursor += strlen(add);

    switch (node->kind) {
    case ND_IF:
        _print_tree(node->cond, prefix_buff, prefix_cursor, 1, NULL, "COND");
        _print_tree(node->then, prefix_buff, prefix_cursor, 1, NULL, "THEN");
        _print_tree(node->els,  prefix_buff, prefix_cursor, 1, NULL, "ELSE");
        break;
    case ND_FOR:
        if (node->init) _print_tree(node->init, prefix_buff, prefix_cursor, 1, NULL, "INIT");
        if (node->cond) _print_tree(node->cond, prefix_buff, prefix_cursor, 1, NULL, "COND");
        if (node->iter) _print_tree(node->iter, prefix_buff, prefix_cursor, 1, NULL, "ITER");
        if (node->body) _print_tree(node->body, prefix_buff, prefix_cursor, 1, NULL, "BODY");
        break;
    case ND_BLOCK:
        for (Node *n = node->body; n; n = n->next) {
            _print_tree(n, prefix_buff, prefix_cursor, 1, NULL, NULL);
        }
        break;
    case ND_FUNCALL:
        for (Node *n = node->args; n; n = n->next) {
            _print_tree(n, prefix_buff, prefix_cursor, 1, NULL, NULL);
        }
        break;
    default:
        // Recurse: right then left so right appears above left
        _print_tree(node->rhs, prefix_buff, prefix_cursor, 1, NULL, NULL);
        _print_tree(node->lhs, prefix_buff, prefix_cursor, 0, NULL, NULL);
    }
}

void
print_tree(const Node *root, char *prefix)
{
    // char *line;
    // int line_len;
    // get_stmt_str(root->tok, &line, &line_len);
    // printf("%s%.*s\n", prefix, line_len, line);

    char prefix_buff[256]; // must be big enough or segfault
    memcpy(prefix_buff, prefix, strlen(prefix));

    _print_tree(root, prefix_buff, strlen(prefix), -1, NULL, NULL);
}

static Var *
find_var(Token *tok)
{
    for (Var *var = locals; var; var = var->next) {
        if (var->tok->len == tok->len &&
            strncmp(var->tok->str,  tok->str, tok->len) == 0) {
            return var;
        }
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
    node->ty = ty_int;
    return node;
}

static Node *
new_implicit_num(Token *tok, int val)
{
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    return node;
}

static Var *
new_var(Token *tok, Type *ty)
{
    if (find_var(tok)) error_tok(tok, "redefinition of var");

    Var *var = calloc(1, sizeof(*var));
    var->tok = tok;
    var->ty = ty;

    var->next = locals;
    locals = var;

    return var;
}

static Node *
new_var_node(Token *tok, Type *ty)
{
    Node *node = new_node(ND_VAR, tok);
    node->var = new_var(tok, ty);
    node->ty = ty;
    return node;
}

static Node *
var_node(Token *tok, Var *var)
{
    Node *node;

    if (!var) {
        var = find_var(tok);
        if (!var) error_tok(tok, "undefined variable");
        node = new_node(ND_VAR, tok);
    } else {
        node = new_node(ND_VAR, var->tok);
    }

    node->var = var;
    node->ty = var->ty;

    return node;
}

// Overloads '+' for pointer arithmetic.
static Node *
new_add(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    switch (double_case(lhs->ty->kind, rhs->ty->kind)) {
    case double_case(TY_INT, TY_INT):
        return new_binary(ND_ADD, lhs, rhs, tok);
    case double_case(TY_INT, TY_PTR):
        swap(lhs, rhs);
        // fallthrough
    case double_case(TY_PTR, TY_INT):
        // printf("confirm we are here!\n");
        rhs = new_binary(ND_MUL, rhs, new_implicit_num(tok, 8), tok);
        return new_binary(ND_ADD, lhs, rhs, tok);
    case double_case(TY_PTR, TY_PTR):
        // TODO: Add type printing and better diagnostics.
        error_tok(tok, "Invalid operation");
    }

    return NULL;
}

// Overloads '-' for pointer arithmetic.
static Node *
new_sub(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    switch (double_case(lhs->ty->kind, rhs->ty->kind)) {
    case double_case(TY_INT, TY_INT):
        return new_binary(ND_SUB, lhs, rhs, tok);
    case double_case(TY_PTR, TY_PTR): {
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        node->ty = ty_int;
        return new_binary(ND_DIV, node, new_implicit_num(tok, 8), tok);
    }
    case double_case(TY_PTR, TY_INT): {
        rhs = new_binary(ND_MUL, rhs, new_implicit_num(tok, 8), tok);
        add_type(rhs);
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        node->ty = lhs->ty;
        return node;
    }
    case double_case(TY_INT, TY_PTR):
        // TODO: Add type printing and better diagnostics.
        error_tok(tok, "Invalid operation");
    }

    return NULL;
}

static Node *
new_binary_with_checks(NodeKind kind, Node *left, Node *right, Token *tok)
{
    switch (kind) {
        case ND_ADD: return new_add(left, right, tok);
        case ND_SUB: return new_sub(left, right, tok);
        default:     return new_binary(kind, left, right, tok);
    }
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
        *tok = tk->next;
        return true;
    }
    return false;
}

static bool
skip_class(Token **tok, bool (*is_kind_class)(TokenKind kind))
{
    Token *tk = *tok;
    if (is_kind_class(tk->kind)) {
        *tok = tk->next;
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

static bool
tok_match(Token *tok, char *name)
{
    return strlen(name) == (size_t)tok->len && strncmp(name, tok->str, tok->len) == 0;
}

static bool
skip_id(Token **tok, char *name)
{
    Token *tk = *tok;

    if (tk->kind != TK_ID) return false;
    if (tok_match(tk, name)) {
        tk = tk->next;
        *tok = tk;
        return true;
    }
    return false;
}

static void
expect_skip_id(Token **tok, char *name)
{
    if (!skip_id(tok, name)) {
        error_tok(*tok, "Expected '%s', got '%s'", name, tk_kind_str((*tok)->kind));
    }
}

static int
get_precedence(Token *tok)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
    switch (tok->kind) {
    case '*':
    case '/':
        return 100;
    case '+':
    case '-':
        return 50;
    case '<':
    case '>':
    case TK_GREQ:
    case TK_LTEQ:
        return 25;
    case TK_EQ:
    case TK_NOEQ:
        return 12;
    case '=':
        return 6;
    default:
        return MIN_PREC-1;
    }
#pragma GCC diagnostic pop
}

NodeKind
tok_to_binary_op(Token *tok)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
    switch (tok->kind) {
    case '+':     return ND_ADD;
    case '-':     return ND_SUB;
    case '*':     return ND_MUL;
    case '/':     return ND_DIV;
    case '<':     return ND_LT;
    case '>':     return ND_GT;
    case '=':     return ND_ASSIGN;
    case TK_GREQ: return ND_GTE;
    case TK_LTEQ: return ND_LTE;
    case TK_EQ:   return ND_EQ;
    case TK_NOEQ: return ND_NE;
    default:
        error_tok(tok, "Not a binary operator");
        return -1;
    }
#pragma GCC diagnostic pop
}

static Node *parse_expr(Token **tok, int min_prec);
static Node *parse_leaf(Token **tok);

// funcall = (expr ("," expr)*)?
static Node *
funcall(Token **tok, Token *mark)
{
    Node head = {0};
    Node *node = &head;

    for (int i = 0; !skip(tok, ')'); ++i) {
        if (i) expect_skip(tok, ',');
        node = node->next = parse_expr(tok, MIN_PREC);
    }

    node = new_node(ND_FUNCALL, mark);
    node->args = head.next;
    return node;
}

// right leaning tree builder
static Node *
parse_increasing_recedence(Token **tok, Node *left, int min_prec)
{
    Token *mark = *tok;

    int next_prec = get_precedence(mark); // Returns lowest precedence on any non binary operation token so that we stop.
    if (next_prec < min_prec) return left;

    (*tok) = (*tok)->next;

    Node *right = parse_expr(tok, next_prec);
    return new_binary_with_checks(tok_to_binary_op(mark), left, right, mark);
}

// left leaning tree builder
static Node *
parse_expr(Token **tok, int min_prece)
{
    Node *node;
    Node *left = parse_leaf(tok);

    for (;;) {
        node = parse_increasing_recedence(tok, left, min_prece);
        if (node == left) break;

        left = node;
    }

    return node;
}

static Node *
parse_leaf(Token **tok)
{
    Token *mark = *tok;

    // var
    if (skip(tok, TK_ID)) {
        if (skip(tok, '(')) { // TEMPORARY: must handle both function and var symbols in the same space and then try to parse "(" ... ")" to see if it's a function call and set appropriate flags and stuff ...
            return funcall(tok, mark);
        }
        return var_node(mark, NULL);
    }
    // num
    if (skip(tok, TK_NUM)) return new_num(mark);
    // paren
    if (skip(tok, '(')) {
        Node *node = parse_expr(tok, MIN_PREC);
        expect_skip(tok, ')');
        return node;
    }
    // unary unary operator
    switch (mark->kind) { // TODO: Extract to a function.
        case '+':
            *tok = mark->next;
            return parse_leaf(tok);
        case '-':
            *tok = mark->next;
            return new_unary(ND_NEG, parse_leaf(tok), mark);
        case '*':
            *tok = mark->next;
            return new_unary(ND_DEREF, parse_leaf(tok), mark);
        case '&':
            *tok = mark->next;
            Node *left = parse_leaf(tok);
            switch (left->kind) {
                case ND_VAR: case ND_DEREF:
                    break;
                default: error_tok(mark, "Lvalue is required as unary & operand!");
            }
            return new_unary(ND_ADDR, left, mark);
        default:
            // fallthrough
    }

    error_tok(mark, "Expected a primary token got '%s'!", tk_kind_str(mark->kind));
    abort();
}

static Type *
parse_id_declarator(Token **tok)
{
    expect_skip_id(tok, "int");
    Type *ty = ty_int;
    while (skip(tok, '*')) ty = pointer_to(ty);
    return ty;
}

static Node *
parse_var_declaration(Token **tok)
{
    Node head = {0};
    Node *body = &head;

    Token *mark;

    Type *type = parse_id_declarator(tok);

    for (int i = 0; !skip(tok, ';'); ++i) {
        if (i) expect_skip(tok, ',');

        Type *ty = type;
        while (skip(tok, '*')) ty = pointer_to(ty);

        mark = *tok;
        expect_skip(tok, TK_ID);
        Var *var = new_var(mark, ty);

        mark = *tok;
        if (skip(tok, '=')) {
            Node *left = var_node(NULL, var);
            Node *right = parse_expr(tok, MIN_PREC);
            Node *binary = new_binary(ND_ASSIGN, left, right, mark);
            body = body->next = new_unary(ND_EXPR_STMT, binary, *tok);
        }
    } // skiped ';'
    Node *node = new_node(ND_BLOCK, mark); // NOTE: this node's tok is incorrect, but I don't care about tok for a ND_BLOCK.
    node->body = head.next;
    return node;
}

static Node *
parse_expr_statement(Token **tok)
{
    Token *mark = *tok;

    // empty statement
    if (skip(tok, ';')) return new_node(ND_BLOCK, mark);

    Node *node = parse_expr(tok, MIN_PREC);
    expect_skip(tok, ';');
    node = new_unary(ND_EXPR_STMT, node, mark);
    return node;
}

static Node *
parse_statement(Token **tok)
{
    Node *node;
    Token *mark = *tok;

    // block statement { ... }
    if (skip(tok, '{')) {
        Node head = {0};
        node = &head;
        while (!skip(tok, '}')) {
            node = node->next = parse_statement(tok);
        }
        node = new_node(ND_BLOCK, mark);
        node->body = head.next;
        return node;
    }

    // return statement
    if (skip_id(tok, "return")) {
        node = new_unary(ND_RETURN, parse_expr(tok, MIN_PREC), mark);
        expect_skip(tok, ';');
        return node;
    }

    // declaration | defenition
    if (tok_match(*tok, "int")) return parse_var_declaration(tok);

    // "if" statement
    if (skip_id(tok, "if")) {
        node = new_node(ND_IF, mark);
        expect_skip(tok, '(');
        node->cond = parse_expr(tok, MIN_PREC);
        expect_skip(tok, ')');
        node->then = parse_statement(tok);
        if (skip_id(tok, "else")) node->els = parse_statement(tok);
        return node;
    }

    // "for" statement
    if (skip_id(tok, "for")) {
        node = new_node(ND_FOR, mark);
        expect_skip(tok, '(');
        node->init = parse_expr_statement(tok);
        if (!skip(tok, ';')) {
            node->cond = parse_expr(tok, MIN_PREC);
            expect_skip(tok, ';');
        }
        if (!skip(tok, ')')) {
            node->iter = parse_expr(tok, MIN_PREC);
            expect_skip(tok, ')');
        }
        node->then = parse_statement(tok);
        return node;
    }

    // "while" statement
    if (skip_id(tok, "while")) {
        node = new_node(ND_FOR, mark);
        expect_skip(tok, '(');
        node->cond = parse_expr(tok, MIN_PREC);
        expect_skip(tok, ')');
        node->then = parse_statement(tok);
        return node;
    }

    // "do-while" statement
    if (skip_id(tok, "do")) {
        node = new_node(ND_DO, mark);
        node->then = parse_statement(tok);
        expect_skip_id(tok, "while");
        expect_skip(tok, '(');
        node->cond = parse_expr(tok, MIN_PREC);
        expect_skip(tok, ')');
        expect_skip(tok, ';');
        return node;
    }

    // else:
    // expression statement
    node = parse_expr_statement(tok);

    return node;
}

static Function *
parse_function(Token **tok)
{
    Node head = {0};
    Node *body = &head;

    expect_skip_id(tok, "int");
    Token *func_name = *tok;
    expect_skip(tok, TK_ID);
    expect_skip(tok, '(');

    // id_declarator id (, id_declarator id)
    int parameters_count = 0;
    for (; !skip(tok, ')'); ++parameters_count) {
        if (parameters_count) expect_skip(tok, ',');
        Type *ty = parse_id_declarator(tok);
        Token *var_name = *tok;
        expect_skip(tok, TK_ID);
        new_var(var_name, ty);
    }
    Var *parameters = locals; // right now points to the last function parameter

    // Later on it's going to be like this:
    //
    // var -> var -> var -> var -> var -> var
    //  ^             ^
    // locals      params
    //
    // So they use the same namespace, and we can easily alocated stack for
    // in the expected order by traversing `locals`

    expect_skip(tok, '{');
    while (!skip(tok, '}')) {
        body = body->next = parse_statement(tok);
        add_type(body);
        if ((*tok)->kind == TK_EOF) error_tok(*tok, "Expected '}' at end of function defenition!");
    }
    Node *node = new_node(ND_BLOCK, func_name);
    node->body = head.next;

    Function *func = calloc(1, sizeof(*func));
    func->tok = func_name;
    func->body = node;
    func->parameters = parameters;
    func->parameters_count = parameters_count;
    func->locals = locals;
    locals = NULL; // Reset locals for next function.

    return func;
}

Program *
parse(Token *tok)
{
    Function head = {0};
    Function *func = &head;

    while (tok->kind != TK_EOF) {
        func = func->next = parse_function(&tok);
    }

    Program *prog = calloc(1, sizeof(*prog));
    prog->functions = head.next;

    return prog;
}
