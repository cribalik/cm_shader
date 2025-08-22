#include <SDL3/SDL.h>
#define SHAD_COMPILER
#define SHAD_RUNTIME
#include "shad.h"
#include "shad.c"
#include <assert.h>

#include "kitchensink.h"

#define ASSERT_EQ_INT(a, b) do {int _a = (a); int _b = (b); if (_a != _b) {fprintf(stderr, "Assertion failed: %s != %s (%i != %i)\n", #a, #b, _a, _b); return 1;}} while (0)

int main(int argc, char const *argv[]) {
    ShadCompilation sc;
    int res;

    assert(!shad_compile("instancing.shader", SHAD_OUTPUT_FORMAT_SDL, &sc));

    assert(shad_compile("kitchensink.shader", SHAD_OUTPUT_FORMAT_SDL, &sc));
    ASSERT_EQ_INT(sc.num_vertex_input_buffers, 2);
    ASSERT_EQ_INT(sc.num_vertex_inputs, 3);
    ASSERT_EQ_INT(sc.vertex_input_buffers[0].slot, 0);
    ASSERT_EQ_INT(sc.vertex_input_buffers[1].slot, 3);
    ASSERT_EQ_INT(sc.num_vertex_samplers, 1);
    ASSERT_EQ_INT(sc.num_vertex_buffers, 1);
    ASSERT_EQ_INT(sc.num_vertex_images, 1);
    ASSERT_EQ_INT(sc.num_vertex_uniforms, 1);
    ASSERT_EQ_INT(sc.num_fragment_samplers, 1);
    ASSERT_EQ_INT(sc.num_fragment_buffers, 1);
    ASSERT_EQ_INT(sc.num_fragment_images, 1);
    ASSERT_EQ_INT(sc.num_fragment_uniforms, 1);
    ASSERT_EQ_INT(sc.num_vertex_input_buffers, 2);

    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.num_vertex_buffers, 2);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.num_vertex_attributes, 3);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_buffer_descriptions[0].slot, 0);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_buffer_descriptions[1].slot, 3);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_buffer_descriptions[0].pitch, 16);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_buffer_descriptions[1].pitch, 12);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_buffer_descriptions[0].input_rate, SDL_GPU_VERTEXINPUTRATE_VERTEX);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_buffer_descriptions[1].input_rate, SDL_GPU_VERTEXINPUTRATE_INSTANCE);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_attributes[0].location, 0);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_attributes[1].location, 1);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_attributes[2].location, 2);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_attributes[0].buffer_slot, 0);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_attributes[1].buffer_slot, 3);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_attributes[2].buffer_slot, 0);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_attributes[0].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_attributes[1].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.vertex_input_state.vertex_attributes[2].format, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.target_info.num_color_targets, 3);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.target_info.color_target_descriptions[0].format, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.target_info.color_target_descriptions[1].format, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.target_info.color_target_descriptions[2].format, SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.target_info.depth_stencil_format, SDL_GPU_TEXTUREFORMAT_D24_UNORM);
    ASSERT_EQ_INT(shad_sdl_pipeline_kitchensink.target_info.has_depth_stencil_target, true);
    ASSERT_EQ_INT(shad_sdl_fragment_shader_kitchensink.num_samplers, 1);
    ASSERT_EQ_INT(shad_sdl_fragment_shader_kitchensink.num_storage_buffers, 1);
    ASSERT_EQ_INT(shad_sdl_fragment_shader_kitchensink.num_storage_textures, 1);
    ASSERT_EQ_INT(shad_sdl_fragment_shader_kitchensink.num_uniform_buffers, 1);
    ASSERT_EQ_INT(shad_sdl_vertex_shader_kitchensink.num_samplers, 1);
    ASSERT_EQ_INT(shad_sdl_vertex_shader_kitchensink.num_storage_buffers, 1);
    ASSERT_EQ_INT(shad_sdl_vertex_shader_kitchensink.num_storage_textures, 1);
    ASSERT_EQ_INT(shad_sdl_vertex_shader_kitchensink.num_uniform_buffers, 1);

    assert(shad_compile("import.shader", SHAD_OUTPUT_FORMAT_SDL, &sc));
    assert(!sc.has_fragment_shader);
    ASSERT_EQ_INT(sc.num_vertex_samplers, 1);
    ASSERT_EQ_INT(sc.num_vertex_buffers, 1);

    fprintf(stderr, "\n\nTests passed!");
    return 0;
}
