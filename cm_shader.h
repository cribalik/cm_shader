#ifndef CM_SHADER_H
#define CM_SHADER_H

/*

cm_shader - Super-powered GLSL with SDL3 integration
=================================================

You write your shaders in a GLSL-style language, it can transpile it to SPIRV, and can fill the SDL creation structs for you.
It also supports annotations for settings like blending, culling, etc. which is also filled into the SDL creation structs.


Example usage
=============

// main.c

void func() {
    // parse and compile shader
    SC_Result sc;
    sc_compile("my_shaders/triangle.shader", SC_OUTPUT_FORMAT_SDL, &sc);

    // SDL shader creation
    SDL_GPUShaderCreateInfo vsinfo, fsinfo;
    sc_sdl_prefill_vertex_shader(&vsinfo, &sc);
    sc_sdl_prefill_fragment_shader(&fsinfo, &sc);
    SDL_GPUShader *vshader = SDL_CreateGPUShader(device, &vsinfo);
    SDL_GPUShader *fshader = SDL_CreateGPUShader(device, &fsinfo);

    // SDL pipeline creation
    SDL_GPUGraphicsPipelineCreateInfo pinfo;
    sc_sdl_prefill_pipeline(&pinfo, &sc);
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &pinfo);

    // destroy result
    sc_result_free(&sc);
}

// my_shaders/triangle.shader

@vert

    @in           vec3 in_pos;                 // buffer slot = 0 by default
    @in(type=u8)  vec4 in_color;      // component type will be unsigned byte, so color is char[4]
    @in(buffer=1) vec2 in_uv;        // buffer slot = 1
    @uniform {vec2 offset;};         // all uniforms are std140

    // vertex shader outputs will automatically get added as fragment shader inputs
    @out vec4 vertex_color;
    @out vec2 vertex_uv;

    void main() {
        gl_Position = vec4(in_pos + offset, 0.0, 1.0);
        vertex_color = in_color;
        vertex_uv = in_uv;
    }

@end

@frag

    @sampler sampler2D some_other_color;
    @buffer readonly some_other_color;
    @out(format=rgba8) vec4 output_color;

    void main() {
        output_color = vertex_color * texture(some_other_color, vertex_uv);
    }

@end


Annotation documentation
========================

TODO

*/

typedef enum SC_OutputFormat {
    SC_OUTPUT_FORMAT_SDL,
    /* Other rendering frameworks will be supported in the future */
} SC_OutputFormat;

typedef char SC_Bool;

typedef enum SC_VertexElementFormat {
    SC_VERTEXELEMENTFORMAT_INVALID,
    SC_VERTEXELEMENTFORMAT_INT,          /* @in int x; */
    SC_VERTEXELEMENTFORMAT_INT2,         /* @in ivec2 x; */
    SC_VERTEXELEMENTFORMAT_INT3,         /* @in ivec3 x; */
    SC_VERTEXELEMENTFORMAT_INT4,         /* @in ivec4 x; */
    SC_VERTEXELEMENTFORMAT_UINT,         /* @in uint x; */
    SC_VERTEXELEMENTFORMAT_UINT2,        /* @in uvec2 x; */
    SC_VERTEXELEMENTFORMAT_UINT3,        /* @in uvec3 x; */
    SC_VERTEXELEMENTFORMAT_UINT4,        /* @in uvec4 x; */
    SC_VERTEXELEMENTFORMAT_FLOAT,        /* @in float x; */
    SC_VERTEXELEMENTFORMAT_FLOAT2,       /* @in vec2 x; */
    SC_VERTEXELEMENTFORMAT_FLOAT3,       /* @in vec3 x; */
    SC_VERTEXELEMENTFORMAT_FLOAT4,       /* @in vec4 x; */
    SC_VERTEXELEMENTFORMAT_BYTE2,        /* @in(type=i8) ivec2 x; */
    SC_VERTEXELEMENTFORMAT_BYTE4,        /* @in(type=i8) ivec4 x; */
    SC_VERTEXELEMENTFORMAT_UBYTE2,       /* @in(type=u8) uvec2 x; */
    SC_VERTEXELEMENTFORMAT_UBYTE4,       /* @in(type=u8) uvec4 x; */
    SC_VERTEXELEMENTFORMAT_BYTE2_NORM,   /* @in(type=i8) vec2 x; */
    SC_VERTEXELEMENTFORMAT_BYTE4_NORM,   /* @in(type=i8) vec4 x; */
    SC_VERTEXELEMENTFORMAT_UBYTE2_NORM,  /* @in(type=u8) vec2 x; */
    SC_VERTEXELEMENTFORMAT_UBYTE4_NORM,  /* @in(type=u8) vec4 x; */
    SC_VERTEXELEMENTFORMAT_SHORT2,       /* @in(type=i16) ivec2 x; */
    SC_VERTEXELEMENTFORMAT_SHORT4,       /* @in(type=i16) ivec4 x; */
    SC_VERTEXELEMENTFORMAT_USHORT2,      /* @in(type=u16) uvec2 x; */
    SC_VERTEXELEMENTFORMAT_USHORT4,      /* @in(type=u16) uvec4 x; */
    SC_VERTEXELEMENTFORMAT_SHORT2_NORM,  /* @in(type=i16) vec2 x; */
    SC_VERTEXELEMENTFORMAT_SHORT4_NORM,  /* @in(type=i16) vec4 x; */
    SC_VERTEXELEMENTFORMAT_USHORT2_NORM, /* @in(type=u16) vec2 x; */
    SC_VERTEXELEMENTFORMAT_USHORT4_NORM, /* @in(type=u16) vec4 x; */
} SC_VertexElementFormat;

typedef enum SC_BlendFactor {
    SC_BLEND_FACTOR_INVALID,
    SC_BLEND_FACTOR_ZERO,
    SC_BLEND_FACTOR_ONE,
    SC_BLEND_FACTOR_SRC_COLOR,
    SC_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    SC_BLEND_FACTOR_DST_COLOR,
    SC_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    SC_BLEND_FACTOR_SRC_ALPHA,
    SC_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    SC_BLEND_FACTOR_DST_ALPHA,
    SC_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    SC_BLEND_FACTOR_CONSTANT_COLOR,
    SC_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
    SC_BLEND_FACTOR_SRC_ALPHA_SATURATE,
} SC_BlendFactor;

typedef enum SC_BlendOp {
    SC_BLEND_OP_INVALID,
    SC_BLEND_OP_ADD,          /* add */
    SC_BLEND_OP_SUBTRACT,     /* subtract */
    SC_BLEND_OP_REV_SUBTRACT, /* rev_subtract */
    SC_BLEND_OP_MIN,          /* min */
    SC_BLEND_OP_MAX,          /* max */
} SC_BlendOp;

typedef enum SC_CullMode {
    SC_CULL_MODE_INVALID,
    SC_CULL_MODE_NONE, /* none */
    SC_CULL_MODE_FRONT,/* front */
    SC_CULL_MODE_BACK, /* back */
} SC_CullMode;

typedef enum SC_CompareOp {
    SC_COMPARE_OP_INVALID,
    SC_COMPARE_OP_NEVER,             /* never */
    SC_COMPARE_OP_LESS,              /* less */
    SC_COMPARE_OP_EQUAL,             /* equal */
    SC_COMPARE_OP_LESS_OR_EQUAL,     /* less_or_equal */
    SC_COMPARE_OP_GREATER,           /* greater */
    SC_COMPARE_OP_NOT_EQUAL,         /* not_equal */
    SC_COMPARE_OP_GREATER_OR_EQUAL,  /* greater_or_equal */
    SC_COMPARE_OP_ALWAYS,            /* always */
} SC_CompareOp;

typedef enum SC_TextureFormat {
    SC_TEXTURE_FORMAT_INVALID,
    SC_TEXTURE_FORMAT_R8,         /* @out(format=r8) float x; */
    SC_TEXTURE_FORMAT_RG8,        /* @out(format=rg8) vec2 x; */
    SC_TEXTURE_FORMAT_RGBA8,      /* @out(format=rgba8) vec4 x; */
    SC_TEXTURE_FORMAT_R16,        /* @out(format=r16) float x; */
    SC_TEXTURE_FORMAT_RG16,       /* @out(format=rg16) vec2 x; */
    SC_TEXTURE_FORMAT_RGBA16,     /* @out(format=rgba16) vec4 x; */
    SC_TEXTURE_FORMAT_R16F,       /* @out(format=r16f) float x; */
    SC_TEXTURE_FORMAT_RG16F,      /* @out(format=rg16f) vec2 x; */
    SC_TEXTURE_FORMAT_RGBA16F,    /* @out(format=rgba16f) vec4 x; */
    SC_TEXTURE_FORMAT_R32F,       /* @out(format=r32f) float x; */
    SC_TEXTURE_FORMAT_RG32F,      /* @out(format=rg32f) vec2 x; */
    SC_TEXTURE_FORMAT_RGBA32F,    /* @out(format=rgba32f) vec4 x; */
    SC_TEXTURE_FORMAT_R11G11B10F, /* @out(format=r11g11b10f) vec3 x; */
    SC_TEXTURE_FORMAT_D16,        /* @depth less write d16 clip */
    SC_TEXTURE_FORMAT_D24,        /* @depth less write d24 clip */
    SC_TEXTURE_FORMAT_D32F,       /* @depth less write d32f clip */
    SC_TEXTURE_FORMAT_D24_S8,     /* @depth less write d24_s8 clip */
    SC_TEXTURE_FORMAT_D32F_S8,    /* @depth less write d32f_s8 clip */
} SC_TextureFormat;

typedef struct SC_VertexInput {
    /* example:
    *
    * @in(type=u8, buffer=3) vec4 rgb;
    *
    * would yield
    *   component_type = u8
    *   data_type = vec4
    *   name = rgb
    *   buffer_slot = 3
    *   format = SC_VERTEXELEMENTFORMAT_UBYTE4_NORM
    *
    */
    int code_location;
    char *component_type;
    char *data_type;
    char *name;
    SC_VertexElementFormat format;
    int buffer_slot;
    int size;
    int align;
    int offset;
    SC_Bool is_flat;
    SC_Bool instanced;
} SC_VertexInput;

typedef struct SC_FragmentOutput {
    int code_location;
    SC_TextureFormat format;

    /* blending */
    int blend_code_location;
    SC_BlendFactor blend_src;
    SC_BlendFactor blend_dst;
    SC_BlendOp blend_op;
} SC_FragmentOutput;

typedef struct SC_VertexInputBuffer {
    int slot;
    SC_Bool instanced;
    int stride;
} SC_VertexInputBuffer;

typedef struct SC_Result {
    /* vertex shader info */
    char *vertex_code;
    size_t vertex_code_size;
    uint32_t *spirv_vertex_code;
    size_t spirv_vertex_code_size;
    SC_VertexInput *vertex_inputs;
    int num_vertex_inputs;
    SC_VertexInputBuffer *vertex_input_buffers;
    int num_vertex_input_buffers;
    int num_vertex_outputs;
    int num_vertex_samplers;
    int num_vertex_images;
    int num_vertex_buffers;
    int num_vertex_uniforms;

    /* fragment shader info */
    SC_Bool has_fragment_shader;
    char *fragment_code;
    size_t fragment_code_size;
    uint32_t *spirv_fragment_code;
    size_t spirv_fragment_code_size;
    SC_FragmentOutput *fragment_outputs;
    int num_fragment_outputs;
    int num_fragment_samplers;
    int num_fragment_images;
    int num_fragment_buffers;
    int num_fragment_uniforms;

    /* depth */
    int depth_code_location;
    SC_Bool depth_write;
    SC_CompareOp depth_cmp;
    SC_TextureFormat depth_format;
    SC_Bool depth_clip;

    /* culling */
    int cull_code_location;
    SC_CullMode cull_mode;

    /* multisampling. Valid values are 1,2,4,8 */
    int multisample_count;

    /* private stuff */
    void *arena;
} SC_Result;

/* Returns 1 on success, 0 on failure. Call sc_result_free() to free result */
int sc_compile(const char *path, SC_OutputFormat output_format, SC_Result *result);
/* Free the result */
void sc_result_free(SC_Result*);

#ifdef SDL_VERSION

/*

Fills SDL_GPU*CreateInfo structs with settings from the shader.

NOTE: Any arrays that have to be allocated to fill the info structs will be bound to SC_Result,
      and will be destroyed when you call sc_result_free().
      In short, only call sc_result_free() once you are done with the create info structs.

NOTE: These functions will memzero the structs, so if you want to override some settings
      you must do so _after_ the call.

*/

void sc_sdl_prefill_vertex_shader(SDL_GPUShaderCreateInfo *info, SC_Result *sc);
void sc_sdl_prefill_fragment_shader(SDL_GPUShaderCreateInfo *info, SC_Result *sc);
void sc_sdl_prefill_pipeline(SDL_GPUGraphicsPipelineCreateInfo *info, SC_Result *sc);
#endif

#endif /* CM_SHADER_H */