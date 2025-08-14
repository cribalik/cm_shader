#include "cm_shader.h"
#include "cm_shader.c"
#include <assert.h>

int main(int argc, char const *argv[]) {
    SC_Result sc;
    int res;

    assert(!sc_compile("instancing.shader", SC_OUTPUT_FORMAT_SDL, &sc));

    assert(sc_compile("kitchensink.shader", SC_OUTPUT_FORMAT_SDL, &sc));
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

    assert(sc_compile("import.shader", SC_OUTPUT_FORMAT_SDL, &sc));
    assert(!sc.has_fragment_shader);
    assert(sc.num_vertex_samplers == 1);
    assert(sc.num_vertex_buffers == 1);

    fprintf(stderr, "\n\nTests passed!");
    return 0;
}
