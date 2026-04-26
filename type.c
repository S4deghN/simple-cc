#include "cc.h"

#include <stdio.h>
#include <stdlib.h>

Type *ty_int = &(Type){ .kind = TY_INT, .size = 8};

char *
ty_kind_str(TypeKind kind)
{
    switch (kind) {
        case TY_INT: return "INT";
        case TY_PTR: return "PTR";
        case TY_FUNC: return "FUNC";
        case TY_ARRAY: return "ARRAY";
        default:
    }
    return "???";
}

bool
is_type(Node *node, TypeKind kind)
{
    return node->ty->kind == kind;
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
    for (Node *n = node->body; n; n = n->next) add_type(node->body);

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
        node->ty = node->var->ty;
        return;
    case ND_ADDR:
        if (is_type(node->lhs, TY_ARRAY))
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
    case ND_EXPR_STMT:
    case ND_RETURN:
    case ND_BLOCK:
    case ND_IF:
    case ND_FOR:
    case ND_DO:
    default: // has no type
        return;
    }
}
