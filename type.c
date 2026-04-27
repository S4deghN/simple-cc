#include "cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Type *ty_char = &(Type){ .kind = TY_CHAR, .size = 1};
Type *ty_int = &(Type){ .kind = TY_INT, .size = 8};

char *
ty_kind_str(TypeKind kind)
{
    switch (kind) {
        case TY_CHAR: return "CHAR";
        case TY_INT: return "INT";
        case TY_PTR: return "PTR";
        case TY_FUNC: return "FUNC";
        case TY_ARRAY: return "ARRAY";
        default:
    }
    return "???";
}

bool
is_integer(Type *ty)
{
    return ty->kind == TY_INT || ty->kind == TY_CHAR;
}

Type *
copy_type(Type *ty)
{
  Type *ret = calloc(1, sizeof(*ret));
  *ret = *ty;
  return ret;
}

Type *
pointer_to(Type *base)
{
    Type *ty = calloc(1, sizeof(*ty));
    ty->kind = TY_PTR;
    ty->base = base;
    ty->size = 8;
    return ty;
}

Type *
array_of(Type *base, int len)
{
    Type *ty = copy_type(base);
    ty->kind = TY_ARRAY;
    ty->size = base->size * len;
    ty->base = base;
    ty->array_len = len;
    return ty;
}

Type *
func_type(Type *ret_ty)
{
  Type *ty = copy_type(ret_ty);
  ty->kind = TY_FUNC;
  ty->ret_ty = ret_ty;
  return ty;
}

int
ptr_indirection(Type *ty)
{
    int count = 0;
    for (Type *c = ty; c->base; c = c->base) {
        ++count;
    }
    return count;
}

bool
ptr_type_match(Type *a, Type *b)
{
    if (!a->base || !b->base) return false;

    int a_indir = 0;
    Type *a_base;
    for (a_base = a; a_base->base; a_base = a_base->base) {
        ++a_indir;
    }

    int b_indir = 0;
    Type *b_base;
    for (b_base = b; b_base->base; b_base = b_base->base) {
        ++b_indir;
    }

    if (a_indir != b_indir) return false;
    if (a_base->kind != b_base->kind) return false;

    return true;
}

bool
func_type_match(Type *a, Type *b)
{
    if (a->kind != b->kind) return false;
    if (a->kind != TY_FUNC) return false;
    if (a->ret_ty->kind != b->ret_ty->kind) return false;
    if (a->ret_ty->kind == TY_PTR && !ptr_type_match(a->ret_ty, b->ret_ty)) return false;
    if (a->param_count != b->param_count) return false;

    Type *ap = a->params;
    Type *bp = b->params;
    for (int i = 0; i < a->param_count; ++i) {
        if (ap->kind != bp->kind) return false;
        if (ap->kind == TY_FUNC && !func_type_match(ap, bp)) return false;
        ap = ap->next;
        bp = bp->next;
    }

    if (a->id_name->len != b->id_name->len ||
        strncmp(a->id_name->str, b->id_name->str, a->id_name->len) != 0) return false;

    return true;
}

void
add_type(Node *node)
{
    if (!node || node->ty) return;

    add_type(node->lhs);
    add_type(node->rhs);
    add_type(node->cond);
    add_type(node->then);
    add_type(node->init);
    add_type(node->iter);
    for (Node *n = node->body; n; n = n->next) add_type(n);

    switch (node->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_NEG:
    case ND_ASSIGN:
        node->ty = node->lhs->ty;
        return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LTE:
    case ND_NUM:
        node->ty = ty_int;
        return;
    case ND_VAR:
        node->ty = node->obj->ty;
        return;
    case ND_ADDR:
        if (node->lhs->ty->kind == TY_ARRAY)
            node->ty = pointer_to(node->lhs->ty->base);
        else
            node->ty = pointer_to(node->lhs->ty);
        break;
    case ND_DEREF:
        if (!node->lhs->ty->base) { // We check baser for pointer-array duality.
            error_tok(node->tok, "Invalid pointer dereference!");
        }
        node->ty = node->lhs->ty->base;
        break;
    case ND_FUNCALL:
        node->ty = ty_int; // @Temporary until we drop undeclared function calls.
        break;
    case ND_RETURN:
    case ND_EXPR_STMT:
    case ND_BLOCK:
    case ND_IF:
    case ND_FOR:
    case ND_DO:
    default: // has no type
        return;
    }
}
