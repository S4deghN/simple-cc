#include "cc.h"

#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

char *
tk_kind_str(TokenKind kind)
{
    switch (kind) {
        case TK_EOF:  return "EOF";
        case TK_ID:   return "ID";
        case TK_NUM:  return "NUM";
        case TK_GREQ: return "GREQ";
        case TK_LTEQ: return "LTEQ";
        case TK_EQ:   return "EQ";
        case TK_NOEQ: return "NOEQ";
        default:
            if (ispunct(kind)) return "PUNCT";
            return "str conversion no implemented!";
    }
}

static char *
str_find_next_newline(char *str_end, char *cursor)
{
    for (; cursor < str_end; ++cursor) {
        if (*cursor == '\n') break;
    }
    return cursor;
}

static char *
str_find_prev_newline(char *str_start, char *cursor)
{
    for (; cursor >= str_start; --cursor) {
        if (*cursor == '\n') break;
    }
    return cursor;
}

static void
verror_at(char *str, size_t len, size_t offset, size_t line_nr, char *file_path, char *fmt, va_list ap)
{
    char *cursor = str + offset;
    char *line_start = str_find_prev_newline(str,       cursor) + 1;
    char *line_end   = str_find_next_newline(str + len, cursor);
    int column = cursor - line_start;
    int line_len = line_end - line_start;

    fprintf(stderr, "%s:%ld:%d:\n", file_path, line_nr, column + 1);
    fprintf(stderr, "  %.*s\n", line_len, line_start);
    fprintf(stderr, "  %*s^\n", column, "");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void
error_at(File *file, size_t offset, size_t line_nr, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    verror_at(file->str, file->len, offset, line_nr, file->path, fmt, ap);
    exit(1);
}

void
error_tok(Token *tok, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char  *str    = tok->file->str;
    size_t len    = tok->file->len;
    size_t offset = tok->str - str;
    verror_at(str, len, offset, tok->line_nr, tok->file->path, fmt, ap);
    exit(1);
}

void
error(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

static int
is_ident_start(int c)
{
    return (isalpha(c) || c == '_');
}

static int
is_ident(int c)
{
    return (is_ident_start(c) || isdigit(c));
}

static int
is_bin_digit(int c)
{
    return (c == '0' || c == '1');
}

static int
is_oct_digit(int c)
{
    return ('0' <= c && c <= '7');
}

static int
is_boundry(int c)
{
    return (isspace(c) || ispunct(c) || c == '\0');
}

static size_t
skip_id(File *file, size_t i, size_t line_nr)
{
    size_t len = file->len;
    char  *str = file->str;

    if (!is_ident_start(str[i])) return i;

    while (++i < len && is_ident(str[i]));
    if (!is_boundry(str[i])) error_at(file, i, line_nr, "Bad formed identifier!");

    return i;
}

static size_t
skip_num(File *file, size_t i, size_t line_nr)
{
    size_t len = file->len;
    char  *str = file->str;

    if (!isdigit(str[i])) return i;

    if (str[i] != '0') { // Regular number
        while (++i < len && isdigit(str[i]));
        if (!is_boundry(str[i])) goto num_err;
        return i;
    }

    if (++i >= len || is_boundry(str[i])) return i;

    if (str[i] == 'x' || str[i] == 'X') { // Hex numbers
        if (++i >= len || !isxdigit(str[i])) goto hex_err;
        while (++i < len && isxdigit(str[i]));
        if (!is_boundry(str[i])) goto hex_err;
        return i;
    hex_err:
        error_at(file, i, line_nr, "Bad formed hex number!");
    }

    if (str[i] == 'b' || str[i] == 'B') { // Binary numbers
        if (++i >= len || !is_bin_digit(str[i])) goto bin_err;
        while (++i < len && is_bin_digit(str[i]));
        if (!is_boundry(str[i])) goto bin_err;
        return i;
    bin_err:
        error_at(file, i, line_nr, "Bad formed binary number!");
    }

    if (is_oct_digit(str[i])) { // Octal numbers
        while (++i < len && is_oct_digit(str[i]));
        if (!is_boundry(str[i])) error_at(file, i, line_nr, "Bad formed octal number!");
        return i;
    }

num_err:
    error_at(file, i, line_nr, "Bad formed number!");
    return i;
}

static size_t
skip_rel(File *file, size_t i, size_t line_nr, TokenKind *kind)
{
    (void) line_nr;
    size_t len = file->len;
    char  *str = file->str;

    if (i+1 >= len || str[i+1] != '=') return i;

    switch (str[i]) {
    case '=': *kind = TK_EQ;   i += 2; break;
    case '!': *kind = TK_NOEQ; i += 2; break;
    case '<': *kind = TK_LTEQ; i += 2; break;
    case '>': *kind = TK_GREQ; i += 2; break;
    }

    return i;
}

Token *
new_tok(TokenKind kind, char *str, size_t len, File *file, size_t line_nr) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->str = str;
    tok->len = len;
    tok->file = file;
    tok->line_nr = line_nr;
    return tok;
}

Token *
tokenize(File *file)
{
    size_t len     = file->len;
    char  *str     = file->str;
    size_t line_nr = 1;

    Token head = {0};
    Token *tok = &head;

    size_t i = 0;
    for (; i < len;) {
        if (isspace(str[i])) {
            if (str[i] == '\n') line_nr += 1;
            ++i;
            continue;
        }

        size_t mark = i;
        TokenKind kind;

        if      ((i = skip_id(file, i, line_nr)) != mark) { kind = TK_ID; }
        else if ((i = skip_num(file, i, line_nr)) != mark) { kind = TK_NUM; }
        else if ((i = skip_rel(file, i, line_nr, &kind)) != mark) {}
        else if (ispunct(str[i])) { kind = str[i++]; }
        else {
            error_at(file, i, line_nr, "Unknown Syntax!");
        }

        tok = tok->next = new_tok(kind, str + mark, i - mark, file, line_nr);
    }

    tok = tok->next = new_tok(TK_EOF, str + i, 0, file, line_nr);

    return head.next;
}
