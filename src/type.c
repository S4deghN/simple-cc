#include "cc.h"

#include <stdlib.h>

Type *ty_int = &(Type){TY_INT};

bool
is_type(Node *node, TypeKind kind)
{
    return node->ty->kind == kind;
}

Type *
pointer_to(Type *type)
{
    Type *ty = calloc(1, sizeof(*ty));
    ty->kind = TY_PTR;
    ty->base = type;
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
    case ND_VAR:
        node->ty = ty_int;
        return;
    case ND_ADDR:
        node->ty = pointer_to(node->lhs->ty);
        return;
    case ND_DEREF:
        if (is_type(node->lhs, TY_PTR)) {
            node->ty = node->lhs->ty->base;
        } else {
            node->ty = ty_int;
        }
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
