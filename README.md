# Super-powered GLSL with SDL3 integration

- Compiles shaders written in a GLSL-style language to SPIRV
- Fill the SDL3 GPU creation structs for you
- Handle setting `set` and `binding` indices for you
- Supports annotations for settings like blending, culling, etc. which is also filled into the SDL creation structs
- Compiled shaders can be output to both C and binary format

# Example

## Shader code
```glsl
@blend src_alpha one_minus_src_alpha add
@cull front

@vert
    @in vec2 in_pos;
    @in vec2 in_uv;
    @out vec2 vertex_uv;
    @import "some_other_file.shader"
    void main() {
        gl_Position = vec4(in_pos, 0.0, 1.0);
        vertex_uv = in_uv;
    }
@end

@frag
    @sampler sampler2D color;
    @out(format=rgba8) vec4 output_color;
    void main() {
        output_color = texture(color, vertex_uv);
    }
@end
```

## Compiling the shader

You can either compile using the C interface, or using the CLI.

### Compilation using CLI

```bash
shad_cli.exe triangle.shader --output triangle_shader.h --sdl --output_c
```

You can find the CLI under [Releases](https://github.com/cribalik/cm_shader/releases)

### Compilation using C library

```c++
/* at build time */
#define SHAD_COMPILER
#include "shad.h"
void compile() {
    ShadResult sr;
    shad_compile("triangle.shader", SHAD_OUTPUT_FORMAT_SDL, &sr);

    char *code;
    size_t code_len;
    shad_serialize_to_c(&sr, "triangle", &code, &code_len);
    write_to_file("triangle_shader.h", code, code_len);
}
#include "shad.c"
```

## Using the compilation result to create SDL Shaders & Pipelines

```c++
#include <SDL3/SDL.h>
#define SHAD_RUNTIME
#include "shad.h"

/* this contains all the shader info we compiled, accessible through shad_result_<name>() */
#include "triangle_shader.h"

void func() {
    ShadResult *sr = shad_result_triangle(); /* this exists in triangle_shader.h */

    /* SDL shader creation */
    SDL_GPUShaderCreateInfo vsinfo, fsinfo;
    shad_sdl_prefill_vertex_shader(&vsinfo, &sr);
    shad_sdl_prefill_fragment_shader(&fsinfo, &sr);
    SDL_GPUShader *vshader = SDL_CreateGPUShader(device, &vsinfo);
    SDL_GPUShader *fshader = SDL_CreateGPUShader(device, &fsinfo);

    /* SDL pipeline creation */
    SDL_GPUGraphicsPipelineCreateInfo pinfo;
    shad_sdl_prefill_pipeline(&pinfo, &sr);
    pinfo.vertex_shader = vshader;
    pinfo.fragment_shader = fshader;
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &pinfo);

    /* free compilation */
    shad_result_free(&sr);
}

#include "shad.c"
```

Find complete examples under `examples`.

# State of project

Completed:
- [x] Support Windows
- [x] Support Vulkan (SPIRV)
- [x] Support serialization so you can prebake the shaders in a build step rather than having to parse and compile the shaders at runtime
- [x] Make a command line tool for precompiling shaders

TODO:
- [ ] Support compiling to D3D12
- [ ] Support Linux

# Build

## Building the compiler

**NOTE**: If you will be compiling the shaders at build time, consider using the CLI instead.

1. Link the Vulkan SDK. `cm_shader` uses the Vulkan SDK (specifically glslang) to compile GLSL to SPIRV.
2. Include `shad.h` and `shad.c` into your project, and define `SHAD_COMPILER`

`shad.c` is written in a single-header-library style, so you should be able to safely include it into your own C file without fear of name collisions.

```c++
#define SHAD_COMPILER
#include "shad.h"
/* your code */
#include "shad.c"
```

## Building the runtime

1. Include SDL3 headers
2. Include `shad.h` and `shad.c`, and define `SHAD_RUNTIME`

`shad.c` is written in a single-header-library style, so you should be able to safely include it into your own C file without fear of name collisions.

```c++
#define SHAD_RUNTIME
#include "shad.h"
/* your code */
#include "shad.c"
```

# Documentation

## `SHAD_COMPILER` versus `SHAD_RUNTIME`

There are two aspects to the library - compiling the shaders to generate SPIRV and reflection data, and actually applying that information to the SDL creation structs.
The former you probably want to do at build time, and the latter you want to do at runtime.
The former also requires the Vulkan SDK, while the latter only requires SDL headers.

To enable the compilation functions, you specify `SHAD_COMPILER` before including `shad.h`

To enable the runtime functions, you specify `SHAD_RUNTIME` before including `shad.h`

## Shader annotations

### Vertex input/output

#### Example

```glsl
@in           vec3 in_pos;        /* buffer slot = 0 by default */
@in(type=u8)  vec4 in_color;      /* component type will be unsigned byte, so color is char[4] */
@in(buffer=1) vec2 in_uv;        /* buffer slot = 1 */
@in(buffer=2, instanced) vec3 in_transform;
```

#### Specification
```glsl
@in(<args>)  name;
```

`<args>` can be any of
- `buffer=<number>`
  where `<number>` is the buffer slot
- `type=<type>`
  where `<type>` will indicate the component type, where valid values are:

    `u8`, `i8`, `u16`, `i16`
  
  For example `@in(type=u8) vec4 color;` specifies 4 unsigned chars.
  Only vec2 and vec4 allow specifying a component type unfortunately. From what I understand, this is due to limitations in Metal.
- `instanced`
  This indicates that the buffer corresponding to this vertex input should stride forward per instance as opposed to per vertex. As instancing is on a buffer level, not vertex input level, all vertex inputs bound to this buffer need to specify it as instanced.

### Fragment input/output

#### Example

```glsl
@out(format=rgba8) vec4 color;
```

#### Specification

Fragment input automatically becomes the vertex output, no need to specify it.

Fragment output optionally allows you to specify an output format, but sometimes you don't know it ahead of time (like when rendering to the window), in which case you must specify it yourself when creating your pipeline - cm_shader will not specify that for you.

The allowed formats are:
- r8
- rg8
- rgba8
- r16
- rg16
- rgba16
- r16f
- rg16f
- rgba16f
- r32f
- rg32f
- rgba32f
- r11g11b10f

### Samplers, Textures, Buffers, Uniforms

Samplers, (storage) textures, (storage) buffers, and uniforms are specified with `@sampler`, `@texture`, `@buffer`, `@uniform`.

#### Example

```glsl
@sampler sampler2D mysampler;
@texture(format=rgba8) mytexture;
@buffer {int my_storage_buffer_int;};
@uniform {int my_uniform_int;};
```

The texture format must be specified (as in regular GLSL). You can find the supported formats [here](https://www.khronos.org/opengl/wiki/Image_Load_Store).

The slots for these are allocated in-order. For example:

```glsl
@vert
    @uniform {...} /* index 0 */
    @uniform {...} /* index 1 */
    @sampler {...} /* index 0 */
    @sampler {...} /* index 1 */
    ...
@end

@vert
    @uniform {...} /* index 0 */
    @uniform {...} /* index 1 */
    @sampler {...} /* index 0 */
    @sampler {...} /* index 1 */
    ...
@end
```

### Depth

```glsl
@depth <op> read|write <format> clip|clamp
```

where `<op>` is one of
- never
- less
- equal
- less_or_equal
- greater
- not_equal
- greater_or_equal
- always
and `<format>` is one of
- d16
- d24
- d32f
- d24_s8
- d32f_s8

Depth must be specified at the top of the file before the vertex shader

### Multisampling

```glsl
@multisample 1|2|4|8
```

Multisampling must be specified at the top of the file before the vertex shader

### Culling

```glsl
@cull none|front|back
```

Culling must be specified at the top of the file before the vertex shader

### Blend

```glsl
@blend <src_blend_factor> <dst_bland_factor> <op>
```

where `<src_blend_factor>` and `<dst_blend_factor>` is one of
- zero
- one
- src_color
- one_minus_src_color
- dst_color
- one_minus_dst_color
- src_alpha
- one_minus_src_alpha
- dst_alpha
- one_minus_dst_alpha
- constant_color
- one_minus_constant_color
- src_alpha_saturate

and `<op>` is one of
- add
- subtract
- rev_subtract
- min
- max

To specify different `@blend` settings for different fragment outputs, you can just re-specify the blend before the output declaration like so:

```glsl
@frag
    @blend src_alpha one_minus_src_alpha add
    @output(format=rgba8) vec4 color1;
    @blend src_alpha one_minus_src_alpha subtract
    @output(format=rgba8) vec4 color2;
@end
```

### @primitive

Specifies to primitive topology.

```glsl
@primitive triangle_list|triangle_strip|line_list|line_strip|point_list
```

### @import

Example:
```glsl
@import "somefile.shader"
```

Works similarly to C/C++ `#include` - finds a file relative to the current file and pastes its contents in-place.
