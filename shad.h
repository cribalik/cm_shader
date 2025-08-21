#ifndef CM_SHADER_H
#define CM_SHADER_H

#include <stdint.h>

/*

cm_shader - Super-powered GLSL with SDL3 integration
=================================================

You write your shaders in a GLSL-style language, it can transpile it to SPIRV, and can fill the SDL creation structs for you.
It also supports annotations for settings like blending, culling, etc. which is also filled into the SDL creation structs.

See README.md for documentation

*/

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(SHAD_RUNTIME) && !defined(SHAD_COMPILER)
    #error "Either SHAD_RUNTIME or SHAD_COMPILER must be defined. Check the README.md for examples"
#endif

typedef enum ShadOutputFormat {
    SHAD_OUTPUT_FORMAT_INVALID,
    SHAD_OUTPUT_FORMAT_SDL
    /* Other rendering frameworks will be supported in the future */
} ShadOutputFormat;

typedef char ShadBool;
#define ShadFalse 0
#define ShadTrue 1

typedef enum ShadVertexElementFormat {
    SHAD_VERTEXELEMENTFORMAT_INVALID,
    SHAD_VERTEXELEMENTFORMAT_INT,          /* @in int x; */
    SHAD_VERTEXELEMENTFORMAT_INT2,         /* @in ivec2 x; */
    SHAD_VERTEXELEMENTFORMAT_INT3,         /* @in ivec3 x; */
    SHAD_VERTEXELEMENTFORMAT_INT4,         /* @in ivec4 x; */
    SHAD_VERTEXELEMENTFORMAT_UINT,         /* @in uint x; */
    SHAD_VERTEXELEMENTFORMAT_UINT2,        /* @in uvec2 x; */
    SHAD_VERTEXELEMENTFORMAT_UINT3,        /* @in uvec3 x; */
    SHAD_VERTEXELEMENTFORMAT_UINT4,        /* @in uvec4 x; */
    SHAD_VERTEXELEMENTFORMAT_FLOAT,        /* @in float x; */
    SHAD_VERTEXELEMENTFORMAT_FLOAT2,       /* @in vec2 x; */
    SHAD_VERTEXELEMENTFORMAT_FLOAT3,       /* @in vec3 x; */
    SHAD_VERTEXELEMENTFORMAT_FLOAT4,       /* @in vec4 x; */
    SHAD_VERTEXELEMENTFORMAT_BYTE2,        /* @in(type=i8) ivec2 x; */
    SHAD_VERTEXELEMENTFORMAT_BYTE4,        /* @in(type=i8) ivec4 x; */
    SHAD_VERTEXELEMENTFORMAT_UBYTE2,       /* @in(type=u8) uvec2 x; */
    SHAD_VERTEXELEMENTFORMAT_UBYTE4,       /* @in(type=u8) uvec4 x; */
    SHAD_VERTEXELEMENTFORMAT_BYTE2_NORM,   /* @in(type=i8) vec2 x; */
    SHAD_VERTEXELEMENTFORMAT_BYTE4_NORM,   /* @in(type=i8) vec4 x; */
    SHAD_VERTEXELEMENTFORMAT_UBYTE2_NORM,  /* @in(type=u8) vec2 x; */
    SHAD_VERTEXELEMENTFORMAT_UBYTE4_NORM,  /* @in(type=u8) vec4 x; */
    SHAD_VERTEXELEMENTFORMAT_SHORT2,       /* @in(type=i16) ivec2 x; */
    SHAD_VERTEXELEMENTFORMAT_SHORT4,       /* @in(type=i16) ivec4 x; */
    SHAD_VERTEXELEMENTFORMAT_USHORT2,      /* @in(type=u16) uvec2 x; */
    SHAD_VERTEXELEMENTFORMAT_USHORT4,      /* @in(type=u16) uvec4 x; */
    SHAD_VERTEXELEMENTFORMAT_SHORT2_NORM,  /* @in(type=i16) vec2 x; */
    SHAD_VERTEXELEMENTFORMAT_SHORT4_NORM,  /* @in(type=i16) vec4 x; */
    SHAD_VERTEXELEMENTFORMAT_USHORT2_NORM, /* @in(type=u16) vec2 x; */
    SHAD_VERTEXELEMENTFORMAT_USHORT4_NORM /* @in(type=u16) vec4 x; */
} ShadVertexElementFormat;

typedef enum ShadBlendFactor {
    SHAD_BLEND_FACTOR_INVALID,
    SHAD_BLEND_FACTOR_ZERO,
    SHAD_BLEND_FACTOR_ONE,
    SHAD_BLEND_FACTOR_SRC_COLOR,
    SHAD_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    SHAD_BLEND_FACTOR_DST_COLOR,
    SHAD_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    SHAD_BLEND_FACTOR_SRC_ALPHA,
    SHAD_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    SHAD_BLEND_FACTOR_DST_ALPHA,
    SHAD_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    SHAD_BLEND_FACTOR_CONSTANT_COLOR,
    SHAD_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
    SHAD_BLEND_FACTOR_SRC_ALPHA_SATURATE
} ShadBlendFactor;

typedef enum ShadBlendOp {
    SHAD_BLEND_OP_INVALID,
    SHAD_BLEND_OP_ADD,          /* add */
    SHAD_BLEND_OP_SUBTRACT,     /* subtract */
    SHAD_BLEND_OP_REV_SUBTRACT, /* rev_subtract */
    SHAD_BLEND_OP_MIN,          /* min */
    SHAD_BLEND_OP_MAX          /* max */
} ShadBlendOp;

typedef enum ShadCullMode {
    SHAD_CULL_MODE_INVALID,
    SHAD_CULL_MODE_NONE, /* none */
    SHAD_CULL_MODE_FRONT,/* front */
    SHAD_CULL_MODE_BACK /* back */
} ShadCullMode;

typedef enum ShadCompareOp {
    SHAD_COMPARE_OP_INVALID,
    SHAD_COMPARE_OP_NEVER,             /* never */
    SHAD_COMPARE_OP_LESS,              /* less */
    SHAD_COMPARE_OP_EQUAL,             /* equal */
    SHAD_COMPARE_OP_LESS_OR_EQUAL,     /* less_or_equal */
    SHAD_COMPARE_OP_GREATER,           /* greater */
    SHAD_COMPARE_OP_NOT_EQUAL,         /* not_equal */
    SHAD_COMPARE_OP_GREATER_OR_EQUAL,  /* greater_or_equal */
    SHAD_COMPARE_OP_ALWAYS            /* always */
} ShadCompareOp;

typedef enum ShadTextureFormat {
    SHAD_TEXTURE_FORMAT_INVALID,
    SHAD_TEXTURE_FORMAT_R8,         /* @out(format=r8) float x; */
    SHAD_TEXTURE_FORMAT_RG8,        /* @out(format=rg8) vec2 x; */
    SHAD_TEXTURE_FORMAT_RGBA8,      /* @out(format=rgba8) vec4 x; */
    SHAD_TEXTURE_FORMAT_R16,        /* @out(format=r16) float x; */
    SHAD_TEXTURE_FORMAT_RG16,       /* @out(format=rg16) vec2 x; */
    SHAD_TEXTURE_FORMAT_RGBA16,     /* @out(format=rgba16) vec4 x; */
    SHAD_TEXTURE_FORMAT_R16F,       /* @out(format=r16f) float x; */
    SHAD_TEXTURE_FORMAT_RG16F,      /* @out(format=rg16f) vec2 x; */
    SHAD_TEXTURE_FORMAT_RGBA16F,    /* @out(format=rgba16f) vec4 x; */
    SHAD_TEXTURE_FORMAT_R32F,       /* @out(format=r32f) float x; */
    SHAD_TEXTURE_FORMAT_RG32F,      /* @out(format=rg32f) vec2 x; */
    SHAD_TEXTURE_FORMAT_RGBA32F,    /* @out(format=rgba32f) vec4 x; */
    SHAD_TEXTURE_FORMAT_R11G11B10F, /* @out(format=r11g11b10f) vec3 x; */
    SHAD_TEXTURE_FORMAT_D16,        /* @depth less write d16 clip */
    SHAD_TEXTURE_FORMAT_D24,        /* @depth less write d24 clip */
    SHAD_TEXTURE_FORMAT_D32F,       /* @depth less write d32f clip */
    SHAD_TEXTURE_FORMAT_D24_S8,     /* @depth less write d24_s8 clip */
    SHAD_TEXTURE_FORMAT_D32F_S8    /* @depth less write d32f_s8 clip */
} ShadTextureFormat;

typedef enum ShadPrimitive {
    SHAD_PRIMITIVE_INVALID,
    SHAD_PRIMITIVE_TRIANGLE_LIST,
    SHAD_PRIMITIVE_TRIANGLE_STRIP,
    SHAD_PRIMITIVE_LINE_LIST,
    SHAD_PRIMITIVE_LINE_STRIP,
    SHAD_PRIMITIVE_POINT_LIST
} ShadPrimitive;

typedef struct ShadCodeLocation {
    char *path;
    char *start;
    char *pos;
} ShadCodeLocation;

typedef struct ShadVertexInput {
    /* example:
    *
    * @in(type=u8, buffer=3) vec4 rgb;
    *
    * would yield
    *   component_type = u8
    *   data_type = vec4
    *   name = rgb
    *   buffer_slot = 3
    *   format = SHAD_VERTEXELEMENTFORMAT_UBYTE4_NORM
    *
    */
    ShadCodeLocation code_location;
    char *component_type;
    char *data_type;
    char *name;
    ShadVertexElementFormat format;
    int buffer_slot;
    int size;
    int align;
    int offset;
    ShadBool is_flat;
    ShadBool instanced;
} ShadVertexInput;

typedef struct ShadFragmentOutput {
    ShadCodeLocation code_location;
    ShadTextureFormat format;

    /* blending */
    ShadCodeLocation blend_code_location;
    ShadBlendFactor blend_src;
    ShadBlendFactor blend_dst;
    ShadBlendOp blend_op;
} ShadFragmentOutput;

typedef struct ShadVertexInputBuffer {
    int slot;
    ShadBool instanced;
    int stride;
} ShadVertexInputBuffer;

typedef struct ShadResult {
    /* vertex shader info */
    char *vertex_code;
    int vertex_code_size;
    char *spirv_vertex_code;
    int spirv_vertex_code_size;
    ShadVertexInput *vertex_inputs;
    int num_vertex_inputs;
    ShadVertexInputBuffer *vertex_input_buffers;
    int num_vertex_input_buffers;
    int num_vertex_outputs;
    int num_vertex_samplers;
    int num_vertex_images;
    int num_vertex_buffers;
    int num_vertex_uniforms;

    /* fragment shader info */
    ShadBool has_fragment_shader;
    char *fragment_code;
    int fragment_code_size;
    char *spirv_fragment_code;
    int spirv_fragment_code_size;
    ShadFragmentOutput *fragment_outputs;
    int num_fragment_outputs;
    int num_fragment_samplers;
    int num_fragment_images;
    int num_fragment_buffers;
    int num_fragment_uniforms;

    /* depth */
    ShadCodeLocation depth_code_location;
    ShadBool depth_write;
    ShadCompareOp depth_cmp;
    ShadTextureFormat depth_format;
    ShadBool depth_clip;

    /* culling */
    ShadCodeLocation cull_code_location;
    ShadCullMode cull_mode;

    /* primitive */
    ShadPrimitive primitive;

    /* multisampling. Valid values are 1,2,4,8 */
    int multisample_count;

    /* private stuff */
    void *arena;
} ShadResult;

/* Free a compilation result */
void shad_result_free(ShadResult*);
/*
    Serialize a compilation into C code to include into your project.
    The C code will be of the format:
        static const ShadResult shad_result_<name_of_file_without_extension>;
*/
void shad_serialize_to_c(const ShadResult *result, const char *name, char **code_out, int *num_bytes_out);
/*
    Serialize a compilation into a binary format.
    Deserialize with shad_deserialize()
    Free the result with shad_free()
*/
void shad_serialize(const ShadResult *result, char **bytes_out, int *num_bytes_out);
/*
    Deserialize bytes that were serialized with shad_serialize()
*/
ShadBool shad_deserialize(char *bytes, int num_bytes, ShadResult *result);
void shad_free(void*);

/*******

    Compilation API

    Use this API to compile shaders. Can be done as a build step or at runtime.
    Requires linking with Vulkan (glslang specifically)

*******/

#ifdef SHAD_COMPILER

/* Returns 1 on success, 0 on failure. Call shad_result_free() to free result */
ShadBool shad_compile(const char *path, ShadOutputFormat output_format, ShadResult *result);

#endif /* SHAD_COMPILER */

/*******

    Runtime API

    Use this API to create GPU Shaders and Pipelines at runtime.
    Doesn't require linking to anything
    You can either serialize to binary representation, or to C code for compiling into your project.

*******/

#ifdef SHAD_RUNTIME

/*****
  SDL implementation
 *****/

#ifdef SDL_VERSION

/*

Fills SDL_GPU*CreateInfo structs with settings from the shader.

NOTE: Any arrays that have to be allocated to fill the info structs will be bound to ShadResult,
      and will be destroyed when you call shad_result_free().
      In short, only call shad_result_free() once you are done with the create info structs.

NOTE: These functions will memzero the structs, so if you want to override some settings
      you must do so _after_ the call.

*/

void shad_sdl_prefill_vertex_shader(struct SDL_GPUShaderCreateInfo *info, ShadResult *sc);
void shad_sdl_prefill_fragment_shader(struct SDL_GPUShaderCreateInfo *info, ShadResult *sc);
void shad_sdl_prefill_pipeline(struct SDL_GPUGraphicsPipelineCreateInfo *info, ShadResult *sc);

#endif /* SDL_VERSION */

#endif /* SHAD_RUNTIME*/

#ifdef __cplusplus
}
#endif

#endif /* CM_SHADER_H */