# Super-powered GLSL with SDL3 integration

- Compiles shaders written in a GLSL-style language to SPIRV
- Fill the SDL3 GPU creation structs for you
- Handle setting `set` and `binding` indices for you
- Supports annotations for settings like blending, culling, etc. which is also filled into the SDL creation structs
- Compiled shaders can be output to both C and binary format

# Example

## Shader code

```glsl
// triangle.shader
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

## Compile shader

Using CLI (find it under [Releases](https://github.com/cribalik/cm_shader/releases))
```bash
shad sdl3 triangle.shader > my_shaders.h
```

Using C library

```c++
#include "shad.h"
#include "shad.c"

void compile() {
    ShadCompilation sc;
    shad_compile("triangle.shader", SHAD_OUTPUT_FORMAT_SDL, &sc);

    char *code;
    int code_len;
    shad_sdl_serialize_to_c(&sc, "triangle", &code, &code_len);
    write_to_file("my_shaders.h", code, code_len);

    shad_compilation_free(&sc);
}
```

## Use the compilation result to create SDL Shaders & Pipelines

```c++
#include "my_shaders.h"

void func() {
    /* shader creation */
    SDL_GPUShaderCreateInfo vsinfo = shad_sdl_vertex_shader_triangle;
    SDL_GPUShaderCreateInfo fsinfo = shad_sdl_fragment_shader_triangle;
    /* your own settings here... */
    SDL_GPUShader *vshader = SDL_CreateGPUShader(device, vsinfo);
    SDL_GPUShader *fshader = SDL_CreateGPUShader(device, fsinfo);

    /* pipeline creation */
    SDL_GPUGraphicsPipelineCreateInfo pinfo = shad_sdl_pipeline_triangle;
    pinfo.vertex_shader = vshader;
    pinfo.fragment_shader = fshader;
    /* your own settings here... */
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &pinfo);
}
```

Find complete examples under `examples`.

# State of project

Completed:
- [x] Support Windows
- [x] Support Vulkan (SPIRV)
- [x] Support serialization so you can prebake the shaders in a build step rather than having to parse and compile the shaders at runtime
- [x] Make a command line tool for precompiling shaders
- [x] Support Linux

TODO:
- [ ] Support compiling to D3D12

# Build

```cpp
#include "shad.h"
#include "shad.c"
```

`shad.h/c` are written in a single-header-library style, so you should be able to safely include it into your own C/C++ file without fear of name collisions.

To use `shad_sdl_fill_*()` functions, you must include `SDL.h` before including `shad.h`. cm_shader doesn't call any SDL functions, it only needs the headers.

# C API Documentation

**NOTE:** Prefer using the cli instead if you can, you won't have to use `shad.h/c` at all!

## Compilation API (`shad_compile*()` functions)

Use this API to compile shaders and serialize/deserialize the result. Handy if you have an engine that compiles shaders at runtime and caches the builds.

`shad_compile()`
Compile a shader. free with shad_compilation_free()
This requires linking with Vulkan (glslang specifically)

`shad_compilation_serialize()`
Serialize a compilation. data is freed when you call shad_compilation_free()

`shad_compilation_deserialize()`
Deserialize a compilation. free with shad_compilation_free()

`shad_compilation_free()`
Free a compilation

## SDL3 API (`shad_sdl_*()` functions)

This is the main API for using shad with SDL3.

`shad_sdl_serialize_to_c()`
Outputs C code with fully initialized SDL3 structs. The C code will be of the format:

```cpp
    static const SDL_GPUShaderCreateInfo shad_sdl_vertex_shader_<name> = {...};
    static const SDL_GPUShaderCreateInfo shad_sdl_fragment_shader_<name> = {...};
    static const SDL_GPUGraphicsPipelineCreateInfo shad_sdl_pipeline_<name> = {...};
```

`shad_sdl_fill_vertex_shader()`
Fill SDL_GPUShaderCreateInfo with settings from the vertex shader compilation result
Requires `SDL.h`

`shad_sdl_fill_fragment_shader()`
Fill SDL_GPUShaderCreateInfo with settings from the fragment shader compilation result
Requires `SDL.h`

`shad_sdl_fill_pipeline()`
Fill SDL_GPUGraphicsPipelineCreateInfo with settings from the pipeline compilation result
Requires `SDL.h`

NOTE: Any arrays that have to be allocated to fill the info structs will be bound to ShadCompilation,
    and will be destroyed when you call shad_compilation_free().
    In short, only call shad_compilation_free() once you are done with the create info structs.

NOTE: These functions will memzero the structs, so if you want to override some settings
    you must do so _after_ the call.

## Shader annotations

### Vertex input/output

#### Example

```glsl
@in           vec3 in_pos;        /* buffer slot = 0 by default */
@in(type=u8)  vec4 in_color;      /* component type will be unsigned byte, so color is char[4] */
@in(buffer=1) vec2 in_uv;        /* buffer slot = 1 */
@in(buffer=2, instanced) vec3 in_transform;
@out vec4 vertex_color;
@out vec2 vertex_uv;
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

`@out` doesn't have any parameters. Vertex output automatically gets added to fragment input.

### Fragment output

#### Example

```glsl
@out(format=rgba8) vec4 color;
```

#### Specification

Fragment input automatically becomes the vertex output, no need to specify it.

Fragment output optionally allows you to specify an output format, but sometimes you don't know it ahead of time (like when rendering to the window), in which case you must specify it yourself when creating your pipeline - cm_shader will not specify that for you.

The allowed formats are:
```
r8 rg8 rgba8 r16 rg16 rgba16 r16f rg16f rgba16f r32f rg32f rgba32f r11g11b10f
```

### Samplers, Textures, Buffers, Uniforms

Samplers, storage textures, storage buffers, and uniforms are specified with `@sampler`, `@image`, `@buffer`, `@uniform`.

#### Example

```glsl
@sampler sampler2D mysampler;
@image(format=rgba8) image2D myimage;
@buffer {int my_storage_buffer_int;};
@uniform {int my_uniform_int;};
```

The image is mandatory (as in regular GLSL). You can find the supported formats [here](https://www.khronos.org/opengl/wiki/Image_Load_Store).

You can specify `readonly` or `writeonly` for `@buffer` and `@image` like so:
```glsl
@image(format=rgba8) readonly image2D myimage;
@buffer writeonly image2D myimage;
```

The slots for each type is allocated in-order. For example:

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
```
never less equal less_or_equal greater not_equal greater_or_equal always
```

and `<format>` is one of
```
d16 d24 d32f d24_s8 d32f_s8
```

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
```
zero one src_color one_minus_src_color dst_color one_minus_dst_color src_alpha one_minus_src_alpha dst_alpha one_minus_dst_alpha constant_color one_minus_constant_color src_alpha_saturate
```

and `<op>` is one of

```
add subtract rev_subtract min max
```

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
