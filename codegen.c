#include "cc.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>

static FILE *output_file;

static int depth;
// based on this: https://en.wikipedia.org/wiki/X86_calling_conventions#List_of_x86_calling_conventions, syscalls use %r10 instread of %rcx
static char *call_reg8[] =  {"%dil", "%sil", "%dl",  "%cl", "%r8b", "%r9b"};
static char *call_reg64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};

static Obj *current_fn;

static void
println(char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  va_end(ap);
  fprintf(output_file, "\n");
}

static void
new_unique_name(Token **tok) {
  static int id = 0;
  (*tok)->str = calloc(1, 20);
  (*tok)->len = sprintf((*tok)->str, ".L..%d", id++);
}

static int
counter()
{
    static int count;
    return count++;
}

static void
push(void)
{
    println("  push\t\t%%rax");
    depth++;
}

static void
pop(char *arg)
{
    println("  pop\t\t%s", arg);
    depth--;
}

static void gen_expr(Node *node);

static void
gen_addr(Node *node)
{
    switch (node->kind) {
    case ND_VAR:
        if (node->obj->is_local) {
            println("  lea\t\t%d(%%rbp), %%rax", node->obj->stack_offset);
        } else {
            Token *tok = node->obj->tok;
            println("  lea\t\t%.*s(%%rip), %%rax", tok->len, tok->str);
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
        println("  movsbq\t(%%rax), %%rax");
        break;
    case 8:
        println("  mov\t\t(%%rax), %%rax");
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
        println("  mov\t\t%%al, (%%rdi)");
        break;
    case 8:
        println("  mov\t\t%%rax, (%%rdi)");
        break;
    default:
        error_tok(ty->id_name, "Size %d type is not supported!");
    }
}

static void gen_stmt(Node *node);

static void
gen_expr(Node *node)
{
    NodeKind kind = node->kind;

    switch (kind) {
    case ND_NUM:
        println("  mov\t\t$%d, %%rax", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        println("  neg\t\t%%rax");
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
        println("  mov\t\t$0, %%rax");
        println("  call\t\t%.*s", node->tok->len, node->tok->str);
        return;
    case ND_STMT_EXPR:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;

    default:
        if (!node->rhs || !node->lhs) {
            print_tree(stderr, node, "");
            assert("This node can not be handles as lhs, rhs expresion!");
        }
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");

    switch (kind) {
    case ND_ADD:
        println("  add\t\t%%rdi, %%rax");
        return;
    case ND_SUB:
        println("  sub\t\t%%rdi, %%rax");
        return;
    case ND_MUL:
        println("  imul\t\t%%rdi, %%rax");
        return;
    case ND_DIV:
        println("  cqo"); // lhs is in rax right now.
        println("  idiv\t\t%%rdi");
        return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LTE:
    case ND_GT:
    case ND_GTE:
        println("  cmp\t\t%%rdi, %%rax");
        if      (kind == ND_EQ)  println("  sete\t\t%%al");
        else if (kind == ND_NE)  println("  setne\t\t%%al");
        else if (kind == ND_LT)  println("  setl\t\t%%al");
        else if (kind == ND_LTE) println("  setle\t\t%%al");
        else if (kind == ND_GT)  println("  setg\t\t%%al");
        else if (kind == ND_GTE) println("  setge\t\t%%al");
        println("  movzb\t\t%%al, %%rax");
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
        println(".L.for_begin.%d:", uniq);
        if (node->cond) {
            gen_expr(node->cond);
            println("  cmp\t\t$0, %%rax");
            println("  je \t\t.L.for_end.%d", uniq);
        }
        gen_stmt(node->then);
        if (node->iter) gen_expr(node->iter);
        println("  jmp\t\t.L.for_begin.%d", uniq);
        println(".L.for_end.%d:", uniq);
        break;
    case ND_DO:
        println(".L.do_begin.%d:", uniq);
        gen_stmt(node->then);
        gen_expr(node->cond);
        println("  cmp\t\t$0, %%rax");
        println("  je \t\t.L.do_end.%d", uniq);
        println("  jmp\t\t.L.do_begin.%d", uniq);
        println(".L.do_end.%d:", uniq);
        break;
    case ND_IF:
        gen_expr(node->cond);
        println("  cmp\t\t$0, %%rax");
        println("  je \t\t.L.if_end.%d", uniq);
        gen_stmt(node->then);
        if (node->els) println("  jmp\t\t.L.endelse.%d", uniq);
        println(".L.if_end.%d:", uniq);
        if (node->els) {
            gen_stmt(node->els);
            println(".L.endelse.%d:", uniq);
        }
        break;
    case ND_BLOCK:
        for (Node *n = node->body; n; n = n->next) {
            gen_stmt(n);
        }
        break;
    case ND_RETURN:
        gen_expr(node->lhs);
        println("  jmp\t\t.L.return.%.*s", current_fn->tok->len, current_fn->tok->str);
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

        if (tok->kind == TK_STR) new_unique_name(&tok);

        println("  .data");
        println("  .globl %.*s", tok->len, tok->str);
        println("%.*s:", tok->len, tok->str);
        if (var->init_data) {
            for (int i = 0; i < var->ty->size; i++)
                println("  .byte %d", var->init_data[i]);
        } else {
            println("  .zero %d", var->ty->size);
        }
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
        println("  .global %.*s", name->len, name->str);
        println("  .text");
        println("%.*s:", name->len, name->str);

        // prolog
        println("  push\t\t%%rbp");
        println("  mov\t\t%%rsp, %%rbp");
        println("  sub\t\t$%d, %%rsp", fn->stack_size);

        // Save passed-by-register arguments to the stack
        int i = fn->parameters_count - 1;
        for (Obj *var = fn->parameters; var; var = var->next) {
            if (var->ty->size == 1) {
                println("  mov\t\t%s, %d(%%rbp)", call_reg8[i--], var->stack_offset);
            } else {
                println("  mov\t\t%s, %d(%%rbp)", call_reg64[i--], var->stack_offset);
            }
        }

        print_tree(output_file, fn->body, "  // ");
        gen_stmt(fn->body);
        assert(depth == 0);

        // epilogue
        println(".L.return.%.*s:", name->len, name->str);
        println("  mov\t\t%%rbp, %%rsp");
        println("  pop\t\t%%rbp");

        println("  ret");
    }
}

void
codegen(Obj *prog, FILE* out)
{
    output_file = out;
    emit_data(prog);
    emit_text(prog);
}
