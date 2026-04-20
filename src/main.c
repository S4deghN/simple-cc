#include "cc.h"

#include <string.h>

int main(int argc, char *argv[]) {

    if (argc != 2) error("%s: invalid number of arguments!", argv[0]);

    Token *tk = tokenize(&(File){ .str = argv[1], .len = strlen(argv[1]), .path = "argv[1]" });
    Function *prog = parse(tk);
    codegen(prog);
    return 0;
}
