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
gen_addr(Node *node)
{
    int offset = (node->name - 'a' + 1) * 8;
    printf("  lea -%d(%%rbp), %%rax\n", offset);
    // or
    // printf("  mov\t%%rbp, %%rax\n");
    // printf("  sub\t$%d, %%rax\n", offset);
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
    } else if (node->kind == ND_VAR) {
        gen_addr(node);
        printf("  mov\t(%%rax), %%rax\n");
        return;
    } else if (node->kind == ND_ASSIGN) {
        gen_addr(node->lhs);
        push();
        gen_expr(node->rhs);
        pop("%rdi");
        printf("  mov\t%%rax, (%%rdi)\n");
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

    // prolog
    printf("  push %%rbp\n");
    printf("  mov %%rsp, %%rbp\n");
    printf("  sub $208, %%rsp\n"); // allocated stack memory for all single letter variable.

    for (Node *n = node; n; n = n->next) {
        print_tree(n, "  // ");
        gen_stmt(n);
    }
    assert(depth == 0);

    // epilogue
    printf("  mov\t%%rbp, %%rsp\n");
    printf("  pop\t%%rbp\n");

    printf("  ret\n");
}
