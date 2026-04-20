#include "cc.h"

#include <assert.h>

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
