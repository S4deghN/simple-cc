#ifndef SRC_STRING_H
#define SRC_STRING_H


typedef struct {
    size_t len;
    char *str;
} StringView;

void sv_trim_left(StringView *sv) {
    while (sv->len && isspace(*sv->str)) {
        --sv->len;
        ++sv->str;
    }
}

void sv_trim_right(StringView *sv) {
    char *end = sv->str + sv->len - 1;
    while (sv->len && isspace(*end)) {
        --sv->len;
        --end;
    }
}

void sv_trim(StringView *sv) {
    sv_trim_left(sv);
    sv_trim_right(sv);
}

StringView sv_chop_by_delim(StringView *sv, char c) {
    StringView chop = *sv;
    while (sv->len && *sv->str != c) {
        --sv->len;
        ++sv->str;
    }
    chop.len -= sv->len;

    int delim_skip = (sv->len > 0);
    sv->len -= delim_skip;
    sv->str += delim_skip;

    return chop;
}

StringView sv_chop_by_type(StringView *sv, int (*istype)(int)) {
    StringView chop = *sv;
    while (sv->len && !istype(*sv->str)) {
        --sv->len;
        ++sv->str;
    }
    int success = (sv->len > 0);
    sv->len -= success;
    sv->str += success;

    chop.len -= sv->len;

    return chop;
}

void sv_drop(StringView *sv, size_t n) {
    if (n > sv->len) n = sv->len;
    sv->len -= n;
    sv->str += n;
}

static int
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

#endif
