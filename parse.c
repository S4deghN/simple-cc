#include "cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

const int MIN_PREC = -1;

Obj empty;

typedef struct Scope Scope;
struct Scope {
    Scope *next;
    Obj *objs;
};

Scope *const global = &(Scope){0};
Scope *scope = global;

Scope *fn_scope = &(Scope){0};

static void
enter_scope()
{
    Scope *sc = calloc(1, sizeof(*sc));
    sc->next = scope;
    scope = sc;
}

static void
leave_scope()
{
    // Save objects of the leaving scope to fn_scope.
    Obj *obj_start = scope->objs;
    if (obj_start) {
        Obj *obj_end = obj_start;
        for (; obj_end->next; obj_end = obj_end->next);
        obj_end->next = fn_scope->objs;
        fn_scope->objs = obj_start;
    }

    scope = scope->next;
}

static bool
is_local_scope(Scope *sc)
{
    return sc->next != NULL;
}

static void
insert_scope_obj(Scope *sc, Obj *obj)
{
    obj->next = sc->objs;
    sc->objs = obj;
}

static Obj *
find_obj_this_scope(Token *tok, Scope *sc)
{
    for (Obj *obj = sc->objs; obj; obj = obj->next) {
        if (obj->tok->len == tok->len &&
            strncmp(obj->tok->str,  tok->str, tok->len) == 0) {
            return obj;
        }
    }
    return NULL;
}

static Obj *
find_obj(Token *tok)
{
    for (Scope *sc = scope; sc; sc = sc->next) {
        Obj *obj = find_obj_this_scope(tok, sc);
        if (obj) return obj;
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
    node->val = get_number(tok);
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

static Obj *
new_obj(Type *ty, Scope *scope)
{
    Obj *obj;
    bool prototype_existed = false;

    if ((obj = find_obj_this_scope(ty->id_name, scope))) {
        if (obj->is_function && !obj->body && func_type_match(ty, obj->ty)) {
            // Use the found obj
            prototype_existed = true;
        } else if (obj->tok->kind == TK_STR) {
            return obj;
        } else {
            error_tok(ty->id_name, "Redefinition of identifier");
        }
    } else {
        obj = calloc(1, sizeof(*obj));
    }

    obj->tok = ty->id_name;
    obj->ty = ty;
    if (ty->kind == TY_FUNC) {
        obj->is_function = true;
        obj->parameters_count = ty->param_count;
    }

    obj->is_local = is_local_scope(scope);

    if (!prototype_existed) { // We already had it on the list so don't make a circle!
        insert_scope_obj(scope, obj);
    }

    return obj;
}

// Always global
Obj *
new_string_obj(Token *tok)
{
    assert(tok->kind == TK_STR);

    Type *ty = array_of(ty_char, tok->str_data.len); // +1 for null.
    ty->id_name = tok;

    Obj *obj = new_obj(ty, global);
    obj->init_data = tok->str_data.data;

    return obj;
}
static Node *
obj_node(Token *tok)
{
    Obj *obj = find_obj(tok);
    if (!obj) error_tok(tok, "Undefined identifier");

    Node *node = new_node(obj->is_function ? ND_FUNCALL : ND_VAR, tok);
    node->obj = obj;
    node->ty = obj->ty;

    return node;
}

// Overloads '+' for pointer and array arithmetic.
static Node *
new_add(Node *left, Node *right, Token *tok) {
    add_type(left);
    add_type(right);

    // int + int
    if (!left->ty->base && !right->ty->base) return new_binary(ND_ADD, left, right, tok);

    // ptr + ptr
    if (left->ty->base && right->ty->base) error_tok(tok, "Invalid pointer-pointer addition");

    // int + ptr
    if (right->ty->base) swap(left, right);

    // ptr + int
    right = new_binary(ND_MUL, right, new_implicit_num(tok, left->ty->base->size), tok);
    return new_binary(ND_ADD, left, right, tok);

}

// Overloads '-' for pointer and array arithmetic.
static Node *
new_sub(Node *left, Node *right, Token *tok) {
    add_type(left);
    add_type(right);

    // ptr - ptr
    if (left->ty->base && right->ty->base) {
        Node *node = new_binary(ND_SUB, left, right, tok);
        node->ty = ty_int;
        return new_binary(ND_DIV, node, new_implicit_num(tok, left->ty->base->size), tok);
    }

    // int - ptr
    if (right->ty->base) error_tok(tok, "Invalid int-pointer subtraction");

    // ptr - int
    if (left->ty->base) {
        right = new_binary(ND_MUL, right, new_implicit_num(tok, left->ty->base->size), tok);
        add_type(right);
        Node *node = new_binary(ND_SUB, left, right, tok);
        node->ty = left->ty;
        return node;
    }

    // int - int
    return new_binary(ND_SUB, left, right, tok);
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

static Token *
skip(Token **tok, TokenKind kind)
{
    Token *tk = *tok;
    if (tk->kind == kind) {
        *tok = tk->next;
        return tk;
    }
    return NULL;
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


static Token *
expect_skip(Token **tok, TokenKind kind)
{
    Token *tk = skip(tok, kind);
    if (!tk) {
        error_tok(*tok, "Expected '%s', got '%s'", tk_kind_str(kind), tk_kind_str((*tok)->kind));
    }
    return tk;
}

static bool
tok_match(Token *tok, char *name)
{
    return strlen(name) == (size_t)tok->len && strncmp(name, tok->str, tok->len) == 0;
}

static Token *
skip_id(Token **tok, char *name)
{
    Token *tk = *tok;

    if (tk->kind != TK_ID) return NULL;
    if (tok_match(tk, name)) {
        *tok = tk->next;
        return tk;
    }
    return NULL;
}

static Token *
expect_skip_id(Token **tok, char *name)
{
    Token *tk = skip_id(tok, name);
    if (!tk) {
        error_tok(*tok, "Expected '%s', got '%s'", name, tk_kind_str((*tok)->kind));
    }
    return tk;
}

static Token *
skip_kw(Token **tok, char *name)
{
    Token *tk = *tok;

    if (tk->kind != TK_KEYWORD) return NULL;
    if (tok_match(tk, name)) {
        *tok = tk->next;
        return tk;
    }
    return NULL;
}

static Token *
expect_skip_kw(Token **tok, char *name)
{
    Token *tk = skip_kw(tok, name);
    if (!tk) {
        error_tok(*tok, "Expected '%s', got '%s'", name, tk_kind_str((*tok)->kind));
    }
    return tk;
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

static Node *parse_leaf(Token **tok);
static Node *parse_expr(Token **tok, int min_prec);
static Node *parse_statement(Token **tok);

// funcall = (expr ("," expr)*)?
static Node *
funcall(Token **tok, Token *mark)
{
    Node *node = obj_node(mark);

    Node head = {0};
    Node *args = &head;
    for (int i = 0; !skip(tok, ')'); ++i) {
        if (i) expect_skip(tok, ',');
        args = args->next = parse_expr(tok, MIN_PREC);
        add_type(args);
    }
    node->args = head.next;

    return node;
}

static Node *
parse_right_unary(Token **tok, Node *left) {
    Token *mark = *tok;
    if (mark->kind == '[') {
        *tok = mark->next;
        Node *right = parse_expr(tok, MIN_PREC);
        expect_skip(tok, ']');
        left = new_unary(ND_DEREF, new_add(left, right, mark), mark);
    }
    return left;
}

// right leaning tree builder
static Node *
parse_increasing_recedence(Token **tok, Node *left, int min_prec)
{
    Token *mark = *tok;

    int next_prec = get_precedence(mark); // Returns lowest precedence on any non binary operation token so that we stop.

    // special case for '=' we need a right leaning tree in case of consecutive assignments
    if (next_prec + (mark->kind == '=') <= min_prec) return parse_right_unary(tok, left);

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
    Node *node;

    // var
    if (skip(tok, TK_ID)) {
        if (skip(tok, '(')) {
            return funcall(tok, mark);
        }
        return obj_node(mark);
    }
    // num
    if (skip(tok, TK_NUM)) return new_num(mark);
    // str
    if (skip(tok, TK_STR)) {
        new_string_obj(mark);
        return obj_node(mark);
    }
    // paren
    if (skip(tok, '(')) {
        if ((*tok)->kind == '{') { // statement-expression
            node = parse_statement(tok); // will parse a compound with body
            node->kind = ND_STMT_EXPR;
        } else { // expression-statement
            node = parse_expr(tok, MIN_PREC);
        }
        expect_skip(tok, ')');
        return node;
    }
    // left unary operator
    switch (mark->kind) {
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
        node = parse_leaf(tok);
        switch (node->kind) {
            case ND_VAR: case ND_DEREF:
                break;
            default: error_tok(mark, "Lvalue is required as unary & operand!");
        }
        return new_unary(ND_ADDR, node, mark);
    default:
        // fallthrough
    }

    if (skip_kw(tok, "sizeof")) {
        node = parse_leaf(tok);
        add_type(node);
        return new_implicit_num(mark, node->ty->size);
    }

    error_tok(mark, "Expected a primary token got '%s'!", tk_kind_str(mark->kind));
    abort();
}

static bool
is_typename(Token *tok)
{
    return tok_match(tok, "char") || tok_match(tok, "int");
}

static Type *
parse_base_type(Token **tok)
{
    Token *name = expect_skip(tok, TK_KEYWORD); // @Temporary, must expect typename.
    Type *ty;

    if (tok_match(name, "char")) {
        ty = copy_type(ty_char);
    } else if (tok_match(name, "int")) {
        ty = copy_type(ty_int);
    } else {
        error_tok(name, "Not a supported type name!");
    }

    ty->ty_name = name;

    return ty;
}

static Type *
parse_id_declarator(Token **tok, Type *ty) // `ty` will be modified.
{
    // Be aware that right now we are using this function recursively to parse a function declerator and also its parameters. So one could pass a function declarator as a function declaration as parameter which is not defined in C and we do not support it either but for the sake of simplicity we do not care right now.
    while (skip(tok, '*')) ty = pointer_to(ty);

    if (skip(tok, '(')) error_tok(*tok, "Function pointer is not supported!");

    ty->id_name = expect_skip(tok, TK_ID);

    if (skip(tok, '(')) {
        ty->ret_ty = copy_type(ty);
        ty->kind = TY_FUNC;

        Type head = {0};
        Type *param = &head;
        int param_count = 0;
        for (Type *tmp; !skip(tok, ')'); ++param_count) {
            if (param_count) expect_skip(tok, ',');

            tmp = parse_base_type(tok);

            while (skip(tok, '*')) tmp = pointer_to(tmp);

            if ((*tok)->kind == TK_ID) {
                tmp = parse_id_declarator(tok, tmp);
            }

            param = param->next = tmp;
        }
        ty->params = head.next;
        ty->param_count = param_count;
        return ty;
    }

    if (skip(tok, '[')) {
        // We have to build the array type backward!
        // We could also use recursion but I don't like it.
        int i = 0;
        int len_stack[32];
        do {
            len_stack[i++] = get_number(expect_skip(tok, TK_NUM));
            expect_skip(tok, ']');
        }
        while (skip(tok, '['));
        while (i--) ty = array_of(ty, len_stack[i]);
    }


    return ty;
}

static Node *
parse_var_declaration(Token **tok, Type *base_ty)
{
    Token *mark;

    Node head = {0};
    Node *body = &head;
    for (int i = 0; !skip(tok, ';'); ++i) {
        if (i) expect_skip(tok, ',');

        Type *ty = copy_type(base_ty);
        ty = parse_id_declarator(tok, ty);
        // if (ty->kind == TY_FUNC) error_tok(ty->id_name, "Nested function declaration is not supported!");

        Obj *obj = new_obj(ty, scope); // TODO: Refactor and use in global scope too.
        obj->is_local = 1;

        mark = *tok;
        if (skip(tok, '=')) {
            Node *left = obj_node(obj->tok);
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

    // declaration | defenition
    if (is_typename(*tok)) {
        Type *base_ty = parse_base_type(tok);
        return parse_var_declaration(tok, base_ty);
    }

    // compound statement { ... }
    if (skip(tok, '{')) {
        enter_scope();
        Node head = {0};
        node = &head;
        while (!skip(tok, '}')) {
            node = node->next = parse_statement(tok);
        }
        node = new_node(ND_BLOCK, mark);
        node->body = head.next;
        leave_scope();
        return node;
    }

    // return statement
    if (skip_kw(tok, "return")) {
        node = new_unary(ND_RETURN, parse_expr(tok, MIN_PREC), mark);
        expect_skip(tok, ';');
        return node;
    }

    // "if" statement
    if (skip_kw(tok, "if")) {
        node = new_node(ND_IF, mark);
        expect_skip(tok, '(');
        node->cond = parse_expr(tok, MIN_PREC);
        expect_skip(tok, ')');
        node->then = parse_statement(tok);
        if (skip_kw(tok, "else")) node->els = parse_statement(tok);
        return node;
    }

    // "for" statement
    if (skip_kw(tok, "for")) {
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
    if (skip_kw(tok, "while")) {
        node = new_node(ND_FOR, mark);
        expect_skip(tok, '(');
        node->cond = parse_expr(tok, MIN_PREC);
        expect_skip(tok, ')');
        node->then = parse_statement(tok);
        return node;
    }

    // "do-while" statement
    if (skip_kw(tok, "do")) {
        node = new_node(ND_DO, mark);
        node->then = parse_statement(tok);
        expect_skip_kw(tok, "while");
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

static void
parse_function_body(Token **tok, Obj *obj)
{
    fn_scope->objs = NULL;
    enter_scope();

    // Allocate parameters in local scope.
    for (Type *param = obj->ty->params; param; param = param->next) {
        new_obj(param, scope);
    }
    obj->parameters = scope->objs; // Right now points to the last function parameter

    Node head = {0};
    Node *body = &head;
    while (!skip(tok, '}')) {
        body = body->next = parse_statement(tok);
        add_type(body);
        if ((*tok)->kind == TK_EOF) error_tok(*tok, "Expected '}' at end of function defenition!");
    }
    body = new_node(ND_BLOCK, obj->tok);
    body->body = head.next;
    obj->body = body;

    leave_scope();
    obj->locals = fn_scope->objs;
}

//
// Generates Objs and puts them in the globals list
// They may or may not contain as their body a code block (ND_BLOCK).
//
static void
parse_global(Token **tok)
{
    Type *ty = parse_base_type(tok);

    // e.g. int;
    if (skip(tok, ';')) return;

    for (int i = 0; !skip(tok, ';'); ++i) {
        if (i) expect_skip(tok, ',');

        ty = parse_id_declarator(tok, ty);

        Obj *obj = new_obj(ty, global); // Take the return value in case we want to set a body for it down this path.

        if (i == 0 && obj->is_function && skip(tok, '{')) {
            parse_function_body(tok, obj); // Will assign body of the object!
            return;
        }

        if (!obj->is_function && skip(tok, '=')) {
            assert(0 && "Not supported yet!");
            // parse_decl_assignmen();
            continue;
        }
    }

}

Obj *
parse(Token *tok)
{
    while (tok->kind != TK_EOF) {
        parse_global(&tok);
    }

    return global->objs;
}
