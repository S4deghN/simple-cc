#include "cc.h"

#include <stdio.h>
#include <assert.h>

static int depth;
// based on this: https://en.wikipedia.org/wiki/X86_calling_conventions#List_of_x86_calling_conventions, syscalls use %r10 instread of %rcx
static char *call_reg[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};

static Function *current_fn;

static int
counter()
{
    static int count;
    return count++;
}

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

static void gen_expr(Node *node);

static void
gen_addr(Node *node)
{
    switch (node->kind) {
    case ND_VAR:
        printf("  lea %d(%%rbp), %%rax\n", node->var->stack_offset);
        // or
        // printf("  mov\t%%rbp, %%rax\n");
        // printf("  sub\t$%d, %%rax\n", node->var->stack_offset);
        break;
    case ND_DEREF:
        gen_expr(node->lhs);
        break;
    default:
        expect_node_many(node, 2, ND_VAR, ND_DEREF);
    }
}

static void
gen_expr(Node *node)
{
    NodeKind kind = node->kind;

    switch (kind) {
    case ND_NUM:
        printf("  mov\t$%d, %%rax\n", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        printf("  neg\t%%rax\n");
        return;
    case ND_VAR:
        gen_addr(node);
        printf("  mov\t(%%rax), %%rax\n");
        return;
    case ND_DEREF:
        gen_expr(node->lhs); // first load var from stack to rax.
        printf("  mov\t(%%rax), %%rax\n");
        return;
    case ND_ADDR:
        // NOTE: we could also check for lvalueness of lhs here instead of the parser.
        // same for ND_ASSIGN.
        gen_addr(node->lhs);
        return;
    case ND_ASSIGN:
        gen_addr(node->lhs);
        push();
        gen_expr(node->rhs);
        pop("%rdi");
        printf("  mov\t%%rax, (%%rdi)\n");
        return;
    case ND_FUNCALL:
        int nargs = 0;
        for (Node *arg = node->args; arg; arg = arg->next) {
            gen_expr(arg);
            push();
            nargs += 1;
        }
        for (int i = nargs - 1; i >= 0; --i) {
            pop(call_reg[i]);
        }
        printf("  mov\t$0, %%rax\n");
        printf("  call\t%.*s\n", node->tok->len, node->tok->str);
        return;
    default:
        if (!node->rhs || !node->lhs) {
            print_tree(node, "");
            assert("This node can not be handles as lhs, rhs expresion!");
        }
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");

    switch (kind) {
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
    case ND_GT:
    case ND_GTE:
        printf("  cmp\t%%rdi, %%rax\n");
        if      (kind == ND_EQ)  printf("  sete\t%%al\n");
        else if (kind == ND_NE)  printf("  setne\t%%al\n");
        else if (kind == ND_LT)  printf("  setl\t%%al\n");
        else if (kind == ND_LTE) printf("  setle\t%%al\n");
        else if (kind == ND_GT)  printf("  setg\t%%al\n");
        else if (kind == ND_GTE) printf("  setge\t%%al\n");
        printf("  movzb\t%%al, %%rax\n");
        return;
    default:
        error("codege: invalid expression");
    }
}

static void
gen_stmt(Node *node)
{
    int uniq = counter();

    switch (node->kind) {
    case ND_FOR:
        if (node->init) gen_stmt(node->init);
        printf(".L.for_begin.%d:\n", uniq);
        if (node->cond) {
            gen_expr(node->cond);
            printf("  cmp\t$0, %%rax\n");
            printf("  je \t.L.for_end.%d\n", uniq);
        }
        gen_stmt(node->then);
        if (node->iter) gen_expr(node->iter);
        printf("  jmp\t.L.for_begin.%d\n", uniq);
        printf(".L.for_end.%d:\n", uniq);
        break;
    case ND_DO:
        printf(".L.do_begin.%d:\n", uniq);
        gen_stmt(node->then);
        gen_expr(node->cond);
        printf("  cmp\t$0, %%rax\n");
        printf("  je \t.L.do_end.%d\n", uniq);
        printf("  jmp\t.L.do_begin.%d\n", uniq);
        printf(".L.do_end.%d:\n", uniq);
        break;
    case ND_IF:
        gen_expr(node->cond);
        printf("  cmp\t$0, %%rax\n");
        printf("  je \t.L.if_end.%d\n", uniq);
        gen_stmt(node->then);
        if (node->els) printf("  jmp\t.L.endelse.%d\n", uniq);
        printf(".L.if_end.%d:\n", uniq);
        if (node->els) {
            gen_stmt(node->els);
            printf(".L.endelse.%d:\n", uniq);
        }
        break;
    case ND_BLOCK:
        for (Node *n = node->body; n; n = n->next) {
            gen_stmt(n);
        }
        break;
    case ND_RETURN:
        gen_expr(node->lhs);
        printf("  jmp\t.L.return.%.*s\n", current_fn->tok->len, current_fn->tok->str);
        break;
    case ND_EXPR_STMT:
        gen_expr(node->lhs);
        break;
    default:
        // gen_expr(node);
        expect_node_many(node, 3, ND_EXPR_STMT, ND_RETURN, ND_BLOCK);
    }
}

static void
assing_locals_offset(Function *func)
{
    int offset = 0;
    // NOTE: We expect that this list of locals is in stack order. i.e., last declared stack variable must be assigned the smallest (in magnitude) offset. (closest to the stack base)
    for (Var *var = func->locals; var; var = var->next) {
        offset -= 8;
        var->stack_offset = offset;
    }
    func->stack_size = align_to(-offset, 16);
}

void
codegen(Function *functions)
{
    for (Function *fn = functions; fn; fn = fn->next) {
        current_fn = fn;
        assing_locals_offset(fn);

        Token *name = fn->tok;
        printf(".global %.*s\n", name->len, name->str);
        printf("%.*s:\n", name->len, name->str);

        // prolog
        printf("  push\t%%rbp\n");
        printf("  mov\t%%rsp, %%rbp\n");
        printf("  sub\t$%d, %%rsp\n", fn->stack_size);

        print_tree(fn->body, "  // ");
        gen_stmt(fn->body);
        assert(depth == 0);

        // epilogue
        printf(".L.return.%.*s:\n", name->len, name->str);
        printf("  mov\t%%rbp, %%rsp\n");
        printf("  pop\t%%rbp\n");

        printf("  ret\n");
    }
}
