/* required to include shad_compile() and serialization functions */
#define SHAD_COMPILER
#include "shad.h"
#include "shad.c"

#include <stdio.h>

int main(int argc, char const *argv[]) {
    /* compile */
    ShadCompilation sc;
    shad_compile("triangle.shader", SHAD_OUTPUT_FORMAT_SDL, &sc);

    /* serialize to binary */
    char *bytes;
    int bytes_len;
    shad_compilation_serialize(&sc, &bytes, &bytes_len);

    /* write bytes to a header that we'll include in main.c */
    FILE *output = fopen("precompiled.bin", "wb");
    if (!output) return fprintf(stderr, "Failed to create file precompiled.bin\n"), 1;
    fwrite(bytes, 1, bytes_len, output);
    fclose(output);

    return 0;
}
