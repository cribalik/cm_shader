/* required to include shad_compile() and serialization functions */
#define SHAD_COMPILER
#include "shad.h"

#include <stdio.h>

int main(int argc, char const *argv[]) {
    /* compile */
    ShadResult sc;
    shad_compile("triangle.shader", SHAD_OUTPUT_FORMAT_SDL, &sc);

    /* serialize to binary */
    char *bytes;
    int bytes_len;
    shad_serialize(&sc, &bytes, &bytes_len);

    /* write bytes to a header that we'll include in main.c */
    FILE *output = fopen("precompiled.h", "wb");
    if (!output) return fprintf(stderr, "Failed to create file precompiled.h\n"), 1;
    fprintf(output, "char precompiled_bytes[] = {");
    for (int i = 0; i < bytes_len; ++i)
        fprintf(output, "%i,", (int)bytes[i]);
    fprintf(output, "};\nsize_t precompiled_num_bytes = %i;\n", (int)bytes_len);
    fclose(output);

    return 0;
}

#include "shad.c"