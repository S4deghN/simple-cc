#ifndef SRC_LEXER_H
#define SRC_LEXER_H

#include <stddef.h>

typedef enum {
    TK_EOF,
    TK_ERR,
    TK_IDENT,
    TK_KEYWORD,
    TK_NUMBER,
    TK_OPEN_CURLY,
    TK_CLOSE_CURLY,
    TK_OPEN_PAREN,
    TK_CLOSE_PAREN,
    TK_SEMICOLON,
    TK_COMMA,
    TK_PLUS,
    TK_MINUS,
} TokenKind;

char *tk_kind_str(TokenKind kind) {
    switch (kind) {
        case TK_EOF: return "EOF";
        case TK_ERR: return "ERR";
        case TK_IDENT: return "IDENT";
        case TK_KEYWORD: return "KEYWORD";
        case TK_NUMBER: return "NUMBER";
        case TK_OPEN_CURLY: return "OPEN_CURLY";
        case TK_CLOSE_CURLY: return "CLOSE_CURLY";
        case TK_OPEN_PAREN: return "OPEN_PAREN";
        case TK_CLOSE_PAREN: return "CLOSE_PAREN";
        case TK_SEMICOLON: return "SEMICOLON";
        case TK_COMMA: return "COMMA";
        case TK_PLUS: return "PLUS";
        case TK_MINUS: return "MINUS";
        default: return "str conversion no implemented!";
    }
}

typedef struct {
    TokenKind kind;
    char *str;
    size_t len;
} Token;

typedef struct {
    char *buf;
    size_t buf_size;
    size_t cursor;
    size_t mark;
    size_t line_start;
    size_t line_nr;
    char *source_path;
} Lexer;

void lexer_init(Lexer *l, char *buf, size_t buf_size, char *source_path);
Token lexer_next(Lexer *l);
void lexer_report_line(Lexer *l);

#endif

#ifdef LEXER_IMPLEMENTATION

#include "base.h"

#include <ctype.h>
#include <string.h>

static bool
is_c_ident_start(char c)
{
    if (isalpha(c) || c == '_') {
        return true;
    }
    return false;
}

static bool
is_c_ident(char c)
{
    if (is_c_ident_start(c) || isdigit(c)) {
        return true;
    }
    return false;
}

static bool
is_c_digit(char c)
{
    if (isdigit(c)) {
        return true;
    }
    return false;
}

static bool
is_c_hex_digit(char c)
{
    if (isxdigit(c)) {
        return true;
    }
    return false;
}

static bool
is_c_bin_digit(char c)
{
    if (c == '0' || c == '1') {
        return true;
    }
    return false;
}

static bool
is_c_oct_digit(char c)
{
    if ('0' <= c && c <= '7') {
        return true;
    }
    return false;
}

static bool
is_c_boundry(char c)
{
    if (isspace(c) || ispunct(c) || c == '\0') {
        return true;
    }
    return false;
}

static bool
is_str_keyword(char *str, size_t len)
{
    const char *c_keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline",
        "int", "long", "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
        "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
    };

    for (uint i = 0; i < array_len(c_keywords); ++i) {
        if (strncmp(str, c_keywords[i], len) == 0) {
            return true;
        }
    }
    return false;
}

static int
str_find_next_newline(char *str, size_t len)
{
    size_t i = 0;
    for (; i < len; ++i) {
        if (str[i] == '\n') break;
    }
    return i;
}

void
lexer_init(Lexer *l, char *buf, size_t buf_size, char *source_path) {
    memset(l, 0, sizeof(*l));
    l->line_nr = 1;
    l->buf = buf;
    l->buf_size = buf_size;
    l->source_path = source_path;
}

#define RET(...) do { ret = (Token){ __VA_ARGS__ }; goto finish; } while(0)

Token
lexer_next(Lexer *l) {
    Token ret;
    char *buf = l->buf;
    size_t buf_size = l->buf_size;
    size_t cursor = l->cursor;

    if (cursor >= buf_size) RET(.kind = TK_EOF);

    while(isspace(buf[cursor])) {
        if (buf[cursor] == '\n' && cursor + 1 < buf_size) {
            l->line_start = cursor + 1;
            l->line_nr += 1;
        }
        cursor += 1;
        if (cursor >= buf_size) RET(.kind = TK_EOF);
    }

    size_t mark = cursor;

    if (is_c_ident_start(buf[cursor])) { // Identifiers and keywords
        while (++cursor < buf_size && is_c_ident(buf[cursor]));
        if (!is_c_boundry(buf[cursor])) RET(.kind = TK_ERR);
        if (is_str_keyword(&buf[mark], cursor - mark)) RET(.kind = TK_KEYWORD);
        RET(.kind = TK_IDENT);
    } else if (is_c_digit(buf[cursor])) { // Numbers
        if (buf[cursor] != '0') { // Regular number
            while (++cursor < buf_size && is_c_digit(buf[cursor]));
            if (!is_c_boundry(buf[cursor])) RET(.kind = TK_ERR, .str = "Bad formed number!");
            RET(.kind = TK_NUMBER);
        }
        if (++cursor >= buf_size) RET(.kind = TK_NUMBER);
        if (buf[cursor] == 'x' || buf[cursor] == 'X') { // Hex numbers
            if (++cursor >= buf_size || !is_c_hex_digit(buf[cursor])) goto hex_err;
            while (++cursor < buf_size && is_c_hex_digit(buf[cursor]));
            if (!is_c_boundry(buf[cursor])) goto hex_err;
            RET(.kind = TK_NUMBER);
        hex_err:
            RET(.kind = TK_ERR, .str = "Bad formed hex number!");
        }
        if (buf[cursor] == 'b' || buf[cursor] == 'B') { // Binary numbers
            if (++cursor >= buf_size || !is_c_bin_digit(buf[cursor])) goto bin_err;
            while (++cursor < buf_size && is_c_bin_digit(buf[cursor]));
            if (!is_c_boundry(buf[cursor])) goto bin_err;
            RET(.kind = TK_NUMBER);
        bin_err:
            RET(.kind = TK_ERR, .str = "Bad formed binary number!");
        }
        if (is_c_oct_digit(buf[cursor])) { // Octal numbers
            while (++cursor < buf_size && is_c_oct_digit(buf[cursor]));
            if (!is_c_boundry(buf[cursor])) RET(.kind = TK_ERR, .str = "Bad formed octal number!");;
            RET(.kind = TK_NUMBER);
        }
    } else if (buf[cursor] == '{') {
        ++cursor;
        ret.kind = TK_OPEN_CURLY;
    } else if (buf[cursor] == '}') {
        ++cursor;
        ret.kind = TK_CLOSE_CURLY;
    } else if (buf[cursor] == '(') {
        ++cursor;
        ret.kind = TK_OPEN_PAREN;
    } else if (buf[cursor] == ')') {
        ++cursor;
        ret.kind = TK_CLOSE_PAREN;
    } else if (buf[cursor] == ';') {
        ++cursor;
        ret.kind = TK_SEMICOLON;
    } else if (buf[cursor] == ',') {
        ++cursor;
        ret.kind = TK_COMMA;
    } else if (buf[cursor] == '+') {
        ++cursor;
        ret.kind = TK_PLUS;
    } else if (buf[cursor] == '-') {
        ++cursor;
        ret.kind = TK_MINUS;
    } else {
        ++cursor;
        ret.kind = TK_ERR;
        ret.str = "Unknown symbol!";
        ret.len = strlen(ret.str);
    }

finish:
    l->cursor = cursor;
    l->mark = mark;

    if (ret.kind > TK_ERR) {
        ret.str = &buf[mark];
        ret.len = cursor - mark;
    }

    if (ret.kind == TK_ERR) {
        lexer_report_line(l);
        fprintf(stderr, "Syntax Error: %s\n", ret.str);
        exit(1);
    }

    return ret;
}

void
lexer_report_line(Lexer *l)
{
    int column = l->mark - l->line_start;
    int region_len = l->cursor - column;
    char *line = &l->buf[l->line_start];
    int line_len = str_find_next_newline(line, l->buf_size - l->line_start);

    fprintf(stderr, "%s:%ld:%d:\n", l->source_path, l->line_nr, column);
    fprintf(stderr, "  %.*s\n", line_len, line);
    fprintf(stderr, "  %*.s^%*.s^\n", column, "", region_len - 1, "");
    // fprintf(stderr, "column = %d, l->cursor = %d, l->mark = %d, region_len = %d\n",
    //     column, l->cursor, l->mark, region_len);
}
#endif
