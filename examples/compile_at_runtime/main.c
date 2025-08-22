#include <SDL3/SDL.h>
#include "shad.h"
#include "shad.c"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char const *argv[]) {
    /* compile shader */
    ShadCompilation sc;
    shad_compile("triangle.shader", SHAD_OUTPUT_FORMAT_SDL, &sc);

    /* in your engine, you might want to only recompile the shader when the original shader file changes */
    /* you can use the serialization API to store the result and read it back later */
    {
        /* serialize */
        char *bytes;
        int bytes_len;
        shad_compilation_serialize(&sc, &bytes, &bytes_len);

        /* write to disk */
        FILE *f = fopen("shader.bin", "wb");
        fwrite(bytes, 1, bytes_len, f);
        fclose(f);

        /* free the old compilation result */
        shad_compilation_free(&sc);

        /* read from disk */
        f = fopen("shader.bin", "rb");
        fread(bytes, 1, bytes_len, f);
        fclose(f);

        /* deserialize */
        shad_compilation_deserialize(bytes, bytes_len, &sc);
    }

    /* SDL initialization */
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, 0, NULL);

    /* SDL shader creation */
    SDL_GPUShaderCreateInfo vsinfo, fsinfo;
    shad_sdl_fill_vertex_shader(&vsinfo, &sc);
    shad_sdl_fill_fragment_shader(&fsinfo, &sc);
    SDL_GPUShader *vshader = SDL_CreateGPUShader(device, &vsinfo);
    SDL_GPUShader *fshader = SDL_CreateGPUShader(device, &fsinfo);

    /* SDL pipeline creation */
    SDL_GPUGraphicsPipelineCreateInfo pinfo;
    shad_sdl_fill_pipeline(&pinfo, &sc);
    pinfo.vertex_shader = vshader;
    pinfo.fragment_shader = fshader;
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &pinfo);

    /* we're done with the shader compilation result, we can free it */
    shad_compilation_free(&sc);

    /* SDL texture creation */
    SDL_GPUTextureCreateInfo tinfo = {0};
    tinfo.type = SDL_GPU_TEXTURETYPE_2D;
    tinfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tinfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    tinfo.width = 512;
    tinfo.height = 512;
    tinfo.layer_count_or_depth = 1;
    tinfo.num_levels = 1;
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &tinfo);

    /* render to texture*/
    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUColorTargetInfo color_target_info = {0};
    color_target_info.texture = texture;
    color_target_info.mip_level = 0;
    color_target_info.layer_or_depth_plane = 0;
    color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target_info.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_info, 1, NULL);
    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
    SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
    SDL_EndGPURenderPass(render_pass);

    /* copy from texture to transfer buffer */
    int bitmap_size = tinfo.width * tinfo.height * 4;
    SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {0};
    transfer_buffer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_buffer_info.size = bitmap_size;
    SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_buffer_info);
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    SDL_GPUTextureRegion region = {texture, 0, 0, 0, 0, 0, tinfo.width, tinfo.height, 1};
    SDL_GPUTextureTransferInfo destination = {transfer_buffer, 0, tinfo.width, tinfo.width * tinfo.height};
    SDL_DownloadFromGPUTexture(copy_pass, &region, &destination);
    SDL_EndGPUCopyPass(copy_pass);

    /* finish render */
    SDL_SubmitGPUCommandBuffer(command_buffer);
    SDL_WaitForGPUIdle(device);

    /* write result to bitmap */
    void *data = SDL_MapGPUTransferBuffer(device, transfer_buffer, 0);
    stbi_write_bmp("output.bmp", tinfo.width, tinfo.height, 4, data);

    return 0;
}
