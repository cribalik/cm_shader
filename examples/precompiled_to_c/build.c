/* required to include shad_compile() and serialization functions */
#define SHAD_COMPILER
#include "shad.h"
#include "shad.c"

#include <stdio.h>

int main(int argc, char const *argv[]) {
    /* compile */
    ShadCompilation sc;
    shad_compile("triangle.shader", SHAD_OUTPUT_FORMAT_SDL, &sc);

    /* serialize to C code */
    char *c_code;
    int c_code_len;
    shad_sdl_serialize_to_c(&sc, "triangle", &c_code, &c_code_len);

    /* write to a header that we'll include in main.c */
    FILE *output = fopen("precompiled.h", "wb");
    if (!output) return fprintf(stderr, "Failed to create file precompiled.h\n"), 1;
    fputs(c_code, output);
    fclose(output);

    return 0;
}
