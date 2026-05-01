#include "cc.h"

static char *opt_o;
static bool opt_E;
static char *input_path;

static void
usage(int status, char *prog_name)
{
    fprintf(stderr, "%s [ -o <path> ] <file>\n", prog_name);
    exit(status);
}

static void
parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help"))
            usage(0, argv[0]);

        if (!strcmp(argv[i], "-o")) {
            if (!argv[++i])
                usage(1, argv[0]);
            opt_o = argv[i];
            continue;
        }

        if (!strncmp(argv[i], "-o", 2)) {
            opt_o = argv[i] + 2;
            continue;
        }

        if (!strncmp(argv[i], "-E", 2)) {
            opt_E = true;
            continue;
        }

        if (argv[i][0] == '-' && argv[i][1] != '\0')
            error("unknown argument: %s", argv[i]);

        input_path = argv[i];
    }

    if (!input_path)
        error("no input files");
}

static FILE *
open_file(char *path)
{
    if (!path || strcmp(path, "-") == 0)
        return stdout;

    FILE *out = fopen(path, "w");
    if (!out)
        error("cannot open output file: %s: %s", path, strerror(errno));
    return out;
}

void
preproc(char *input_path, File *file)
{
    assert(run_cmd((char*[]){"cc", "-E", "-P", "-C", input_path, NULL}, file) == 0);
    file->path = input_path;
}

int main(int argc, char *argv[]) {

    parse_args(argc, argv);

    FILE *out = open_file(opt_o);

    // File file = read_entire_file(input_path);
    File file;

    preproc(input_path, &file);
    if (opt_E) {
        fwrite(file.str, 1, file.len, out);
        exit(0);
    }

    Token *tok = tokenize(&file);

    Obj *prog = parse(tok);

    codegen(prog, out);

    return 0;
}
