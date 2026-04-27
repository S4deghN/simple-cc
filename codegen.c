#include "cc.h"

#include <stdio.h>
#include <assert.h>

static int depth;
// based on this: https://en.wikipedia.org/wiki/X86_calling_conventions#List_of_x86_calling_conventions, syscalls use %r10 instread of %rcx
static char *call_reg8[] =  {"%dil", "%sil", "%dl",  "%cl", "%r8b", "%r9b"};
static char *call_reg64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};

static Obj *current_fn;

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
        if (node->obj->is_local) {
            printf("  lea\t%d(%%rbp), %%rax\n", node->obj->stack_offset);
        } else {
            Token *tok = node->obj->tok;
            printf("  lea\t%.*s(%%rip), %%rax\n", tok->len, tok->str);
        }
        break;
    case ND_DEREF:
        gen_expr(node->lhs);
        break;
    default:
        expect_node_many(node, 2, ND_VAR, ND_DEREF);
    }
}

static void
load(Type *ty)
{
    if (ty->kind == TY_ARRAY) {
        // An array's value is its address which is expected to be already loaded
        // into %rax by gen_addr.
        return;
    }
    switch (ty->size) {
    case 1:
        printf("  movsbq\t(%%rax), %%rax\n");
        break;
    case 8:
        printf("  mov\t(%%rax), %%rax\n");
        break;
    default:
        error_tok(ty->id_name, "Size %d type is not supported!");
    }
}

// Store %rax to an address that the stack top is pointing to.
static void
store(Type *ty)
{
    pop("%rdi");
    switch (ty->size) {
    case 1:
        printf("  mov %%al, (%%rdi)\n");
        break;
    case 8:
        printf("  mov %%rax, (%%rdi)\n");
        break;
    default:
        error_tok(ty->id_name, "Size %d type is not supported!");
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
        load(node->ty);
        return;
    case ND_DEREF:
        gen_expr(node->lhs); // first load var from stack to rax.
        load(node->ty);
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
        store(node->ty);
        return;
    case ND_FUNCALL:
        int nargs = 0;
        for (Node *arg = node->args; arg; arg = arg->next) {
            gen_expr(arg);
            push();
            nargs += 1;
        }
        for (int i = nargs - 1; i >= 0; --i) {
            pop(call_reg64[i]);
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
        printf("  cqo\n"); // lhs is in rax right now.
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
assing_locals_offset(Obj *func)
{
    int offset = 0;
    // NOTE: We expect that this list of locals is in stack order. i.e., last declared stack variable must be assigned the smallest (in magnitude) offset. (closest to the stack base)
    for (Obj *var = func->locals; var; var = var->next) {
        offset -= var->ty->size;
        var->stack_offset = offset;
    }
    func->stack_size = align_to(-offset, 16);
}

static void
emit_data(Obj *prog)
{
    Token *tok;
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function) continue;
        tok = var->tok;
        printf("  .data\n");
        printf("  .globl %.*s\n", tok->len, tok->str);
        printf("%.*s:\n", tok->len, tok->str);
        printf("  .zero %d\n", var->ty->size);
    }
}

static void
emit_text(Obj *prog)
{
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function) continue;
        if (!fn->body) continue; // it's only a declaration

        current_fn = fn;
        assing_locals_offset(fn);

        Token *name = fn->tok;
        printf("  .global %.*s\n", name->len, name->str);
        printf("  .text\n");
        printf("%.*s:\n", name->len, name->str);

        // prolog
        printf("  push\t%%rbp\n");
        printf("  mov\t%%rsp, %%rbp\n");
        printf("  sub\t$%d, %%rsp\n", fn->stack_size);

        // Save passed-by-register arguments to the stack
        int i = fn->parameters_count - 1;
        for (Obj *var = fn->parameters; var; var = var->next) {
            if (var->ty->size == 1) {
                printf("  mov\t%s, %d(%%rbp)\n", call_reg8[i--], var->stack_offset);
            } else {
                printf("  mov\t%s, %d(%%rbp)\n", call_reg64[i--], var->stack_offset);
            }
        }

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

void
codegen(Obj *prog)
{
  emit_data(prog);
  emit_text(prog);
}
