int assert(int expected, int actual, char *code);
int printf(char *fmt);

#define ASSERT(x, y) assert(x, y, #y)
