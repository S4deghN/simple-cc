#include "cc.h"

char *
nd_kind_str(NodeKind kind)
{
    switch (kind) {
        case ND_ADD: return "ADD";
        case ND_SUB: return "SUB";
        case ND_MUL: return "MUL";
        case ND_DIV: return "DIV";
        case ND_NUM: return "NUM";
        case ND_NEG: return "NEG";
        case ND_LT:  return "LT";
        case ND_LTE: return "LTE";
        case ND_GT:  return "GT";
        case ND_GTE: return "GTE";
        case ND_EQ:  return "EQ";
        case ND_NE:  return "NE";
        case ND_VAR: return "VAR";
        case ND_ASSIGN: return "ASSIGN";
        case ND_RETURN: return "RETURN";
        case ND_EXPR_STMT:  return "EXPR_STMT";
        case ND_STMT_EXPR:  return "STMT_EXPR";
        case ND_BLOCK:  return "BLOCK";
        case ND_IF:  return "IF";
        case ND_FOR:  return "FOR";
        case ND_DO:  return "DO";
        case ND_ADDR:  return "ADDR";
        case ND_DEREF:  return "DEREF";
        case ND_FUNCALL:  return "FUNCALL";
        default: return "???";
    }
}

static void
get_stmt_str(Token *tok, char **str, int *len)
{
    char *line_start;
    if (tok->str == tok->file->str) line_start = tok->str;
    else line_start = str_find_prev(tok->file->str, tok->str - 1, ';'); // Go back one step in case we are on a ';'
    if (*line_start == ';') line_start += 1; // skip previous ';'
    char *line_end   = str_find_next(tok->file->str + tok->file->len, tok->str, ';') + 1;

    *str = line_start;
    *len = line_end - line_start;
}


static void
_print_tree(FILE *out, const Node *node, char *prefix_buff, int prefix_cursor, int is_right, char *alt_root_name, char *wrap)
{
    if (!node) return;

    char *add;
    char *fmt;
    char *wrap_fmt;
    if (is_right == -1) { // root
        fmt = "%.*s%.*s: %s\n";
        wrap_fmt = "%.*s%s(%.*s): %s\n";
        add = "";
    } else if (is_right) { // right branch
        fmt = "%.*s├─ %.*s: %s\n";
        wrap_fmt = "%.*s├─ %s(%.*s): %s\n";
        add = "│  ";
    } else { // left branch
        fmt = "%.*s└─ %.*s: %s\n";
        wrap_fmt = "%.*s└─ %s(%.*s): %s\n";
        add = "   ";
    }

    char *node_str;
    int node_str_len;
    char *type_str = node->ty ? ty_kind_str(node->ty->kind) : "ø";
    if (node->kind == ND_NUM) {
        char tmp[16];
        node_str = tmp;
        node_str_len = sprintf(tmp, "%d", node->val);
    } else if (node->kind == ND_VAR || node->kind == ND_FUNCALL) {
        node_str = node->tok->str;
        node_str_len = node->tok->len;
    } else if (alt_root_name) {
        node_str = alt_root_name;
        node_str_len = strlen(alt_root_name);
    } else {
        node_str = nd_kind_str(node->kind);
        node_str_len = strlen(node_str);
    }

    if (wrap)
        fprintf(out, wrap_fmt, prefix_cursor, prefix_buff, wrap, node_str_len, node_str, type_str);
    else
        fprintf(out, fmt, prefix_cursor, prefix_buff, node_str_len, node_str, type_str);

    memcpy(&prefix_buff[prefix_cursor], add, strlen(add));
    prefix_cursor += strlen(add);

    switch (node->kind) {
    case ND_IF:
        _print_tree(out, node->cond, prefix_buff, prefix_cursor, 1, NULL, "COND");
        _print_tree(out, node->then, prefix_buff, prefix_cursor, 1, NULL, "THEN");
        _print_tree(out, node->els,  prefix_buff, prefix_cursor, 1, NULL, "ELSE");
        break;
    case ND_FOR:
        if (node->init) _print_tree(out, node->init, prefix_buff, prefix_cursor, 1, NULL, "INIT");
        if (node->cond) _print_tree(out, node->cond, prefix_buff, prefix_cursor, 1, NULL, "COND");
        if (node->iter) _print_tree(out, node->iter, prefix_buff, prefix_cursor, 1, NULL, "ITER");
        if (node->then) _print_tree(out, node->then, prefix_buff, prefix_cursor, 1, NULL, "THEN");
        break;
    case ND_BLOCK:
    case ND_STMT_EXPR:
        for (Node *n = node->body; n; n = n->next) {
            _print_tree(out, n, prefix_buff, prefix_cursor, 1, NULL, NULL);
        }
        break;
    case ND_FUNCALL:
        for (Node *n = node->args; n; n = n->next) {
            _print_tree(out, n, prefix_buff, prefix_cursor, 1, NULL, NULL);
        }
        break;
    default:
        // Recurse: right then left so right appears above left
        _print_tree(out, node->rhs, prefix_buff, prefix_cursor, 1, NULL, NULL);
        _print_tree(out, node->lhs, prefix_buff, prefix_cursor, 0, NULL, NULL);
    }
}

void
print_tree(FILE *out, const Node *root, char *prefix)
{
    // char *line;
    // int line_len;
    // get_stmt_str(root->tok, &line, &line_len);
    // fprintf(out, "%s%.*s\n", prefix, line_len, line);

    char prefix_buff[256]; // must be big enough or segfault
    memcpy(prefix_buff, prefix, strlen(prefix));

    _print_tree(out, root, prefix_buff, strlen(prefix), -1, NULL, NULL);
}


char *
str_find_next(char *str_end, char *cursor, char c)
{
    assert(cursor <= str_end);

    for (;cursor < str_end; ++cursor) {
        if (*cursor == c) break;
    }
    return cursor;
}

char *
str_find_prev(char *str_start, char *cursor, char c)
{
    assert(cursor >= str_start);

    for (; cursor > str_start; --cursor) {
        if (*cursor == c) break;
    }
    return cursor;
}

int
align_to(int n, int align)
{
    return n + (align - (n % align)) % align;
}

File
read_entire_file(const char *path)
{
    FILE *fp;

    if (strcmp(path, "-") == 0) {
        // By convention, read from stdin if a given filename is "-".
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp)
            error("cannot open %s: %s", path, strerror(errno));
    }

    char *buf;
    size_t buflen;
    FILE *out = open_memstream(&buf, &buflen);

    // Read the entire file.
    for (;;) {
        char buf2[4096];
        int n = fread(buf2, 1, sizeof(buf2), fp);
        if (n == 0)
            break;
        fwrite(buf2, 1, n, out);
    }

    if (fp != stdin)
        fclose(fp);

    // Make sure that the last line is properly terminated with '\n'.
    fflush(out);

    if (buflen == 0 || buf[buflen - 1] != '\n') {
        fputc('\n', out);
        ++buflen;
    }

    fputc('\0', out); ++buflen;

    fclose(out);

    return (File) { .str = buf, .len = buflen, .path = (char*)path};
}
