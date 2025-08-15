/* required to include shad_compile() and serialization functions */
#define SHAD_COMPILER
#include "cm_shader.h"
#include "cm_shader.c"

#include <stdio.h>

int main(int argc, char const *argv[]) {
    /* compile */
    ShadResult sc;
    shad_compile("triangle.shader", SHAD_OUTPUT_FORMAT_SDL, &sc);

    /* serialize to C code */
    char *c_code;
    size_t c_code_len;
    shad_serialize_to_c(&sc, &c_code, &c_code_len);

    /* write to a header that we'll include in main.c */
    FILE *output = fopen("precompiled.h", "wb");
    if (!output) return fprintf(stderr, "Failed to create file precompiled.h\n"), 1;
    fputs(c_code, output);
    fclose(output);

    return 0;
}
