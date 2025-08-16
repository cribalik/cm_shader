#define SHAD_COMPILER
#include "../shad.h"
#include "../shad.c"

#include <stdlib.h>
#include <stdio.h>

int streq(const char *a, const char *b) {
    return !strcmp(a,b);
}

void print_usage(const char **argv) {
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "# Outputs C to stdout for SDL\n");
    fprintf(stderr, "shad_cli.exe --output_c --sdl triangle.shader mesh.shader > my_shaders.h\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "# Outputs binary to stdout for SDL\n");
    fprintf(stderr, "shad_cli.exe --output_binary --sdl triangle.shader mesh.shader > my_shaders.bin\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "# Outputs C to file for SDL\n");
    fprintf(stderr, "shad_cli.exe --output_c --sdl triangle.shader mesh.shader --output my_shaders.h\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "# Outputs binary to file for SDL\n");
    fprintf(stderr, "shad_cli.exe --output_binary --sdl triangle.shader mesh.shader --output my_shaders.bin\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "-h, --help: Print help\n");
    fprintf(stderr, "-c, --output_c: Output to C code\n");
    fprintf(stderr, "-b, --output_binary: Output to binary\n");
    fprintf(stderr, "-o FILE, --output FILE: Output to file (if not specified, outputs to stdout)\n");
}

void get_filename(const char *path, const char **filename_out, size_t *filename_len_out) {
    const char *p = path;
    const char *end = path + strlen(path);
    const char *e = end;
    while (e > p && *e != '.') --e;
    if (e == p) e = end;
    const char *f = e;
    while (f > p && *f != '/' && *f != '\\') --f;
    if (f > p) ++f;
    size_t len = e-f;
    char *result = malloc(len+1);
    memcpy(result, f, len);
    result[len] = 0;
    *filename_out = result;
    *filename_len_out = len;
}

int main(int argc, const char **argv) {
    int help = 0;
    enum {
        OUTPUT_TYPE_INVALID,
        OUTPUT_TYPE_C,
        OUTPUT_TYPE_BINARY,
    } output_type;

    const char *output_file = NULL;
    ShadOutputFormat output_format = 0;

    typedef struct InputFile {
        struct InputFile *next;
        const char *file;
        ShadResult result;
    } InputFile;

    InputFile *files = NULL;
    InputFile **curr_file = &files;

    for (const char **argp = argv+1; *argp; ++argp) {
        const char *arg = *argp;
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
        else if (streq(arg, "--output_c") || streq(arg, "-c")) {
            if (output_type && output_type != OUTPUT_TYPE_C) {
                fprintf(stderr, "Error: %s conflicts with previously specified output format\n\n", arg);
                print_usage(argv);
                return 1;
            }
            output_type = OUTPUT_TYPE_C;
        }
        else if (streq(arg, "--output_binary") || streq(arg, "-b")) {
            if (output_type && output_type != OUTPUT_TYPE_BINARY) {
                fprintf(stderr, "Error: %s conflicts with previously specified output format\n\n", arg);
                print_usage(argv);
                return 1;
            }
            output_type = OUTPUT_TYPE_BINARY;
        }
        else if (streq(arg, "--sdl") || streq(arg, "-sdl")) {
            if (output_format && output_format != SHAD_OUTPUT_FORMAT_SDL) {
                fprintf(stderr, "Error: %s conflicts with previously specified output format\n\n", arg);
                print_usage(argv);
                return 1;
            }
            output_format = SHAD_OUTPUT_FORMAT_SDL;
        }
        else {
            if (arg[0] == '-') {
                fprintf(stderr, "Unknown option: '%s'\n\n", arg);
                print_usage(argv);
                return 1;
            }

            InputFile *file = malloc(sizeof(InputFile));
            memset(file, 0, sizeof(*file));
            file->file = arg;
            file->next = *curr_file;
            *curr_file = file;
        }
    }

    /* print help */
    if (help) {
        print_usage(argv);
        return 0;
    }

    /* output types must be specified */
    if (!output_type) {
        fprintf(stderr, "Error: Output type must be specified (--output_c or --output_binary)\n\n");
        print_usage(argv);
        return 1;
    }

    /* output format must be specified */
    if (!output_format) {
        fprintf(stderr, "Error: Output format must be specified (e.g. --sdl for SDL3)\n\n");
        print_usage(argv);
        return 1;
    }

    /* we need output files */
    if (!files) {
        fprintf(stderr, "Error: No output files specified\n\n");
        print_usage(argv);
        return 1;
    }

    /* sanity check that people don't use multiple files with the same filename when using C mode */
    if (output_type == OUTPUT_TYPE_C) {
        for (InputFile *f = files; f; f = f->next) {
            const char *fname;
            size_t fname_len;
            get_filename(f->file, &fname, &fname_len);
            for (InputFile *f2 = files; f2; f2 = f2->next) {
                if (f2 == f) continue;
                const char *f2name;
                size_t f2name_len;
                get_filename(f2->file, &f2name, &f2name_len);
                if (fname_len == f2name_len && memcmp(fname, f2name, fname_len) == 0) {
                    fprintf(stderr, "Error: Duplicate filenames:\n%s\n%s\nThis will cause name collisions in C output.", f->file, f2->file);
                    return 1;
                }
            }
        }
    }

    /* compile */
    for (InputFile *f = files; f; f = f->next) {
        if (!shad_compile(f->file, output_format, &f->result)) {
            return 1;
        }
    }

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
    for (InputFile *f = files; f; f = f->next) {
        char *code;
        size_t len;
        if (output_type == OUTPUT_TYPE_C) {
            const char *fname;
            size_t fname_len;
            get_filename(f->file, &fname, &fname_len);
            shad_serialize_to_c(&f->result, fname, &code, &len);
        }
        else
            shad_serialize(&f->result, &code, &len);
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