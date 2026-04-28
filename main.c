#include "cc.h"

#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char *argv[]) {

    if (argc != 2) error("%s: invalid number of arguments!", argv[0]);

    File file = read_entire_file(argv[1]);

    Token *tk = tokenize(&file);

    Obj *prog = parse(tk);

    codegen(prog);

    return 0;
}
