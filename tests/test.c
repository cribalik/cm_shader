#define SHAD_COMPILER
#define SHAD_RUNTIME
#include "shad.h"
#include "shad.c"
#include <assert.h>

int main(int argc, char const *argv[]) {
    ShadResult sc;
    int res;

    assert(!shad_compile("instancing.shader", SHAD_OUTPUT_FORMAT_SDL, &sc));

    assert(shad_compile("kitchensink.shader", SHAD_OUTPUT_FORMAT_SDL, &sc));
    assert(sc.num_vertex_input_buffers == 2);
    assert(sc.num_vertex_inputs == 3);
    assert(sc.vertex_input_buffers[0].slot == 0);
    assert(sc.vertex_input_buffers[1].slot == 3);
    assert(sc.num_vertex_samplers == 1);
    assert(sc.num_vertex_buffers == 1);
    assert(sc.num_vertex_images == 1);
    assert(sc.num_vertex_uniforms == 1);
    assert(sc.num_fragment_samplers == 1);
    assert(sc.num_fragment_buffers == 1);
    assert(sc.num_fragment_images == 1);
    assert(sc.num_fragment_uniforms == 1);

    assert(shad_compile("import.shader", SHAD_OUTPUT_FORMAT_SDL, &sc));
    assert(!sc.has_fragment_shader);
    assert(sc.num_vertex_samplers == 1);
    assert(sc.num_vertex_buffers == 1);

    fprintf(stderr, "\n\nTests passed!");
    return 0;
}
