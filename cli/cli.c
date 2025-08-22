#include "../shad.h"
#include "../shad.c"

#include <stdlib.h>
#include <stdio.h>

int streq(const char *a, const char *b) {
    return !strcmp(a,b);
}

void print_usage(char **argv) {
    fprintf(stderr, "Usage: shad FRAMEWORK [OPTIONS] FILES...\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Compile shaders to C code for a given framework.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "EXAMPLES:\n");
    fprintf(stderr, "    # Outputs C code to stdout for SDL3\n");
    fprintf(stderr, "    shad sdl3 triangle.shader mesh.shader > my_shaders.h\n");
    fprintf(stderr, "    # Outputs C code to file for SDL3\n");
    fprintf(stderr, "    shad sdl3 triangle.shader mesh.shader -o my_shaders.h\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "FRAMEWORK:\n");
    fprintf(stderr, "    sdl3: SDL3\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "    -h, --help: Print help\n");
    fprintf(stderr, "    -o FILE, --output FILE: Output to file (if not specified, outputs to stdout)\n");
}

void get_filename(char *path, char **filename_out, int *filename_len_out) {
    char *p = path;
    char *end = path + strlen(path);
    char *e = end;
    while (e > p && *e != '.') --e;
    if (e == p) e = end;
    char *f = e;
    while (f > p && *f != '/' && *f != '\\') --f;
    if (f > p) ++f;
    int len = e-f;
    char *result = (char*)malloc(len+1);
    memcpy(result, f, len);
    result[len] = 0;
    *filename_out = result;
    *filename_len_out = len;
}

int main(int argc, char **argv) {
    typedef struct InputFile {
        char *file;
        ShadCompilation result;
    } InputFile;

    int help = 0;
    char *output_file = NULL;
    ShadOutputFormat output_format = SHAD_OUTPUT_FORMAT_INVALID;
    InputFile *files = (InputFile*)malloc(sizeof(InputFile) * argc);
    int num_files = 0;

    if (argc < 2) {
        fprintf(stderr, "Error: No framework specified\n\n");
        print_usage(argv);
        return 1;
    }

    /* parse framework */
    if (streq(argv[1], "sdl3"))
        output_format = SHAD_OUTPUT_FORMAT_SDL;
    else {
        fprintf(stderr, "Error: Unknown framework: %s\n\n", argv[1]);
        print_usage(argv);
        return 1;
    }

    /* parse options and files */
    for (char **argp = argv+2; *argp; ++argp) {
        char *arg = *argp;
        if      (streq(arg, "--help") || streq(arg, "-h")) help = 1;
        else if (streq(arg, "--output") || streq(arg, "-o")) {
            if (output_file) {
                fprintf(stderr, "Error: Output file specified more than once\n\n");
                print_usage(argv);
                return 1;
            }
            if (!argp[1]) {
                fprintf(stderr, "Error: %s must be followed by an output file\n\n", arg);
                print_usage(argv);
                return 1;
            }
            output_file = argp[1];
            ++argp;
        }
        else {
            if (arg[0] == '-') {
                fprintf(stderr, "Unknown option: '%s'\n\n", arg);
                print_usage(argv);
                return 1;
            }

            InputFile *file = &files[num_files++];
            memset(file, 0, sizeof(*file));
            file->file = arg;
        }
    }

    /* print help */
    if (help) {
        print_usage(argv);
        return 0;
    }

    /* output format must be specified */
    if (!output_format) {
        fprintf(stderr, "Error: Output format must be specified (e.g. --framework sdl for SDL3)\n\n");
        print_usage(argv);
        return 1;
    }

    /* we need output files */
    if (!num_files) {
        fprintf(stderr, "Error: No output files specified\n\n");
        print_usage(argv);
        return 1;
    }

    /* sanity check that people don't use multiple files with the same filename */
    for (int i = 0; i < num_files; ++i) {
        char *fname;
        int fname_len;
        get_filename(files[i].file, &fname, &fname_len);
        for (int j = 0; j < num_files; ++j) {
            if (j == i) continue;
            char *f2name;
            int f2name_len;
            get_filename(files[j].file, &f2name, &f2name_len);
            if (fname_len == f2name_len && memcmp(fname, f2name, fname_len) == 0) {
                fprintf(stderr, "Error: Duplicate filenames:\n%s\n%s\nThis will cause name collisions in output.", files[i].file, files[j].file);
                return 1;
            }
        }
    }

    /* compile */
    for (int i = 0; i < num_files; ++i)
        if (!shad_compile(files[i].file, output_format, &files[i].result))
            return 1;

    /* open output file */
    FILE *out = stdout;
    if (output_file) {
        out = fopen(output_file, "wb");
        if (!out) {
            fprintf(stderr, "Failed to open output file %s\n", output_file);
            return 1;
        }
    }

    /* write out the result */
    int num_compiled = 0;
    for (int i = 0; i < num_files; ++i) {
        char *code;
        int len;
        switch (output_format) {
            case SHAD_OUTPUT_FORMAT_SDL: {
                char *fname;
                int fname_len;
                get_filename(files[i].file, &fname, &fname_len);
                shad_sdl_serialize_to_c(&files[i].result, fname, &code, &len);
                break;
            }
            default: {
                fprintf(stderr, "Error: Unknown output format: %i\n", output_format);
                return 1;
            }
        }
        fwrite(code, 1, len, out);
        ++num_compiled;
    }

    if (ferror(out)) {
        fprintf(stderr, "Error: Failure writing to %s\n", output_file ? output_file : "stdout");
        goto err;
    }

    fprintf(stderr, "Successfully compiled %i shaders\n", num_compiled);

    /* this should happen when the process exits, but maybe this will free the output file a bit faster */
    if (output_file)
        fclose(out);
    return 0;

    /* this should happen when the process exits, but maybe this will free the output file a bit faster */
    err:
    if (output_file)
        fclose(out);
    return 1;
}