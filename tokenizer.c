#include "cc.h"

#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

char *
tk_kind_str(TokenKind kind)
{
    char *ret_buff = malloc(16); // TODO: use temp buffer.
    switch (kind) {
        case TK_EOF:     return "EOF";
        case TK_ID:      return "ID";
        case TK_KEYWORD: return "KEYWORD";
        case TK_STR:     return "STR";
        case TK_NUM:     return "NUM";
        case TK_GREQ:    return "GREQ";
        case TK_LTEQ:    return "LTEQ";
        case TK_EQ:      return "EQ";
        case TK_NOEQ:    return "NOEQ";
        default:
            if (ispunct(kind)) {
                sprintf(ret_buff, "PUNCT(%c)", kind);
                return ret_buff;
            }
            return "str conversion not implemented!";
    }
}

int
get_number(Token *tok)
{
    return strtoul(tok->str, NULL, 10);
}

static void
__diag_at(char *str, size_t len, size_t offset, size_t line_nr, char *file_path, char *fmt, va_list ap)
{
    char *cursor = str + offset;
    char *line_start = str_find_prev(str,       cursor, '\n');
    if (line_start != str) line_start += 1;
    char *line_end   = str_find_next(str + len, cursor, '\n');
    int column = cursor - line_start;
    int line_len = line_end - line_start;

    fprintf(stderr, "%s:%ld:%d:\n", file_path, line_nr, column + 1);
    fprintf(stderr, "  %.*s\n", line_len, line_start);
    fprintf(stderr, "  %*s^\n", column, "");
    vfprintf(stderr, fmt, ap);
}

static void
__diag_tok(Token *tok, char *fmt, va_list ap)
{
    char  *str    = tok->file->str;
    size_t len    = tok->file->len;
    size_t offset = tok->str - str;
    __diag_at(str, len, offset, tok->line_nr, tok->file->path, fmt, ap);
}

void
diag_tok(Token *tok, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char  *str    = tok->file->str;
    size_t len    = tok->file->len;
    size_t offset = tok->str - str;
    __diag_at(str, len, offset, tok->line_nr, tok->file->path, fmt, ap);
}

void
error_at(File *file, size_t offset, size_t line_nr, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    __diag_at(file->str, file->len, offset, line_nr, file->path, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

void
error_tok(Token *tok, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    __diag_tok(tok, fmt, ap);
    fprintf(stderr, "\n");
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

static int
is_keyword(char *str, size_t len)
{
    static const char *kws[] = {
        "return", "if", "else", "for", "while", "do", "int", "sizeof", "char"
    };

    for (uint i = 0; i < sizeof(kws)/sizeof(*kws); ++i) {
        if (strlen(kws[i]) == len && strncmp(str, kws[i], len) == 0) {
            return true;
        }
    }
    return false;
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

static unsigned int
read_escape_sequence(char *str, size_t *j) // Bounds checking already done.
{
    size_t i = *j;

    if (is_oct_digit(str[i])) {
        unsigned int num = 0;
        for (int k = 0; k < 3 && is_oct_digit(str[i]); ++k, ++i) { // Only three octal digis
            num = (num << 3) + (str[i] - '0');
        }

        *j = i;
        return num;
    }

    if (str[i] == 'x') {
        ++i;
        char *end;
        unsigned int num = strtoll(&str[i], &end, 16);

        *j += (end - &str[i]);
        return num;
    }


    switch (str[*j]) {
        case 'a': return '\a';
        case 'b': return '\b';
        case 't': return '\t';
        case 'n': return '\n';
        case 'v': return '\v';
        case 'f': return '\f';
        case 'r': return '\r';
        case 'e': return 27; // [GNU] \e for the ASCII escape character is a GNU C extension.
        default: return str[*j];
    }
}

static size_t
skip_string(File *file, size_t i, size_t line_nr, Buff *str_data)
{
    size_t len = file->len;
    char  *str = file->str;

    if (str[i] != '"') return i;

    size_t mark = i;
    while (++i < len && str[i] != '"' && str[i] != '\n');
    if (str[i] != '"') error_at(file, i, line_nr, "Unclosed string litteral!");

    // NOTE: We need a -1 to get size of string between the '"' and a +1 for null termination!
    // We also might allocated a bit over because of escape sequences.
    char *data = calloc(i - mark, sizeof(char));

    size_t data_len = 0;
    for (size_t j = mark + 1; j < i; ++j) {
        if (str[j] == '"') break;

        if (str[j] == '\\') {
            ++j;
            data[data_len++] = read_escape_sequence(str, &j);
            ++j;
        }

        data[data_len++] = str[j];
    }
    data[data_len++] = '\0';

    str_data->data = data;
    str_data->len = data_len;

    assert(data_len <= i - mark);
    // printf("data_len = %lu, i - mark = %lu\n", data_len, i - mark);

    return i + 1; // Consume '"'
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

    Buff str_data;

    Token head = {0};
    Token *tok = &head;

    size_t i = 0;
    for (; i < len;) {
        if (isspace(str[i]) || str[i] == '\0') {
            if (str[i] == '\n') line_nr += 1;
            ++i;
            continue;
        }

        size_t mark = i;
        TokenKind kind;

        if ((i = skip_id(file, i, line_nr)) != mark) {
            kind = TK_ID;
            if (is_keyword(str + mark, i - mark)) kind = TK_KEYWORD;
        }
        else if ((i = skip_num   (file, i, line_nr))           != mark) { kind = TK_NUM; }
        else if ((i = skip_string(file, i, line_nr, &str_data)) != mark) { kind = TK_STR; }
        else if ((i = skip_rel   (file, i, line_nr, &kind))    != mark) {}
        else if (ispunct(str[i])) { kind = str[i++]; }
        else {
            error_at(file, i, line_nr, "Unknown Syntax!");
        }

        tok = tok->next = new_tok(kind, str + mark, i - mark, file, line_nr);
        if (kind == TK_STR) tok->str_data = str_data;
    }

    tok = tok->next = new_tok(TK_EOF, str + i, 0, file, line_nr);

    return head.next;
}
