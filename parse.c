#include "cc.h"

const int MIN_PREC = -1;

// ---------------------------------------
// --- Scope and Context ---
// ---------------------------------------

typedef enum {
    SC_NESTED,
    SC_GLOBAL,
    SC_FUNC,
    SC_STRUCT
} ScopeKind;

typedef struct Scope Scope;
struct Scope {
    ScopeKind kind;
    Scope *next;
    Obj *objs;
    Obj *last_obj;
};
Obj *nested_objs;

Scope *const global = &(Scope){ .kind = SC_GLOBAL };
Scope *scope = global;
Scope *named_scope = global;

static void
enter_scope(ScopeKind kind)
{
    if (kind != SC_NESTED) {
        Scope *sc = calloc(1, sizeof(*sc));
        sc->next = named_scope;
        sc->kind = kind;
        named_scope = sc;
    }

    Scope *sc = calloc(1, sizeof(*sc));
    sc->next = scope;
    sc->kind = kind;
    scope = sc;
}

// Returns the obj list of the leaving named scope
static Obj *
leave_scope()
{
    // Save objects of the leaving scope to named scope.
    // Before:
    //      scope:       objs->->->last_obj->NULL
    //      named_scope: objs->->NULL
    //
    // After:
    //      named_scope: objs->->->last_obj->objs->->NULL
    //                   \________________/  \_____/
    //                           V              V
    //                         scope       named_scope
    if (scope->last_obj) {
        scope->last_obj->next = named_scope->objs;
        named_scope->objs = scope->objs;
    }
    Obj *objs = named_scope->objs;


    if (scope->kind != SC_NESTED) named_scope = named_scope->next;

    scope = scope->next;

    return objs;
}

static bool
is_global_scope(Scope *sc)
{
    return sc->next == NULL;
}

static void
insert_scope_obj(Scope *sc, Obj *obj)
{
    bool is_first_insert = !sc->objs;
    if (is_first_insert) sc->last_obj = obj;

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

// ---------------------------------------
// --- Objects ---
// ---------------------------------------

static Obj *
new_obj(Type *ty, Scope *scope)
{
    // Creates a new object and insert it to the current socpe if it doens't exist.

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

    obj->is_local = !is_global_scope(scope);

    if (!prototype_existed) { // We already had it on the list so don't make a circle!
        insert_scope_obj(scope, obj);
    }

    return obj;
}

static Obj *
new_string_obj(Token *tok)
{
    assert(tok->kind == TK_STR);

    Type *ty = array_of(ty_char, tok->str_data.len); // +1 for null.
    ty->id_name = tok;

    Obj *obj = new_obj(ty, global); // Always global
    obj->init_data = tok->str_data.data;

    return obj;
}

// ---------------------------------------
// --- Node creators ---
// ---------------------------------------

static Node *
new_node(NodeKind kind, Token *tok)
{
    Node *node = calloc(1, sizeof(*node));
    node->kind = kind;
    node->tok = tok;
    return node;
}

static Node *
new_unary_node(NodeKind kind, Node *expr, Token *tok)
{
    Node *node = new_node(kind, tok);
    node->lhs = expr;
    return node;
}

static Node *
new_binary_node(NodeKind kind, Node *lhs, Node *rhs, Token *tok)
{
    Node *node = new_node(kind, tok);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

static Node *
new_num_node(Token *tok)
{
    Node *node = new_node(ND_NUM, tok);
    node->val = get_number(tok);
    node->ty = ty_int;
    return node;
}

static Node *
new_implicit_num_node(Token *tok, int val)
{
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    return node;
}

static Node *
new_obj_node(Token *tok)
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
new_add_node(Node *left, Node *right, Token *tok) {
    add_type(left);
    add_type(right);

    // int + int
    if (!left->ty->base && !right->ty->base) return new_binary_node(ND_ADD, left, right, tok);

    // ptr + ptr
    if (left->ty->base && right->ty->base) error_tok(tok, "Invalid pointer-pointer addition");

    // int + ptr
    if (right->ty->base) swap(left, right);

    // ptr + int
    right = new_binary_node(ND_MUL, right, new_implicit_num_node(tok, left->ty->base->size), tok);
    return new_binary_node(ND_ADD, left, right, tok);

}

// Overloads '-' for pointer and array arithmetic.
static Node *
new_sub_node(Node *left, Node *right, Token *tok) {
    add_type(left);
    add_type(right);

    // ptr - ptr
    if (left->ty->base && right->ty->base) {
        Node *node = new_binary_node(ND_SUB, left, right, tok);
        node->ty = ty_int;
        return new_binary_node(ND_DIV, node, new_implicit_num_node(tok, left->ty->base->size), tok);
    }

    // int - ptr
    if (right->ty->base) error_tok(tok, "Invalid int-pointer subtraction");

    // ptr - int
    if (left->ty->base) {
        right = new_binary_node(ND_MUL, right, new_implicit_num_node(tok, left->ty->base->size), tok);
        add_type(right);
        Node *node = new_binary_node(ND_SUB, left, right, tok);
        node->ty = left->ty;
        return node;
    }

    // int - int
    return new_binary_node(ND_SUB, left, right, tok);
}

static Node *
new_binary_with_checks_node(NodeKind kind, Node *left, Node *right, Token *tok)
{
    switch (kind) {
        case ND_ADD: return new_add_node(left, right, tok);
        case ND_SUB: return new_sub_node(left, right, tok);
        default:     return new_binary_node(kind, left, right, tok);
    }
}

// ---------------------------------------
// --- Token handling ---
// ---------------------------------------

static bool
match_str(Token *tok, char *name)
{
    return strlen(name) == (size_t)tok->len && strncmp(name, tok->str, tok->len) == 0;
}

static bool
is_typename(Token *tok)
{
    return tok->kind == TK_KEYWORD && (
        match_str(tok, "struct") ||
        match_str(tok, "char") ||
        match_str(tok, "int")
        );
}

static bool
match(Token **tok, TokenKind kind)
{
    return (*tok)->kind == kind;
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
skip_id(Token **tok, char *name)
{
    Token *tk = *tok;

    if (tk->kind != TK_ID) return NULL;
    if (match_str(tk, name)) {
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
    if (match_str(tk, name)) {
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

// ---------------------------------------
// --- Parsing ---
// ---------------------------------------

static Node *parse_leaf(Token **tok);
static Node *parse_expr(Token **tok, int min_prec);
static Node *parse_funcall(Token **tok, Token *mark);
static Node *parse_statement(Token **tok);
static Node *parse_declaration(Token **tok);

static Node *
parse_leaf(Token **tok)
{
    Token *mark = *tok;
    Node *node;

    // var
    if (skip(tok, TK_ID)) {
        if (skip(tok, '(')) {
            return parse_funcall(tok, mark);
        }
        return new_obj_node(mark);
    }
    // num
    if (skip(tok, TK_NUM)) return new_num_node(mark);
    // str
    if (skip(tok, TK_STR)) {
        new_string_obj(mark);
        return new_obj_node(mark);
    }
    // paren
    if (skip(tok, '(')) {
        if (match(tok, '{')) { // statement-expression
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
        return new_unary_node(ND_NEG, parse_leaf(tok), mark);
    case '*':
        *tok = mark->next;
        return new_unary_node(ND_DEREF, parse_leaf(tok), mark);
    case '&':
        *tok = mark->next;
        node = parse_leaf(tok);
        switch (node->kind) {
            case ND_VAR: case ND_DEREF:
                break;
            default: error_tok(mark, "Lvalue is required as unary & operand!");
        }
        return new_unary_node(ND_ADDR, node, mark);
    default:
        // fallthrough
    }

    if (skip_kw(tok, "sizeof")) {
        node = parse_leaf(tok);
        add_type(node);
        return new_implicit_num_node(mark, node->ty->size);
    }

    error_tok(mark, "Expected a primary token got '%s'!", tk_kind_str(mark->kind));
    abort();
}

static int
get_binary_precedence(TokenKind kind)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
    switch (kind) {
    case '*': case '/':
        return 100;
    case '+': case '-':
        return 50;
    case '<': case '>':
    case TK_GTE: case TK_LTE:
        return 25;
    case TK_EQ: case TK_NE:
        return 12;
    case '=':
        return 6;
    case ',':
        return 3;
    default:
        return MIN_PREC-1;
    }
#pragma GCC diagnostic pop
}

// funcall = (expr ("," expr)*)?
static Node *
parse_funcall(Token **tok, Token *mark)
{
    Node *node = new_obj_node(mark);

    Node head = {0};
    Node *args = &head;
    for (int i = 0; !skip(tok, ')'); ++i) {
        if (i) expect_skip(tok, ',');
        // We want ',' to be a stop point when parsing function arguments!
        args = args->next = parse_expr(tok, get_binary_precedence(','));
        add_type(args);
    }
    node->args = head.next;

    return node;
}

static Obj *
find_struct_member(Token *tok, Type *ty)
{
    if (ty->kind != TY_STRUCT) error_tok(tok, "Trying to member access a non-struct type!");
    for (Obj *m = ty->members; m; m = m->next) {
        if (m->tok->len == tok->len && strncmp(m->tok->str, tok->str, tok->len) == 0) {
            return m;
        }
    }
    return NULL;
}

static Node *
parse_right_unary(Token **tok, Node *left) {
    Token *mark = *tok;
    if (skip(tok, '[')) {
        Node *right = parse_expr(tok, MIN_PREC);
        expect_skip(tok, ']');
        left = new_unary_node(ND_DEREF, new_add_node(left, right, mark), mark);
    } else if (skip(tok, '.')) {
        add_type(left);
        Obj *member = find_struct_member(expect_skip(tok, TK_ID), left->ty);
        if (!member) error_tok(left->tok, "Struct has no member called '%.*s'!", mark->next->len, mark->next->str);
        left = new_unary_node(ND_MEMBER, left, mark);
        left->member = member;
    }

    return left;
}

// right leaning tree builder
static Node *
parse_increasing_recedence(Token **tok, Node *left, int min_prec)
{
    Token *mark = *tok;

    int next_prec = get_binary_precedence(mark->kind); // Returns lowest precedence on any non binary operation token so that we stop.

    // special case for '=' we need a right leaning tree in case of consecutive assignments
    if (next_prec + (mark->kind == '=') <= min_prec) return parse_right_unary(tok, left);

    (*tok) = (*tok)->next;

    Node *right = parse_expr(tok, next_prec);
    return new_binary_with_checks_node((NodeKind)mark->kind, left, right, mark);
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
parse_expr_statement(Token **tok)
{
    Token *mark = *tok;

    // empty statement
    if (skip(tok, ';')) return new_node(ND_BLOCK, mark);

    Node *node = parse_expr(tok, MIN_PREC);
    expect_skip(tok, ';');
    node = new_unary_node(ND_EXPR_STMT, node, mark);
    return node;
}

static Node *
parse_statement(Token **tok)
{
    Node *node;
    Token *mark = *tok;

    // declaration | defenition
    if (is_typename(*tok)) {
        node = new_node(ND_BLOCK, mark);
        node->body = parse_declaration(tok);
        return node;
    }

    // compound statement { ... }
    if (skip(tok, '{')) {
        enter_scope(SC_NESTED);
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
        node = new_unary_node(ND_RETURN, parse_expr(tok, MIN_PREC), mark);
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

static Type *
parse_struct_base_type(Token **tok)
{
    expect_skip(tok, '{');

    Type *ty = calloc(1, sizeof(*ty));
    ty->kind = TY_STRUCT;

    enter_scope(SC_STRUCT);

    while (!skip(tok, '}')) {
        parse_declaration(tok);
    }

    ty->members = leave_scope();

    // Order of the members is stack like, but we expect the offset of struct members to grow.
    // @TODO: Use dynamic arrays for scope objects.
    size_t size = 0;
    for (Obj *mem = ty->members; mem; mem = mem->next) {
        size += mem->ty->size;
    }
    ty->size = size;

    for (Obj *mem = ty->members; mem; mem = mem->next) {
        size -= mem->ty->size;
        mem->offset = size;
    }


    return ty;
}

static Type *
parse_base_type(Token **tok)
{
    Token *name = expect_skip(tok, TK_KEYWORD); // @Temporary, must expect typename.
    Type *ty;

    if (match_str(name, "struct")) {
        ty = parse_struct_base_type(tok);
    } else if (match_str(name, "char")) {
        ty = copy_type(ty_char);
    } else if (match_str(name, "int")) {
        ty = copy_type(ty_int);
    } else {
        error_tok(name, "Not a supported type name!");
    }

    ty->ty_name = name;

    return ty;
}

static Type *
parse_id_declarator(Token **tok, const Type *base_ty)
{
    // Be aware that right now we are using this function recursively to parse a function declerator and also its parameters. So one could pass a function declarator as a function declaration as parameter which is not defined in C and we do not support it either but for the sake of simplicity we do not care right now.
    Type *ty = copy_type(base_ty);

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

            if (match(tok, TK_ID)) {
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
parse_decl_assignment(Token **tok, Obj *obj)
{
    Token *mark = expect_skip(tok, '=');

    Node *left = new_obj_node(obj->tok);
    Node *right = parse_expr(tok, get_binary_precedence(',')); // Expect comman as a separator not an operator.
    Node *binary = new_binary_node(ND_ASSIGN, left, right, mark);
    return new_unary_node(ND_EXPR_STMT, binary, *tok);
}

static void
parse_function_body(Token **tok, Obj *obj)
{
    Token *mark = expect_skip(tok, '{');

    enter_scope(SC_FUNC);

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
        if (match(tok, TK_EOF)) error_tok(*tok, "Expected '}' at end of function defenition!");
    }
    body = new_node(ND_BLOCK, mark);
    body->body = head.next;
    obj->body = body;

    obj->locals = leave_scope();
}

//
// Creates Objects and puts them in the current scope
// They may or may not contain as their body a code block (ND_BLOCK).
// Returns a node-body in case of assignment to local declarations.
//
static Node *
parse_declaration(Token **tok)
{
    Type *base_ty = parse_base_type(tok);

    // e.g. int;
    if (skip(tok, ';')) return NULL;

    Node head = {0};
    Node *body = &head; // For assignments in local declarations.

    for (int i = 0; !skip(tok, ';'); ++i) {
        if (i) expect_skip(tok, ',');

        Type *ty = parse_id_declarator(tok, base_ty);

        Obj *obj = new_obj(ty, scope);

        if (i == 0 && obj->is_function && match(tok, '{')) {
            if (!is_global_scope(scope)) error_tok(*tok, "Nested function definition is not supporeted!");
            parse_function_body(tok, obj);
            // We don't need to return any node, since function declaration is
            // only supported in the global scope, and nodes of a function body
            // are assigned to its object.
            return NULL;
        }

        if (!obj->is_function && match(tok, '=')) {
            if (is_global_scope(scope)) {
                error_tok(*tok, "Global initialization of variables is not supported!");
            } else {
                body = body->next = parse_decl_assignment(tok, obj);
            }
            continue;
        }
    }

    return head.next;
}

Obj *
parse(Token *tok)
{
    while (tok->kind != TK_EOF) {
        parse_declaration(&tok);
    }

    return global->objs;
}
