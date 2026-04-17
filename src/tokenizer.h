#ifndef SRC_LEXER_H
#define SRC_LEXER_H

#include <stddef.h>

typedef enum {
    TK_ID = 128,
    TK_NUM,
    TK_EOF,
} TokenKind;
char *tk_kind_str(TokenKind kind);

typedef struct {
    char *file_path;
    char *file_start;
    size_t file_len;
} FileInfo;

typedef struct Token Token;
struct Token {
    TokenKind kind;
    Token *next;
    char *str;
    size_t len;
    size_t line_nr;
    FileInfo *file_info;
};

Token *tokenize(char *str, size_t len, char *file_path);
Token *new_tok(TokenKind kind, char *str, size_t len);
void error_at(char *str, size_t len, size_t index, size_t line_nr, FileInfo *file_info, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);

#endif

#ifdef LEXER_IMPLEMENTATION

#include <string.h>
#include <ctype.h>

static FileInfo file_info;
static size_t line_nr;


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
verror_at(char *str, size_t len, size_t index, size_t line_nr, char *file_path, char *fmt, va_list arg_ptr)
{
    char *cursor = str + index;
    char *line_start = str_find_prev_newline(str,       cursor) + 1;
    char *line_end   = str_find_next_newline(str + len, cursor);
    int column = cursor - line_start;
    int line_len = line_end - line_start;

    fprintf(stderr, "%s:%ld:%d:\n", file_path, line_nr, column + 1);
    fprintf(stderr, "  %.*s\n", line_len, line_start);
    fprintf(stderr, "  %*s^\n", column, "");
    vfprintf(stderr, fmt, arg_ptr);
    fprintf(stderr, "\n");
}

void
error_at(char *str, size_t len, size_t index, size_t line_nr, FileInfo *file_info, char *fmt, ...)
{
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    verror_at(str, len, index, line_nr, file_info->file_path, fmt, arg_ptr);
    exit(1);
}

void
error_tok(Token *tok, char *fmt, ...)
{
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    char  *file_start = tok->file_info->file_start;
    size_t file_len   = tok->file_info->file_len;
    size_t index      = tok->str - file_start;
    char  *file_path  = tok->file_info->file_path;
    verror_at(file_start, file_len, index, tok->line_nr, file_path, fmt, arg_ptr);
    exit(1);
}

char *tk_kind_str(TokenKind kind) {
    switch (kind) {
        case TK_ID:  return "ID";
        case TK_NUM: return "NUM";
        case TK_EOF: return "EOF";
        default:
            if (ispunct(kind)) return "PUNCT";
            return "str conversion no implemented!";
    }
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
skip_id(size_t i, char *str, size_t len)
{
    if (!is_ident_start(str[i])) return i;

    while (++i < len && is_ident(str[i]));
    if (!is_boundry(str[i])) error_at(str, len, i, line_nr, &file_info, "Bad formed identifier!");

    return i;
}

static size_t
skip_num(size_t i, char *str, size_t len)
{
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
        error_at(str, len, i, line_nr, &file_info, "Bad formed hex number!");
    }

    if (str[i] == 'b' || str[i] == 'B') { // Binary numbers
        if (++i >= len || !is_bin_digit(str[i])) goto bin_err;
        while (++i < len && is_bin_digit(str[i]));
        if (!is_boundry(str[i])) goto bin_err;
        return i;
    bin_err:
        error_at(str, len, i, line_nr, &file_info, "Bad formed binary number!");
    }

    if (is_oct_digit(str[i])) { // Octal numbers
        while (++i < len && is_oct_digit(str[i]));
        if (!is_boundry(str[i])) error_at(str, len, i, line_nr, &file_info, "Bad formed octal number!");
        return i;
    }

num_err:
    error_at(str, len, i, line_nr, &file_info, "Bad formed number!");
    return i;
}

Token *
new_tok(TokenKind kind, char *str, size_t len) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->str = str;
    tok->len = len;
    return tok;
}

Token *
tokenize(char *str, size_t len, char *file_path)
{
    Token head = {0};
    Token *tok = &head;

    file_info.file_path = file_path;
    file_info.file_start = str;
    file_info.file_len = len;
    line_nr = 1;

    for (size_t i = 0; i < len;) {
        while (isspace(str[i])) {
            if (str[i] == '\n') line_nr += 1;
            ++i;
            if (i >= len) return head.next;
        }

        size_t mark = i;
        TokenKind kind;

        if      ((i = skip_id(i, str, len))  != mark) { kind = TK_ID; }
        else if ((i = skip_num(i, str, len)) != mark) { kind = TK_NUM; }
        else if (str[i] == '(')  { kind = '('; ++i; }
        else if (str[i] == ')')  { kind = ')'; ++i; }
        else if (str[i] == '{')  { kind = '{'; ++i; }
        else if (str[i] == '}')  { kind = '}'; ++i; }
        else if (str[i] == '"')  { kind = '"'; ++i; }
        else if (str[i] == ';')  { kind = ';'; ++i; }
        else if (str[i] == ',')  { kind = ','; ++i; }
        else if (str[i] == '=')  { kind = '='; ++i; }
        else if (str[i] == '*')  { kind = '*'; ++i; }
        else if (str[i] == '+')  { kind = '+'; ++i; }
        else if (str[i] == '-')  { kind = '-'; ++i; }
        else if (str[i] == '/')  { kind = '/'; ++i; }
        else if (str[i] == '!')  { kind = '!'; ++i; }
        else if (str[i] == '\\') { kind = '\\'; ++i; }
        else {
            error_at(str, len, i, line_nr, &file_info, "Unknown Syntax!");
        }

        tok = tok->next = new_tok(kind, str + mark, i - mark);
        tok->line_nr = line_nr;
        tok->file_info = &file_info;
    }

    return head.next;
}

#endif
