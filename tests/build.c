#include <SDL3/SDL.h>
#define SHAD_COMPILER
#define SHAD_RUNTIME
#include "shad.h"
#include "shad.c"

#include <stdio.h>

int main(int argc, char const *argv[]) {
    ShadCompilation sc;
    char *c_code;
    int c_code_len;
    FILE *output;

    shad_compile("kitchensink.shader", SHAD_OUTPUT_FORMAT_SDL, &sc);
    shad_sdl_serialize_to_c(&sc, "kitchensink", &c_code, &c_code_len);

    output = fopen("kitchensink.h", "wb");
    fputs(c_code, output);
    fclose(output);

    return 0;
}
