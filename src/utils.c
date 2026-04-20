#include "cc.h"

#include <assert.h>

char *
str_find_next(char *str_end, char *cursor, char c)
{
    assert(cursor <= str_end);

    while (cursor++ < str_end) {
        if (*cursor == c) break;
    }
    return cursor;
}

char *
str_find_prev(char *str_start, char *cursor, char c)
{
    assert(cursor >= str_start);

    while (cursor-- > str_start) {
        if (*cursor == c) break;
    }
    return cursor;
}
