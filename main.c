#include "cc.h"

#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

int main(int argc, char *argv[]) {

    if (argc != 2) error("%s: invalid number of arguments!", argv[0]);

    Token *tk;

    int fd = open(argv[1], O_RDONLY);
    if (fd != -1) {
        struct stat s;
        assert(fstat(fd, &s) == 0);

        size_t file_size = s.st_size;
        char *file_data = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        assert(file_data != MAP_FAILED);

        tk = tokenize(&(File){ .str = file_data, .len = file_size, .path = argv[1] });
    } else {
        tk = tokenize(&(File){ .str = argv[1], .len = strlen(argv[1]), .path = "argv[1]" });
    }


    Obj *prog = parse(tk);

    // for (Ident *g = prog; g; g = g->next) {
    //     if (g->body) print_tree(g->body, "  // ");
    // }

    codegen(prog);
    return 0;
}
