#include "cc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

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
