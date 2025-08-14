# Super-powered GLSL with SDL3 integration

- Compiles shaders written in a GLSL-style language to SPIRV
- Fill the SDL3 GPU creation structs for you
- Supports annotations for settings like blending, culling, etc. which is also filled into the SDL creation structs

```glsl
@blend src_alpha one_minus_src_alpha add
@cull front

@vert
    @in vec2 in_pos;
    @in vec2 in_uv;
    @out vec2 vertex_uv;
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

# C/C++ usage with SDL3

```c++
#include <SDL3/SDL.h>
#include "cm_shader.h"

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

    // free compilation
    shad_result_free(&sc);
}

#include "cm_shader.c"
```

# Build

## 1. Install the Vulkan SDK.

`cm_shader` uses the Vulkan SDK (specifically glslang) to compile GLSL to SPIRV.
Make sure you add %VULKAN_SDK%\Lib to your lib path, and %VULKAN_SDK%\Include to your includes path.

## 2. Include `cm_shader.h` and `cm_shader.c` into your project.

`cm_shader.c` is written in a single-header-library style, so you can safely include it into your own C file without fear of name collisions.

```c++
#include "cm_shader.h"
// your code
#include "cm_shader.c"
```

# Documentation

## Settings

### Vertex input/output

#### Example

```glsl
@in           vec3 in_pos;        // buffer slot = 0 by default
@in(type=u8)  vec4 in_color;      // component type will be unsigned byte, so color is char[4]
@in(buffer=1) vec2 in_uv;        // buffer slot = 1
@in(buffer=2, instanced) vec3 in_transform;
```

#### Specification
```glsl
@in(<args>)  name;
```

`<args>` can be any of
- `buffer=<number>`
  - where `<number>` is the buffer slot
- `type=<type>`
  - where `<type>` will indicate the component type, where valid values are: u8, i8, u16, i16. For example `@in(type=u8) vec4 color;` specifies 4 unsigned chars.
  Only vec2 and vec4 allow specifying a component type unfortunately. From what I understand, this is due to limitations in Metal.
- `instanced`. This indicates that the buffer corresponding to this vertex input should stride forward per instance as opposed to per vertex. As instancing is on a buffer level, not vertex input level, all vertex inputs bound to this buffer need to specify it as instanced.

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
    @uniform {...} // index 0
    @uniform {...} // index 1
    @sampler {...} // index 0
    @sampler {...} // index 1
    ...
@end

@vert
    @uniform {...} // index 0
    @uniform {...} // index 1
    @sampler {...} // index 0
    @sampler {...} // index 1
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

Culling must be specified at the top of the file before the vertex shader

### Multisampling

```glsl
@multisample 1|2|4|8
```

Culling must be specified at the top of the file before the vertex shader

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
