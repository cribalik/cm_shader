# Super-powered GLSL with SDL3 integration

- Compiles shaders written in a GLSL-style language to SPIRV
- Fill the SDL3 GPU creation structs for you
- Handle setting `set` and `binding` indices for you
- Supports annotations for settings like blending, culling, etc. which is also filled into the SDL creation structs

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

## Compiling at runtime

```c++
/* your C/C++ code */
#include <SDL3/SDL.h>
#define SHAD_COMPILER
#define SHAD_RUNTIME
#include "shad.h"

void func() {
    /* Compile shader */
    ShadResult sc;
    shad_compile("triangle.shader", SHAD_OUTPUT_FORMAT_SDL, &sc);

    /* SDL shader creation */
    SDL_GPUShaderCreateInfo vsinfo, fsinfo;
    shad_sdl_prefill_vertex_shader(&vsinfo, &sc);
    shad_sdl_prefill_fragment_shader(&fsinfo, &sc);
    SDL_GPUShader *vshader = SDL_CreateGPUShader(device, &vsinfo);
    SDL_GPUShader *fshader = SDL_CreateGPUShader(device, &fsinfo);

    /* SDL pipeline creation */
    SDL_GPUGraphicsPipelineCreateInfo pinfo;
    shad_sdl_prefill_pipeline(&pinfo, &sc);
    pinfo.vertex_shader = vshader;
    pinfo.fragment_shader = fshader;
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &pinfo);

    /* free compilation */
    shad_result_free(&sc);
}

#include "shad.c"
```

## Precompiling at build time

```c++
/* at build time */
#define SHAD_COMPILER
#include "shad.h"
void compile() {
    ShadResult sc;
    shad_compile("triangle.shader", SHAD_OUTPUT_FORMAT_SDL, &sc);

    char *c_code;
    size_t c_code_len;
    shad_serialize_to_c(&sc, &c_code, &c_code_len);
    write_to_file("triangle_shader.h", c_code, c_code_len);
}
#include "shad.c"
```

```c++
/* in your app */
#define SHAD_RUNTIME
#include "shad.h"
#include "triangle_shader.h"
void run() {
    ShadResult *sc = shad_result_triangle(); /* this exists in triangle_shader.h */
    /* use sc */
    sc_result_free(sc);
}
#include "shad.c"
```

Find complete examples under `examples`.

# State of project

Completed:
- [x] Support Windows
- [x] Support Vulkan (SPIRV)
- [x] Support serialization so you can prebake the shaders in a build step rather than having to parse and compile the shaders at runtime

TODO:
- [ ] Make a command line tool for precompiling shaders
- [ ] Support compiling to D3D12
- [ ] Support Linux

# Build

## 1. Link the Vulkan SDK (only required for `shad_compile()`)

`cm_shader` uses the Vulkan SDK (specifically glslang) to compile GLSL to SPIRV.

## 2. Include `shad.h` and `shad.c` into your project.

`shad.c` is written in a single-header-library style, so you should be able to safely include it into your own C file without fear of name collisions.

```c++
#include "shad.h"
/* your code */
#include "shad.c"
```

# Documentation

## Compiling versus Runtime

There are two aspects to the library - compiling the shaders to generate SPIRV and reflection data, and actually applying that information to the SDL creation structs.
The former you probably want to do at build time (or inside your engine's editor), and the latter you want to do at runtime.
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

### @import

Example:
```glsl
@import "somefile.shader"
```

Works similarly to C/C++ `#include` - finds a file relative to the current file and pastes its contents in-place.
