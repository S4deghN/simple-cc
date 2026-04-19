#include "cc.h"

#include <stdio.h>
#include <assert.h>

static int depth;

static void
push(void)
{
    printf("  push\t%%rax\n");
    depth++;
}

static void
pop(char *arg)
{
    printf("  pop\t%s\n", arg);
    depth--;
}

static void
gen_expr(Node *node)
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

static void
gen_stmt(Node *node)
{
    expect_node(node, ND_EXPR_STMT);
    gen_expr(node->lhs);
    return;
}

void
codegen(Node *node) 
{
    printf(".global main\n");
    printf("main:\n");

    for (Node *n = node; n; n = n->next) {
        print_tree(node, "  // ");
        gen_stmt(n);
    }
    assert(depth == 0);

    printf("  ret\n");
}
