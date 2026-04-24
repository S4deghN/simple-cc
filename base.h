#ifndef SRC_BASE_H
#define SRC_BASE_H

#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

#define array_len(a) (sizeof(a)/sizeof(a[0]))

#define DA_INIT_CAP 256

#define reserve(list, minimum_cap) do {                                             \
    if ((list)->capacity == 0) {                                                    \
        (list)->capacity = DA_INIT_CAP;                                             \
    }                                                                               \
    while ((minimum_cap) > (list)->capacity) {                                      \
        (list)->capacity *= 2;                                                      \
    }                                                                               \
    (list)->data = realloc((list)->data, (list)->capacity * sizeof(*(list)->data)); \
    assert((list)->data != NULL);                                                   \
} while(0)

#define append_many(list, items, n) do {                                    \
    reserve((list), (list)->count + n);                                     \
    memcpy((list)->data + (list)->count, items, n * sizeof(*(list)->data)); \
    (list)->count += n;                                                     \
} while(0)

#define append(list, item) do {           \
    reserve((list), (list)->count + 1);   \
    (list)->data[(list)->count++] = item; \
} while(0)

#define resize(list, size) do { \
    reserve(list, size);        \
    (list)->count = size;       \
} while(0)

typedef struct {
    char *data;
    size_t count;
    size_t capacity;
} Buff;

int run_cmd(char *argv[], Buff *out_buff);

#endif

#ifdef BASE_IMPLEMENTATION

int run_cmd(char *argv[], Buff *out_buff) {
    char buff[1024];
    char *p = buff;
    for (int i = 0; argv[i] != NULL; ++i) {
        p = stpcpy(p, argv[i]);
        p = stpcpy(p, " ");
    }

    int n;
    FILE *f = popen(buff, "r");
    if (!f) {
        perror("popen: ");
        return -1;
    }

    while((n = fread(buff, 1, sizeof(buff), f))) {
        append_many(out_buff, buff, n);
    }

    int err = pclose(f);
    if (err == -1) {
        perror("pclose: ");
    }

    return 0;
}

#endif

