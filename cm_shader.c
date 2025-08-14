#include "cm_shader.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* we use glslang to cross-compile from GLSL to SPIRV */
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#pragma comment(lib, "glslang-default-resource-limits.lib")
#pragma comment(lib, "glslang.lib")
#pragma comment(lib, "SPIRV-Tools-opt.lib")
#pragma comment(lib, "SPIRV-Tools.lib")

#ifdef _WIN32
    #define SC_ALLOCA(type, count) ((type*)memset(_alloca(sizeof(type) * (count)), 0, sizeof(type) * (count)))
#else
    #define SC_ALLOCA(type, size) ((type*)memset(alloca(sizeof(type) * (count)), 0, sizeof(type) * (count)))
#endif

#define SC_MAX(a,b) ((a) < (b) ? (b) : (a))

typedef struct SC_ArenaBlock SC_ArenaBlock;
struct SC_ArenaBlock {
    SC_ArenaBlock *next;
    char data;
};

typedef struct SC_Arena {
    SC_ArenaBlock *blocks;
    char *curr;
    char *end;
} SC_Arena;

void* sc__alloc(SC_Arena* arena, size_t size, size_t align) {
    if (!size) return NULL;
    char *curr = (char*)(((size_t)arena->curr + (align - 1)) & ~((size_t)align - 1));
    if (curr + size > arena->end) {
        size_t block_size = (size + 1024)*2;
        SC_ArenaBlock *block = (SC_ArenaBlock*)malloc(block_size);
        block->next = arena->blocks;
        arena->blocks = block;
        arena->curr = &block->data;
        arena->end = (char*)block + block_size;
        curr = (char*)(((size_t)arena->curr + (align - 1)) & ~((size_t)align - 1));
    }
    arena->curr = curr + size;
    return curr;
}

#ifdef _MSC_VER
    #define SC_ALIGNOF(type) __alignof(type)
#else
    #define SC_ALIGNOF(type) __alignof__(type)
#endif
#define SC_ALLOC(type, a, n) (type*)memset(sc__alloc(a, sizeof(type)*(n), SC_ALIGNOF(type)), 0, sizeof(type)*(n))
#define SC_STREQ(a,b) (!strcmp(a,b))

void sc__arena_destroy(SC_Arena *a) {
    for (SC_ArenaBlock *b = a->blocks, *next; b; b = next) {
        next = b->next;
        free(b);
    }
}

typedef struct SC_File {
    struct SC_File *next;
    char *path;
    char *prev_data;
    char *prev_s;
} SC_File;

typedef struct SC_Parser {
    char *data;
    char *s;
    SC_Arena *arena;
    SC_File *file;
} SC_Parser;

SC_Bool sc__match(SC_Parser *p, char *token) {
    char *s = p->s;
    char *t = token;
    while (isspace(*s)) if (*s++ == '\n') ++s;
    while (*s && *t && *s == *t) ++s, ++t;
    if (*t) return 0;
    p->s = s;
    return 1;
}

SC_Bool sc__match_identifier(SC_Parser *p, char *identifier) {
    char *s = p->s;
    char *t = identifier;
    while (isspace(*s)) if (*s++ == '\n') ++s;
    while (*s && *t && *s == *t) ++s, ++t;
    if (*t) return 0;
    if (isalnum(*s) || *s == '_') return 0;
    p->s = s;
    return 1;
}

char* sc__strcpy(SC_Arena *arena, char *start, char *end) {
    size_t len = end - start;
    char *str = SC_ALLOC(char, arena, len+1);
    memcpy(str, start, len);
    str[len] = 0;
    return str;
}

char* sc__strcat(SC_Arena *arena, ...) {
    /* calc length */
    va_list args;
    size_t len = 0;
    va_start(args, arena);
    while (1) {
        char *a = va_arg(args, char*);
        if (!a) break;
        char *b = va_arg(args, char*);
        len += b-a;
    }
    va_end(args);

    /* memcpy */
    char *curr, *result;
    va_start(args, arena);
    curr = result = SC_ALLOC(char, arena, len+1);
    while (1) {
        char *a = va_arg(args, char*);
        if (!a) break;
        char *b = va_arg(args, char*);
        memcpy(curr, a, b-a);
        curr += b-a;
    }
    va_end(args);

    *curr = 0;

    return result;
}

char* sc__consume_identifier(SC_Parser *p) {
    char *s = p->s;
    while (isspace(*s)) ++s;
    char *start = s;
    while (isalnum(*s) || *s == '_') ++s;
    char *end = s;
    if (start == end) return NULL;

    p->s = s;
    return sc__strcpy(p->arena, start, end);
}

int sc__parse_f64(const char *str, double *result) {
    const char *s = str;
    double res = 0;
    int neg = 0;
    if (*s == '-') neg = 1, ++s;
    else if (*s == '+') ++s;
    if (!isdigit(*s)) return 0;
    for (; isdigit(*s); ++s)
        res *= 10, res += *s - '0';
    double mul = 0.1;
    if (*s == '.')
        for (++s; isdigit(*s); ++s)
            res += mul * (*s - '0'), mul *= 0.1;
    *result = neg ? -res : res;
    return (int)(s - str);
}

int sc__parse_int(const char *str, long long *result) {
    long long res = 0;
    const char *s = str;
    long long neg = 1;
    if (*s == '-') neg = -1, ++s;
    else if (*s == '+') neg = 1, ++s;
    if (!isdigit(*s)) return 0;
    while (isdigit(*s)) res *= 10, res += *s - '0', ++s;
    *result = res * neg;
    return 1;
}

SC_Bool sc__consume_float(SC_Parser *p, float *result) {
    char *s = p->s;
    while (isspace(*s)) ++s;
    double d;
    int len = sc__parse_f64(s, &d);
    if (!len) return 0;
    *result = (float)d;
    p->s = s + len;
    return 1;
}

SC_Bool sc__consume_integer(SC_Parser *p, int *result) {
    char *s = p->s;
    while (isspace(*s)) ++s;
    long long i;
    int len = sc__parse_int(s, &i);
    if (!len) return 0;
    *result = (int)i;
    p->s = s + len;
    return 1;
}

void sc__consume_whitespace(SC_Parser *p) {
    char *s = p->s;
    while (isspace(*s)) ++s;
    p->s = s;
}

typedef struct SC_Writer {
    SC_Arena *arena;
    char *buf;
    int len;
    int cap;
} SC_Writer;

void sc__write(SC_Writer *w, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (w->len + len >= w->cap) {
        w->cap = (w->cap + len + 64)*2;
        char *buf = SC_ALLOC(char, w->arena, w->cap);
        memcpy(buf, w->buf, w->len);
        buf[w->len] = 0;
        w->buf = buf;
    }

    va_start(args, fmt);
    vsnprintf(w->buf + w->len, len+1, fmt, args);
    va_end(args);

    w->len += len;
}

void sc__errlog(SC_CodeLocation loc, const char *fmt, ...) {
    int line_number = 1;
    char *line = loc.start;
    for (char *c = loc.start; c < loc.pos; ++c)
        if (*c == '\n') ++line_number, line = c+1;
    fprintf(stderr, "Error %s:%i: ", loc.path, line_number);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    int line_len = 0;
    while (line[line_len] && line[line_len] != '\n' && line[line_len] != '\r') ++line_len;
    fprintf(stderr, "\n\n%.*s\n", line_len, line);
}
#define SC_ERROR(loc, ...) do{sc__errlog(loc, __VA_ARGS__); goto error;} while(0)
#define SC_PARSE_ERROR(...) do{SC_CodeLocation loc = {p.file->path, p.data, p.s}; SC_ERROR(loc, __VA_ARGS__);} while(0)

char* sc__read_file(SC_Arena *a, const char *path, int *len_out) {
    FILE *file = fopen(path, "rb");

    if (!file)
        return 0;

    // get file size
    long old_pos;
    int count;
    char* bytes;
    old_pos = ftell(file);
    if (fseek(file, 0, SEEK_END))
        goto err;
    count = ftell(file);
    if (fseek(file, old_pos, SEEK_SET))
        goto err;
    if (count <= 0)
        goto err;

    bytes = SC_ALLOC(char, a, count+1);
    fread(bytes, 1, count, file);
    bytes[count] = 0;
    fclose(file);
    if (len_out) *len_out = count;
    return bytes;

    err:
    fclose(file);
    return 0;
}

static const char sc__texture_format_options[] = "r8, rg8, rgba8, r16, rg16, rgba16, r16f, rg16f, rgba16f, r32f, rg32f, rgba32f, r11g11b10f";
static const char sc__depth_format_options[] = "d16, d24, d32f, d24_s8, d32f_s8";

SC_Bool sc__consume_blend(SC_Parser *p, SC_CodeLocation *blend_code_location, SC_BlendFactor *blend_src, SC_BlendFactor *blend_dst, SC_BlendOp *blend_op) {
    blend_code_location->start = p->data;
    blend_code_location->pos = p->s;
    blend_code_location->path = p->file->path;

    if (sc__match_identifier(p, "default")) {
        *blend_src = SC_BLEND_FACTOR_SRC_ALPHA;
        *blend_dst = SC_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        *blend_op = SC_BLEND_OP_ADD;
    } else if (sc__match_identifier(p, "off")) {
        *blend_src = 0;
        *blend_dst = 0;
        *blend_op = 0;
    } else {
        *blend_src = sc__consume_blend_factor(p);
        *blend_dst = sc__consume_blend_factor(p);
        *blend_op = sc__consume_blend_op(p);
        if (!*blend_src) SC_ERROR(*blend_code_location, "Invalid source blend factor. Options are: zero, one, src_color, one_minus_src_color, dst_color, one_minus_dst_color, src_alpha, one_minus_src_alpha, dst_alpha, one_minus_dst_alpha, constant_color, one_minus_constant_color, src_alpha_saturate");
        if (!*blend_dst) SC_ERROR(*blend_code_location, "Invalid source blend factor. Options are: zero, one, src_color, one_minus_src_color, dst_color, one_minus_dst_color, src_alpha, one_minus_src_alpha, dst_alpha, one_minus_dst_alpha, constant_color, one_minus_constant_color, src_alpha_saturate");
        if (!*blend_op) SC_ERROR(*blend_code_location, "Invalid blend operation. Options are: add, subtract, rev_subtract, min, max");
    }
    return 1;

    error:
    return 0;
}

SC_TextureFormat sc__consume_texture_format(SC_Parser *p) {
    if (sc__match_identifier(p, "r8")) return SC_TEXTURE_FORMAT_R8;
    if (sc__match_identifier(p, "rg8")) return SC_TEXTURE_FORMAT_RG8;
    if (sc__match_identifier(p, "rgba8")) return SC_TEXTURE_FORMAT_RGBA8;
    if (sc__match_identifier(p, "r16")) return SC_TEXTURE_FORMAT_R16;
    if (sc__match_identifier(p, "rg16")) return SC_TEXTURE_FORMAT_RG16;
    if (sc__match_identifier(p, "rgba16")) return SC_TEXTURE_FORMAT_RGBA16;
    if (sc__match_identifier(p, "r16f")) return SC_TEXTURE_FORMAT_R16F;
    if (sc__match_identifier(p, "rg16f")) return SC_TEXTURE_FORMAT_RG16F;
    if (sc__match_identifier(p, "rgba16f")) return SC_TEXTURE_FORMAT_RGBA16F;
    if (sc__match_identifier(p, "r32f")) return SC_TEXTURE_FORMAT_R32F;
    if (sc__match_identifier(p, "rg32f")) return SC_TEXTURE_FORMAT_RG32F;
    if (sc__match_identifier(p, "rgba32f")) return SC_TEXTURE_FORMAT_RGBA32F;
    if (sc__match_identifier(p, "r11g11b10f")) return SC_TEXTURE_FORMAT_R11G11B10F;
    if (sc__match_identifier(p, "d16")) return SC_TEXTURE_FORMAT_D16;
    if (sc__match_identifier(p, "d24")) return SC_TEXTURE_FORMAT_D24;
    if (sc__match_identifier(p, "d32f")) return SC_TEXTURE_FORMAT_D32F;
    if (sc__match_identifier(p, "d24_s8")) return SC_TEXTURE_FORMAT_D24_S8;
    if (sc__match_identifier(p, "d32f_s8")) return SC_TEXTURE_FORMAT_D32F_S8;
    return SC_TEXTURE_FORMAT_INVALID;
}

SC_CompareOp sc__consume_compare_op(SC_Parser *p) {
    if (sc__match_identifier(p, "never")) return SC_COMPARE_OP_NEVER;
    if (sc__match_identifier(p, "less")) return SC_COMPARE_OP_LESS;
    if (sc__match_identifier(p, "equal")) return SC_COMPARE_OP_EQUAL;
    if (sc__match_identifier(p, "less_or_equal")) return SC_COMPARE_OP_LESS_OR_EQUAL;
    if (sc__match_identifier(p, "greater")) return SC_COMPARE_OP_GREATER;
    if (sc__match_identifier(p, "not_equal")) return SC_COMPARE_OP_NOT_EQUAL;
    if (sc__match_identifier(p, "greater_or_equal")) return SC_COMPARE_OP_GREATER_OR_EQUAL;
    if (sc__match_identifier(p, "always")) return SC_COMPARE_OP_ALWAYS;
    return SC_COMPARE_OP_INVALID;
}

SC_CullMode sc__consume_cull_mode(SC_Parser *p) {
    if (sc__match_identifier(p, "none")) return SC_CULL_MODE_NONE;
    if (sc__match_identifier(p, "front")) return SC_CULL_MODE_FRONT;
    if (sc__match_identifier(p, "back")) return SC_CULL_MODE_BACK;
    return SC_CULL_MODE_INVALID;
}

SC_BlendFactor sc__consume_blend_factor(SC_Parser *p) {
    if (sc__match_identifier(p, "zero")) return SC_BLEND_FACTOR_ZERO;
    if (sc__match_identifier(p, "one")) return SC_BLEND_FACTOR_ONE;
    if (sc__match_identifier(p, "src_color")) return SC_BLEND_FACTOR_SRC_COLOR;
    if (sc__match_identifier(p, "one_minus_src_color")) return SC_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    if (sc__match_identifier(p, "dst_color")) return SC_BLEND_FACTOR_DST_COLOR;
    if (sc__match_identifier(p, "one_minus_dst_color")) return SC_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    if (sc__match_identifier(p, "src_alpha")) return SC_BLEND_FACTOR_SRC_ALPHA;
    if (sc__match_identifier(p, "one_minus_src_alpha")) return SC_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (sc__match_identifier(p, "dst_alpha")) return SC_BLEND_FACTOR_DST_ALPHA;
    if (sc__match_identifier(p, "one_minus_dst_alpha")) return SC_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    if (sc__match_identifier(p, "constant_color")) return SC_BLEND_FACTOR_CONSTANT_COLOR;
    if (sc__match_identifier(p, "one_minus_constant_color")) return SC_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    if (sc__match_identifier(p, "src_alpha_saturate")) return SC_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    return SC_BLEND_FACTOR_INVALID;
}

SC_BlendOp sc__consume_blend_op(SC_Parser *p) {
    if (sc__match_identifier(p, "add")) return SC_BLEND_OP_ADD;
    if (sc__match_identifier(p, "subtract")) return SC_BLEND_OP_SUBTRACT;
    if (sc__match_identifier(p, "rev_subtract")) return SC_BLEND_OP_REV_SUBTRACT;
    if (sc__match_identifier(p, "min")) return SC_BLEND_OP_MIN;
    if (sc__match_identifier(p, "max")) return SC_BLEND_OP_MAX;
    return SC_BLEND_OP_INVALID;
}

char* sc__consume_texture(SC_Parser *p) {
    char *format = 0;
    if (sc__match(p, "(")) {
        while (1) {
            if (sc__match(p, ",")) continue;
            else if (sc__match(p, ")")) break;
            else if (sc__match_identifier(p, "format")) {
                if (!sc__match(p, "=")) return 0;
                if (!(format = sc__consume_identifier(p))) return 0;
            }
        }
    }
    return format;
}

SC_Bool sc__glslang(glslang_stage_t stage, SC_Arena *arena, char *code, size_t code_size, uint32_t **result_code, size_t *result_code_size) {
    const char *shaderSource = code;

    glslang_input_t input = {0};
    input.language = GLSLANG_SOURCE_GLSL;
    input.stage = stage;
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_0;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_0;
    input.code = shaderSource;
    input.default_version = 100;
    input.default_profile = GLSLANG_NO_PROFILE;
    input.force_default_version_and_profile = false;
    input.forward_compatible = false;
    input.messages = GLSLANG_MSG_DEFAULT_BIT;
    input.resource = glslang_default_resource();

    glslang_shader_t* shader = glslang_shader_create(&input);

    if (!glslang_shader_preprocess(shader, &input)) {
        fprintf(stderr, "GLSL preprocessing failed\n");
        fprintf(stderr, "%s\n", glslang_shader_get_info_log(shader));
        fprintf(stderr, "%s\n", glslang_shader_get_info_debug_log(shader));
        fprintf(stderr, "%s\n", input.code);
        glslang_shader_delete(shader);
        return 0;
    }

    if (!glslang_shader_parse(shader, &input)) {
        fprintf(stderr, "GLSL parsing failed\n");
        fprintf(stderr, "%s\n", glslang_shader_get_info_log(shader));
        fprintf(stderr, "%s\n", glslang_shader_get_info_debug_log(shader));
        fprintf(stderr, "%s\n", glslang_shader_get_preprocessed_code(shader));
        glslang_shader_delete(shader);
        return 0;
    }

    glslang_program_t* program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        fprintf(stderr, "GLSL linking failed\n");
        fprintf(stderr, "%s\n", glslang_program_get_info_log(program));
        fprintf(stderr, "%s\n", glslang_program_get_info_debug_log(program));
        glslang_program_delete(program);
        glslang_shader_delete(shader);
        return 0;
    }

    glslang_program_SPIRV_generate(program, stage);

    size_t size = glslang_program_SPIRV_get_size(program);
    uint32_t *words = SC_ALLOC(uint32_t, arena, size);
    glslang_program_SPIRV_get(program, words);

    const char* spirv_messages = glslang_program_SPIRV_get_messages(program);
    if (spirv_messages)
        fprintf(stderr, "%s\b", spirv_messages);

    glslang_program_delete(program);
    glslang_shader_delete(shader);

    *result_code = words;
    *result_code_size = size;
    return 1;
}

SC_Bool sc_compile(const char *path, SC_OutputFormat output_format, SC_Result *result) {
    SC_Arena tmp = {0};
    SC_Arena arena = {0};

    memset(result, 0, sizeof(*result));

    int code_len;
    char *code = sc__read_file(&tmp, path, &code_len);
    char *code_end = code + code_len;
    if (!code) return fprintf(stderr, "%s: Couldn't open file\n", path), 0;

    enum {START, VERT, MID, FRAG, END} state = START;

    SC_File *file_info = SC_ALLOC(SC_File, &tmp, 1);
    file_info->path = (char*)path;
    SC_Parser p = {code, code, &arena, file_info};

    /* AST definitions */
    enum {
        SC_AstTextType,
        SC_AstVertTextType,
        SC_AstVertInType,
        SC_AstVertOutType,
        SC_AstVertTextureType,
        SC_AstVertBufferType,
        SC_AstVertSamplerType,
        SC_AstVertUniformType,
        SC_AstFragTextType,
        SC_AstFragTextureType,
        SC_AstFragBufferType,
        SC_AstFragSamplerType,
        SC_AstFragUniformType,
        SC_AstFragOutType,
        SC_AstType,
    };

    typedef struct SC_Ast          {int type; struct SC_Ast *next; SC_CodeLocation code_location;} SC_Ast;
    typedef struct SC_AstText      {SC_Ast base; char *start; char *end;} SC_AstText;
    typedef struct SC_AstVertText      {SC_Ast base; char *start; char *end;} SC_AstVertText;
    typedef struct SC_AstFragText      {SC_Ast base; char *start; char *end;} SC_AstFragText;
    typedef struct SC_AstVertIn    {SC_Ast base; SC_VertexInput attr;} SC_AstVertIn;
    typedef struct SC_AstVertOut   {SC_Ast base;} SC_AstVertOut;
    typedef struct SC_AstVertSampler   {SC_Ast base;} SC_AstVertSampler;
    typedef struct SC_AstVertBuffer    {SC_Ast base; SC_Bool readonly; SC_Bool writeonly;} SC_AstVertBuffer;
    typedef struct SC_AstVertTexture     {SC_Ast base; char *format;} SC_AstVertTexture;
    typedef struct SC_AstVertUniform   {SC_Ast base;} SC_AstVertUniform;
    typedef struct SC_AstFragOut   {SC_Ast base; SC_FragmentOutput out;} SC_AstFragOut;
    typedef struct SC_AstFragSampler   {SC_Ast base;} SC_AstFragSampler;
    typedef struct SC_AstFragBuffer    {SC_Ast base; SC_Bool readonly; SC_Bool writeonly;} SC_AstFragBuffer;
    typedef struct SC_AstFragTexture     {SC_Ast base; char *format;} SC_AstFragTexture;
    typedef struct SC_AstFragUniform   {SC_Ast base;} SC_AstFragUniform;

    SC_CodeLocation curr_blend_code_location = {0};
    SC_BlendFactor curr_blend_src = 0;
    SC_BlendFactor curr_blend_dst = 0;
    SC_BlendOp curr_blend_op = 0;

    SC_Ast *ast_root = 0;
    SC_Ast **ast = &ast_root;
    #define SC_AST_PUSH(_type, ...) do { \
        _type _ast = {_type##Type, NULL, p.file->path, p.data, p.s, ##__VA_ARGS__}; \
        _type *data = (_type*)SC_ALLOC(_type, &tmp, 1); \
        memcpy(data, &_ast, sizeof(_ast)); \
        *data = _ast; \
        *ast = &data->base; \
        ast = &data->base.next; \
    } while (0)

    char *last_pos = p.s;
    while (p.file) {
        if (*p.s != '@' && *p.s != 0) {++p.s; continue;}

        if (state == VERT)
            SC_AST_PUSH(SC_AstVertText, last_pos, p.s);
        else if (state == FRAG)
            SC_AST_PUSH(SC_AstFragText, last_pos, p.s);
        else
            SC_AST_PUSH(SC_AstText, last_pos, p.s);

        if (!*p.s) {
            p.data = p.file->prev_data;
            p.s = p.file->prev_s;
            p.file = p.file->next;
            last_pos = p.s;
            continue;
        }

        if (sc__match_identifier(&p, "@import")) {
            if (!sc__match(&p, "\"")) SC_PARSE_ERROR("Expected file to import");
            char *import_start = p.s;
            while (*p.s && *p.s != '"') ++p.s;
            char *import_end = p.s;
            if (!sc__match(&p, "\"")) SC_PARSE_ERROR("No end to import file path");

            /* find file */
            const char *dir_start = path;
            const char *dir_end = path + strlen(path);
            while (dir_end >= path && *dir_end != '/' && *dir_end != '\\') --dir_end;
            ++dir_end;

            char *file = sc__strcat(&tmp, dir_start, dir_end, import_start, import_end, 0);

            int contents_len;
            char *contents = sc__read_file(&tmp, file, &contents_len);
            if (!contents) SC_PARSE_ERROR("'%s': No such file", file);

            /* splice in the contents. This is so that the imported code also gets parsed */
            SC_File *f = SC_ALLOC(SC_File, &tmp, 1);
            f->next = p.file;
            f->path = file;
            f->prev_data = p.data;
            f->prev_s = p.s;
            p.file = f;
            p.data = contents;
            p.s = contents;
            goto next_token;
        }

        if (sc__match_identifier(&p, "@blend")) {
            if (!sc__consume_blend(&p, &curr_blend_code_location, &curr_blend_src, &curr_blend_dst, &curr_blend_op))
                goto error;
            goto next_token;
        }

        switch (state) {
        case START:

            if (sc__match_identifier(&p, "@vert")) {
                state = VERT;
            }
            else if (sc__match_identifier(&p, "@depth")) {
                result->depth_code_location.path = p.file->path;
                result->depth_code_location.pos = p.s;
                result->depth_code_location.start = p.data;
                /* compare op */
                if (sc__match_identifier(&p, "default")) {
                    result->depth_cmp = SC_COMPARE_OP_LESS;
                } else {
                    result->depth_cmp = sc__consume_compare_op(&p);
                    if (!result->depth_cmp) SC_PARSE_ERROR("Invalid depth compare function. Options are: never, less, equal, less_or_equal, greater, not_equal, greater_or_equal, always");
                }

                /* read/write */
                if (sc__match_identifier(&p, "write"))
                    result->depth_write = 1;
                else if (sc__match_identifier(&p, "read"))
                    result->depth_write = 0;
                else
                    SC_PARSE_ERROR("Expected depth read/write flag. Options are: read, write");

                result->depth_format = sc__consume_texture_format(&p);
                if (!result->depth_format) SC_PARSE_ERROR("Expected depth format after read/write. Valid formats are: %s", sc__depth_format_options);

                /* clip/clamp */
                if (sc__match_identifier(&p, "clip"))
                    result->depth_clip = 1;
                else if (sc__match_identifier(&p, "clamp"))
                    result->depth_clip = 0;
                else
                    SC_PARSE_ERROR("Invalid depth format. Options are: clamp, clip");
            }
            else if (sc__match_identifier(&p, "@cull")) {
                result->cull_code_location.path = p.file->path;
                result->cull_code_location.pos = p.s;
                result->cull_code_location.start = p.data;
                result->cull_mode = sc__consume_cull_mode(&p);
                if (!result->cull_mode) SC_PARSE_ERROR("Invalid cull mode value. Options are: none, front, back");
            }
            else if (sc__match_identifier(&p, "@multisample")) {
                int ms;
                if (!sc__consume_integer(&p, &ms)) SC_PARSE_ERROR("Expected number of samples.\nExample:\n@multisample 4");
                if (ms != 1 && ms != 2 && ms != 4 && ms != 8) SC_PARSE_ERROR("Invalid multisampling count. Supported values are 1,2,4,8");
                result->multisample_count = ms;
            }
            else {
                SC_PARSE_ERROR("Expected @vert to start vertex shader");
            }
            break;

        case VERT:
            if (sc__match_identifier(&p, "@in")) {
                SC_VertexInput attr = {0};
                attr.code_location.path = p.file->path;
                attr.code_location.pos = p.s;
                attr.code_location.start = p.data;
                if (sc__match(&p, "(")) {
                    while (1) {
                        if (sc__match(&p, ",")) continue;
                        else if (sc__match(&p, ")")) break;
                        else if (sc__match_identifier(&p, "buffer")) {
                            if (!sc__match(&p, "=")) SC_PARSE_ERROR("Expected '=' after 'buffer'. Example: @in(buffer=1) vec4 color;");
                            sc__consume_whitespace(&p);
                            if (!isdigit(*p.s)) SC_PARSE_ERROR("Expected buffer number. Example: @in(buffer=1) vec4 color;");
                            attr.buffer_slot = 0;
                            while (isdigit(*p.s))
                                attr.buffer_slot *= 10, attr.buffer_slot += *p.s - '0', ++p.s;
                        }
                        else if (sc__match_identifier(&p, "type")) {
                            if (!sc__match(&p, "=")) SC_PARSE_ERROR("Expected '=' after 'type'. Example: @in(type=u8) vec4 color;");
                            if (!(attr.component_type = sc__consume_identifier(&p))) SC_PARSE_ERROR("Expected component type. Example: @in(type=u8) vec4 color;");
                        }
                        else if (sc__match_identifier(&p, "instanced")) {
                            attr.instanced = 1;
                        }
                    }
                }

                attr.is_flat = sc__match_identifier(&p, "flat");

                if (!(attr.data_type = sc__consume_identifier(&p))) SC_PARSE_ERROR("Expected vertex attribute data type");
                if (!(attr.name = sc__consume_identifier(&p))) SC_PARSE_ERROR("Expected vertex attribute name");

                /* determine format of data */
                /* 1-component */
                if (SC_STREQ(attr.data_type, "float")) {
                    if (attr.component_type) SC_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SC_VERTEXELEMENTFORMAT_FLOAT, attr.size = 4, attr.align = 4;
                } else if (SC_STREQ(attr.data_type, "int")) {
                    if (attr.component_type) SC_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SC_VERTEXELEMENTFORMAT_INT, attr.size = 4, attr.align = 4;
                } else if (SC_STREQ(attr.data_type, "uint")) {
                    if (attr.component_type) SC_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SC_VERTEXELEMENTFORMAT_UINT, attr.size = 4, attr.align = 4;
                /* 2-component */
                } else if (SC_STREQ(attr.data_type, "vec2")) {
                    if (!attr.component_type) attr.format = SC_VERTEXELEMENTFORMAT_FLOAT2, attr.size = 8, attr.align = 4;
                    else if (SC_STREQ(attr.component_type, "u8")) attr.format = SC_VERTEXELEMENTFORMAT_UBYTE2_NORM, attr.size = 2, attr.align = 1;
                    else if (SC_STREQ(attr.component_type, "i8")) attr.format = SC_VERTEXELEMENTFORMAT_BYTE2_NORM, attr.size = 2, attr.align = 1;
                    else if (SC_STREQ(attr.component_type, "u16")) attr.format = SC_VERTEXELEMENTFORMAT_USHORT2_NORM, attr.size = 4, attr.align = 2;
                    else if (SC_STREQ(attr.component_type, "i16")) attr.format = SC_VERTEXELEMENTFORMAT_SHORT2_NORM, attr.size = 4, attr.align = 2;
                    else SC_PARSE_ERROR("Invalid attribute component_type. Possible values are u8, i8, u16, i16");
                } else if (SC_STREQ(attr.data_type, "ivec2")) {
                    if (!attr.component_type) attr.format = SC_VERTEXELEMENTFORMAT_INT2, attr.size = 8, attr.align = 4;
                    else if (SC_STREQ(attr.component_type, "i8")) attr.format = SC_VERTEXELEMENTFORMAT_BYTE2, attr.size = 2, attr.align = 1;
                    else if (SC_STREQ(attr.component_type, "i16")) attr.format = SC_VERTEXELEMENTFORMAT_SHORT2, attr.size = 4, attr.align = 2;
                    else SC_PARSE_ERROR("Invalid attribute component_type. Possible values are i8, i16");
                } else if (SC_STREQ(attr.data_type, "uvec2")) {
                    if (!attr.component_type) attr.format = SC_VERTEXELEMENTFORMAT_UINT2, attr.size = 8, attr.align = 4;
                    else if (SC_STREQ(attr.component_type, "u8")) attr.format = SC_VERTEXELEMENTFORMAT_UBYTE2, attr.size = 2, attr.align = 1;
                    else if (SC_STREQ(attr.component_type, "u16")) attr.format = SC_VERTEXELEMENTFORMAT_USHORT2, attr.size = 4, attr.align = 2;
                    else SC_PARSE_ERROR("Invalid attribute component_type. Possible values are u8, u16");
                /* 3-component */
                } else if (SC_STREQ(attr.data_type, "vec3")) {
                    if (attr.component_type) SC_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SC_VERTEXELEMENTFORMAT_FLOAT3, attr.size = 12, attr.align = 4;
                } else if (SC_STREQ(attr.data_type, "ivec3")) {
                    if (attr.component_type) SC_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SC_VERTEXELEMENTFORMAT_INT3, attr.size = 12, attr.align = 4;
                } else if (SC_STREQ(attr.data_type, "uvec3")) {
                    if (attr.component_type) SC_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SC_VERTEXELEMENTFORMAT_UINT3, attr.size = 12, attr.align = 4;
                /* 4-component */
                } else if (SC_STREQ(attr.data_type, "vec4")) {
                    if (!attr.component_type) attr.format = SC_VERTEXELEMENTFORMAT_FLOAT4, attr.size = 16, attr.align = 4;
                    else if (SC_STREQ(attr.component_type, "u8")) attr.format = SC_VERTEXELEMENTFORMAT_UBYTE4_NORM, attr.size = 4, attr.align = 1;
                    else if (SC_STREQ(attr.component_type, "i8")) attr.format = SC_VERTEXELEMENTFORMAT_BYTE4_NORM, attr.size = 4, attr.align = 1;
                    else if (SC_STREQ(attr.component_type, "u16")) attr.format = SC_VERTEXELEMENTFORMAT_USHORT4_NORM, attr.size = 8, attr.align = 2;
                    else if (SC_STREQ(attr.component_type, "i16")) attr.format = SC_VERTEXELEMENTFORMAT_SHORT4_NORM, attr.size = 8, attr.align = 2;
                    else SC_PARSE_ERROR("Invalid attribute component_type. Possible values are u8, i8, u16, i16");
                } else if (SC_STREQ(attr.data_type, "ivec4")) {
                    if (!attr.component_type) attr.format = SC_VERTEXELEMENTFORMAT_INT4, attr.size = 16, attr.align = 4;
                    else if (SC_STREQ(attr.component_type, "i8")) attr.format = SC_VERTEXELEMENTFORMAT_BYTE4, attr.size = 4, attr.align = 1;
                    else if (SC_STREQ(attr.component_type, "i16")) attr.format = SC_VERTEXELEMENTFORMAT_SHORT4, attr.size = 8, attr.align = 2;
                    else SC_PARSE_ERROR("Invalid attribute component_type. Possible values are i8, i16");
                } else if (SC_STREQ(attr.data_type, "uvec4")) {
                    if (!attr.component_type) attr.format = SC_VERTEXELEMENTFORMAT_UINT4, attr.size = 16, attr.align = 4;
                    else if (SC_STREQ(attr.component_type, "u8")) attr.format = SC_VERTEXELEMENTFORMAT_UBYTE4, attr.size = 4, attr.align = 1;
                    else if (SC_STREQ(attr.component_type, "u16")) attr.format = SC_VERTEXELEMENTFORMAT_USHORT4, attr.size = 8, attr.align = 2;
                    else SC_PARSE_ERROR("Invalid attribute component_type. Possible values are u8, u16");
                } else {
                    SC_PARSE_ERROR("Unknown attribute type. Supported values are float, int, uint, vec[n], ivec[n], uvec[n] for n=2,3,4");
                }

                if (!attr.format) SC_PARSE_ERROR("Unknown error - failed to determine vertex attribute format");

                SC_AST_PUSH(SC_AstVertIn, attr);
            }
            else if (sc__match_identifier(&p, "@sampler")) {
                SC_AST_PUSH(SC_AstVertSampler);
            }
            else if (sc__match_identifier(&p, "@image")) {
                char *format = sc__consume_texture(&p);
                if (!format) SC_PARSE_ERROR("You must specify the texture format.\nExample:\n@image(format=rgba8) mytexture;\n\nValid formats are: %s", sc__texture_format_options);
                SC_AST_PUSH(SC_AstVertTexture, format);
            }
            else if (sc__match_identifier(&p, "@buffer")) {
                SC_Bool readonly = sc__match_identifier(&p, "readonly");
                SC_Bool writeonly = sc__match_identifier(&p, "writeonly");
                SC_AST_PUSH(SC_AstVertBuffer, readonly, writeonly);
            }
            else if (sc__match_identifier(&p, "@uniform")) {
                SC_AST_PUSH(SC_AstVertUniform);
            }
            else if (sc__match_identifier(&p, "@out")) {
                SC_AST_PUSH(SC_AstVertOut);
                while (*p.s && *p.s != ';') ++p.s;
            }
            else if (sc__match_identifier(&p, "@end")) {
                state = MID;
            }
            else
                SC_PARSE_ERROR("Unknown command");
            break;

        case MID:
            if (!sc__match_identifier(&p, "@frag")) SC_PARSE_ERROR("Expected @frag to start fragment shader (or end of file to omit fragment shader)");
            result->has_fragment_shader = 1;
            state = FRAG;
            break;

        case END:
            SC_PARSE_ERROR("Nothing allowed past @end");
            break;

        case FRAG:
            if (sc__match_identifier(&p, "@out")) {
                SC_FragmentOutput output = {0};
                output.code_location.path = p.file->path;
                output.code_location.pos = p.s;
                output.code_location.start = p.data;
                if (sc__match(&p, "(")) {
                    while (1) {
                        if (sc__match(&p, ",")) continue;
                        else if (sc__match(&p, ")")) break;
                        else if (sc__match_identifier(&p, "format")) {
                            if (!sc__match(&p, "=")) SC_PARSE_ERROR("Expected '=' after 'format'. Example: @out(format=u8) vec4 color;");
                            if (!(output.format = sc__consume_texture_format(&p))) SC_PARSE_ERROR("Expected data format. Example: @out(format=rgba8) vec4 color; Valid formats are: %s", sc__texture_format_options);
                        }
                    }
                }
                output.blend_code_location = curr_blend_code_location;
                output.blend_src = curr_blend_src;
                output.blend_dst = curr_blend_dst;
                output.blend_op = curr_blend_op;
                SC_AST_PUSH(SC_AstFragOut, output);
            }
            else if (sc__match_identifier(&p, "@sampler")) {
                SC_AST_PUSH(SC_AstFragSampler);
            }
            else if (sc__match_identifier(&p, "@image")) {
                char *format = sc__consume_texture(&p);
                if (!format) SC_PARSE_ERROR("You must specify the texture format.\nExample:\n@image(format=rgba8) mytexture;\n\nValid formats are: %s", sc__texture_format_options);
                SC_AST_PUSH(SC_AstFragTexture, format);
            }
            else if (sc__match_identifier(&p, "@buffer")) {
                SC_Bool readonly = sc__match_identifier(&p, "readonly");
                SC_Bool writeonly = sc__match_identifier(&p, "writeonly");
                SC_AST_PUSH(SC_AstFragBuffer, readonly, writeonly);
            }
            else if (sc__match_identifier(&p, "@uniform")) {
                SC_AST_PUSH(SC_AstFragUniform);
            }
            else if (sc__match_identifier(&p, "@end")) {
                state = END;
            }
            else
                SC_PARSE_ERROR("Unknown command");
            break;
        }

        next_token:;
        last_pos = p.s;
        continue;
    }

    /* count number of stuff */
    int num_vertex_inputs = 0;
    int num_vertex_outputs = 0;
    int num_vertex_samplers = 0;
    int num_vertex_images = 0;
    int num_vertex_buffers = 0;
    int num_vertex_uniforms = 0;
    int num_fragment_outputs = 0;
    int num_fragment_samplers = 0;
    int num_fragment_images = 0;
    int num_fragment_buffers = 0;
    int num_fragment_uniforms = 0;
    for (SC_Ast *node = ast_root; node; node = node->next) {
        num_vertex_inputs += node->type == SC_AstVertInType;
        num_vertex_outputs += node->type == SC_AstVertOutType;
        num_vertex_samplers += node->type == SC_AstVertSamplerType;
        num_vertex_images += node->type == SC_AstVertTextureType;
        num_vertex_buffers += node->type == SC_AstVertBufferType;
        num_vertex_uniforms += node->type == SC_AstVertUniformType;
        num_fragment_outputs += node->type == SC_AstFragOutType;
        num_fragment_samplers += node->type == SC_AstFragSamplerType;
        num_fragment_images += node->type == SC_AstFragTextureType;
        num_fragment_buffers += node->type == SC_AstFragBufferType;
        num_fragment_uniforms += node->type == SC_AstFragUniformType;
    }

    /* gather all the vertex inputs */
    SC_VertexInput *vertex_inputs = SC_ALLOC(SC_VertexInput, &arena, num_vertex_inputs);
    {
        int vertex_input_index = 0;
        for (SC_Ast *node = ast_root; node; node = node->next)
            if (node->type == SC_AstVertInType)
                vertex_inputs[vertex_input_index++] = ((SC_AstVertIn*)node)->attr;
    }

    /* gather vertex input buffers */
    SC_VertexInputBuffer *vertex_input_buffers = NULL;
    int num_vertex_input_buffers = 0;
    {
        int max_vertex_input_buffer_slot = 0;
        for (int i = 0; i < num_vertex_inputs; ++i)
            max_vertex_input_buffer_slot = SC_MAX(max_vertex_input_buffer_slot, vertex_inputs[i].buffer_slot+1);
        vertex_input_buffers = SC_ALLOC(SC_VertexInputBuffer, &arena, max_vertex_input_buffer_slot);
        for (int i = 0; i < num_vertex_inputs; ++i) {
            SC_VertexInput *in = vertex_inputs + i;
            SC_VertexInputBuffer *buffer = NULL;
            for (int j = 0; j < num_vertex_input_buffers; ++j)
                if (vertex_input_buffers[j].slot == in->buffer_slot)
                    {buffer = &vertex_input_buffers[j]; break;}
            if (!buffer) {
                buffer = &vertex_input_buffers[num_vertex_input_buffers++];
                buffer->slot = in->buffer_slot;
                buffer->instanced = in->instanced;
            }
            else if (buffer->instanced != in->instanced)
                SC_ERROR(in->code_location, "All attributes for buffer %i must be specified as instanced", in->buffer_slot);
        }
    }

    /* calculate stride of vertex input buffers */
    for (int i = 0; i < num_vertex_input_buffers; ++i) {
        SC_VertexInputBuffer *bi = vertex_input_buffers + i;
        int size = 0, align = 0;
        for (int j = 0; j < num_vertex_inputs; ++j) {
            SC_VertexInput *vi = &vertex_inputs[j];
            if (vi->buffer_slot != bi->slot) continue;
            size = ((size + vi->align - 1) & ~(vi->align - 1)) + vi->size;
            align = SC_MAX(align, vi->align);
        }
        size = (size + align - 1) & ~(align - 1);
        bi->stride = size;
    }

    /* gather fragment outputs */
    SC_FragmentOutput *fragment_outputs = SC_ALLOC(SC_FragmentOutput, &arena, num_fragment_outputs);
    {
        int fragment_output_index = 0;
        for (SC_Ast *node = ast_root; node; node = node->next)
            if (node->type == SC_AstFragOutType)
                fragment_outputs[fragment_output_index++] = ((SC_AstFragOut*)node)->out;
    }

    /* write output shaders */
    SC_Writer vertex_output = {&arena};
    SC_Writer fragment_output = {&arena};
    sc__write(&vertex_output, "#version 450\n");
    sc__write(&fragment_output, "#version 450\n");

    switch (output_format) {
        case SC_OUTPUT_FORMAT_SDL: {
            int vertex_input_index = 0;
            int vertex_output_index = 0;
            int vertex_sampler_index = 0;
            int vertex_image_index = num_vertex_samplers;
            int vertex_buffer_index = num_vertex_samplers + num_vertex_images;
            int vertex_uniform_index = 0;
            int fragment_output_index = 0;
            int fragment_sampler_index = 0;
            int fragment_image_index = num_fragment_samplers;
            int fragment_buffer_index = num_fragment_samplers + num_fragment_images;
            int fragment_uniform_index = 0;
            for (SC_Ast *node = ast_root; node; node = node->next) {
                switch (node->type) {
                    case SC_AstTextType:
                    case SC_AstVertTextType:
                    case SC_AstFragTextType: {
                        SC_AstText *text = (SC_AstText*)node;
                        if (node->type == SC_AstVertTextType || node->type == SC_AstTextType)
                            sc__write(&vertex_output, "%.*s", (int)(text->end - text->start), text->start);
                        if (node->type == SC_AstFragTextType || node->type == SC_AstTextType)
                            sc__write(&fragment_output, "%.*s", (int)(text->end - text->start), text->start);
                        break;
                    }
                    case SC_AstVertInType: {
                        SC_AstVertIn *in = (SC_AstVertIn*)node;
                        sc__write(&vertex_output, "layout(location = %i) in%s%s %s", vertex_input_index, in->attr.is_flat ? " flat " : " ", in->attr.data_type, in->attr.name);
                        ++vertex_input_index;
                        break;
                    }
                    case SC_AstVertOutType: {
                        SC_AstVertOut *out = (SC_AstVertOut*)node;
                        char *rest = out->base.code_location.pos;
                        int rest_len = 0;
                        while (rest[rest_len] && rest[rest_len] != ';') ++rest_len;
                        sc__write(&vertex_output, "layout(location = %i) out%.*s", vertex_output_index, rest_len, rest);
                        sc__write(&fragment_output, "layout(location = %i) in%.*s;", vertex_output_index, rest_len, rest);
                        ++vertex_output_index;
                        break;
                    }
                    case SC_AstVertSamplerType: sc__write(&vertex_output, "layout(set = 0, binding = %i) uniform", vertex_sampler_index), ++vertex_sampler_index; break;
                    case SC_AstVertTextureType: {
                        SC_AstVertTexture *tex = (SC_AstVertTexture*)node;
                        sc__write(&vertex_output, "layout(set = 0, binding = %i, %s) uniform image2D", vertex_image_index, tex->format), ++vertex_image_index; break;
                        break;
                    }
                    case SC_AstVertBufferType: {
                        SC_AstVertBuffer *buf = (SC_AstVertBuffer*)node;
                        sc__write(&vertex_output, "layout(std140, set = 0, binding = %i) buffer%s Buffer%i", vertex_buffer_index, buf->readonly ? " readonly" : buf->writeonly ? "writeonly" : "", vertex_buffer_index);
                        ++vertex_buffer_index;
                        break;
                    }
                    case SC_AstVertUniformType: sc__write(&vertex_output, "layout(std140, set = 1, binding = %i) uniform Uniform%i", vertex_uniform_index, vertex_uniform_index), ++vertex_uniform_index; break;
                    case SC_AstFragOutType:     sc__write(&fragment_output, "layout(location = %i) out", fragment_output_index), ++fragment_output_index; break;
                    case SC_AstFragSamplerType: sc__write(&fragment_output, "layout(set = 2, binding = %i) uniform", fragment_sampler_index), ++fragment_sampler_index; break;
                    case SC_AstFragTextureType: {
                        SC_AstFragTexture *tex = (SC_AstFragTexture*)node;
                        sc__write(&fragment_output, "layout(set = 2, binding = %i, %s) uniform image2D", fragment_image_index, tex->format), ++fragment_image_index; break;
                        break;
                    }
                    case SC_AstFragBufferType: {
                        SC_AstFragBuffer *buf = (SC_AstFragBuffer*)node;
                        sc__write(&fragment_output, "layout(std140, set = 2, binding = %i) buffer%s Buffer%i", fragment_buffer_index, buf->readonly ? " readonly" : buf->writeonly ? "writeonly" : "", fragment_buffer_index);
                        ++fragment_buffer_index;
                        break;
                    }
                    case SC_AstFragUniformType: sc__write(&fragment_output, "layout(std140, set = 3, binding = %i) uniform Uniform%i", fragment_uniform_index, fragment_uniform_index), ++fragment_uniform_index; break;
                }
            }
            break;
        }
        default: return fprintf(stderr, "Invalid output format"), 0;
    }

    /* record some results */
    result->vertex_inputs = vertex_inputs;
    result->num_vertex_inputs = num_vertex_inputs;
    result->vertex_input_buffers = vertex_input_buffers;
    result->num_vertex_input_buffers = num_vertex_input_buffers;
    result->num_vertex_outputs = num_vertex_outputs;
    result->num_vertex_samplers = num_vertex_samplers;
    result->num_vertex_images = num_vertex_images;
    result->num_vertex_buffers = num_vertex_buffers;
    result->num_vertex_uniforms = num_vertex_uniforms;
    result->fragment_outputs = fragment_outputs;
    result->num_fragment_outputs = num_fragment_outputs;
    result->num_fragment_samplers = num_fragment_samplers;
    result->num_fragment_images = num_fragment_images;
    result->num_fragment_buffers = num_fragment_buffers;
    result->num_fragment_uniforms = num_fragment_uniforms;

    /* convert output code to SPIRV */
    result->vertex_code = vertex_output.buf;
    result->vertex_code_size = vertex_output.len;
    if (!sc__glslang(GLSLANG_STAGE_VERTEX, &arena, result->vertex_code, result->vertex_code_size, &result->spirv_vertex_code, &result->spirv_vertex_code_size))
        return 0;
    result->spirv_vertex_code_size *= 4;
    if (result->has_fragment_shader) {
        result->fragment_code = fragment_output.buf;
        result->fragment_code_size = fragment_output.len;
        if (!sc__glslang(GLSLANG_STAGE_FRAGMENT, &arena, result->fragment_code, result->fragment_code_size, &result->spirv_fragment_code, &result->spirv_fragment_code_size))
            return 0;
        result->spirv_fragment_code_size *= 4;
    }

    /* move the arena into the result */
    SC_Arena *arena_cpy = SC_ALLOC(SC_Arena, &arena, 1);
    *arena_cpy = arena;
    result->arena = arena_cpy;

    /* destroy temp arena */
    sc__arena_destroy(&tmp);
    return 1;

    error: {
        /* all memory lives in these two arenas */
        sc__arena_destroy(&tmp);
        sc__arena_destroy(&arena);
        return 0;
    }

    #undef SC_ERROR
    #undef SC_PARSE_ERROR
}

void sc_result_free(SC_Result *r) {
    if (r->arena)
        sc__arena_destroy(r->arena);
}

/* SDL Implementation */

#ifdef SDL_VERSION

static const SDL_GPUCullMode sc_to_sdl_cull_mode[] = {
    SDL_GPU_CULLMODE_NONE, /* SDL_GPU_CULLMODE_INVALID */
    SDL_GPU_CULLMODE_NONE, /* SDL_GPU_CULLMODE_NONE */
    SDL_GPU_CULLMODE_FRONT, /* SDL_GPU_CULLMODE_FRONT */
    SDL_GPU_CULLMODE_BACK, /* SDL_GPU_CULLMODE_BACK */
};

static const SDL_GPUVertexElementFormat sc_to_sdl_vertex_element_format[] = {
    SDL_GPU_VERTEXELEMENTFORMAT_INVALID, /* SC_VERTEXELEMENTFORMAT_INVALID */
    SDL_GPU_VERTEXELEMENTFORMAT_INT, /* SC_VERTEXELEMENTFORMAT_INT */
    SDL_GPU_VERTEXELEMENTFORMAT_INT2, /* SC_VERTEXELEMENTFORMAT_INT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_INT3, /* SC_VERTEXELEMENTFORMAT_INT3 */
    SDL_GPU_VERTEXELEMENTFORMAT_INT4, /* SC_VERTEXELEMENTFORMAT_INT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_UINT, /* SC_VERTEXELEMENTFORMAT_UINT */
    SDL_GPU_VERTEXELEMENTFORMAT_UINT2, /* SC_VERTEXELEMENTFORMAT_UINT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_UINT3, /* SC_VERTEXELEMENTFORMAT_UINT3 */
    SDL_GPU_VERTEXELEMENTFORMAT_UINT4, /* SC_VERTEXELEMENTFORMAT_UINT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, /* SC_VERTEXELEMENTFORMAT_FLOAT */
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, /* SC_VERTEXELEMENTFORMAT_FLOAT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, /* SC_VERTEXELEMENTFORMAT_FLOAT3 */
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, /* SC_VERTEXELEMENTFORMAT_FLOAT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE2, /* SC_VERTEXELEMENTFORMAT_BYTE2 */
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE4, /* SC_VERTEXELEMENTFORMAT_BYTE4 */
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2, /* SC_VERTEXELEMENTFORMAT_UBYTE2 */
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, /* SC_VERTEXELEMENTFORMAT_UBYTE4 */
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM, /* SC_VERTEXELEMENTFORMAT_BYTE2_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM, /* SC_VERTEXELEMENTFORMAT_BYTE4_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM, /* SC_VERTEXELEMENTFORMAT_UBYTE2_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, /* SC_VERTEXELEMENTFORMAT_UBYTE4_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT2, /* SC_VERTEXELEMENTFORMAT_SHORT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT4, /* SC_VERTEXELEMENTFORMAT_SHORT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, /* SC_VERTEXELEMENTFORMAT_USHORT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT4, /* SC_VERTEXELEMENTFORMAT_USHORT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM, /* SC_VERTEXELEMENTFORMAT_SHORT2_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM, /* SC_VERTEXELEMENTFORMAT_SHORT4_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM, /* SC_VERTEXELEMENTFORMAT_USHORT2_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM, /* SC_VERTEXELEMENTFORMAT_USHORT4_NORM */
};

static const SDL_GPUCompareOp sc_to_sdl_compare_op[] = {
    SDL_GPU_COMPAREOP_INVALID, /* SC_COMPARE_OP_INVALID */
    SDL_GPU_COMPAREOP_NEVER, /* SC_COMPARE_OP_NEVER */
    SDL_GPU_COMPAREOP_LESS, /* SC_COMPARE_OP_LESS */
    SDL_GPU_COMPAREOP_EQUAL, /* SC_COMPARE_OP_EQUAL */
    SDL_GPU_COMPAREOP_LESS_OR_EQUAL, /* SC_COMPARE_OP_LESS_OR_EQUAL */
    SDL_GPU_COMPAREOP_GREATER, /* SC_COMPARE_OP_GREATER */
    SDL_GPU_COMPAREOP_NOT_EQUAL, /* SC_COMPARE_OP_NOT_EQUAL */
    SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, /* SC_COMPARE_OP_GREATER_OR_EQUAL */
    SDL_GPU_COMPAREOP_ALWAYS, /* SC_COMPARE_OP_ALWAYS */
};

static const SDL_GPUTextureFormat sc_to_sdl_texture_format[] = {
    SDL_GPU_TEXTUREFORMAT_INVALID, /* SC_TEXTURE_FORMAT_INVALID */
    SDL_GPU_TEXTUREFORMAT_R8_UNORM, /* SC_TEXTURE_FORMAT_R8 */
    SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, /* SC_TEXTURE_FORMAT_RG8 */
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, /* SC_TEXTURE_FORMAT_RGBA8 */
    SDL_GPU_TEXTUREFORMAT_R16_UNORM, /* SC_TEXTURE_FORMAT_R16 */
    SDL_GPU_TEXTUREFORMAT_R16G16_UNORM, /* SC_TEXTURE_FORMAT_RG16 */
    SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM, /* SC_TEXTURE_FORMAT_RGBA16 */
    SDL_GPU_TEXTUREFORMAT_R16_FLOAT, /* SC_TEXTURE_FORMAT_R16F */
    SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT, /* SC_TEXTURE_FORMAT_RG16F */
    SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, /* SC_TEXTURE_FORMAT_RGBA16F */
    SDL_GPU_TEXTUREFORMAT_R32_FLOAT, /* SC_TEXTURE_FORMAT_R32F */
    SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT, /* SC_TEXTURE_FORMAT_RG32F */
    SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, /* SC_TEXTURE_FORMAT_RGBA32F */
    SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT, /* SC_TEXTURE_FORMAT_R11G11B10F */
    SDL_GPU_TEXTUREFORMAT_D16_UNORM, /* SC_TEXTURE_FORMAT_D16 */
    SDL_GPU_TEXTUREFORMAT_D24_UNORM, /* SC_TEXTURE_FORMAT_D24 */
    SDL_GPU_TEXTUREFORMAT_D32_FLOAT, /* SC_TEXTURE_FORMAT_D32F */
    SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, /* SC_TEXTURE_FORMAT_D24_S8 */
    SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, /* SC_TEXTURE_FORMAT_D32F_S8 */
};

static const SDL_GPUBlendFactor sc_to_sdl_blend_factor[] = {
    SDL_GPU_BLENDFACTOR_INVALID, /* SC_BLEND_FACTOR_INVALID */
    SDL_GPU_BLENDFACTOR_ZERO, /* SC_BLEND_FACTOR_ZERO */
    SDL_GPU_BLENDFACTOR_ONE, /* SC_BLEND_FACTOR_ONE */
    SDL_GPU_BLENDFACTOR_SRC_COLOR, /* SC_BLEND_FACTOR_SRC_COLOR */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR, /* SC_BLEND_FACTOR_ONE_MINUS_SRC_COLOR */
    SDL_GPU_BLENDFACTOR_DST_COLOR, /* SC_BLEND_FACTOR_DST_COLOR */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR, /* SC_BLEND_FACTOR_ONE_MINUS_DST_COLOR */
    SDL_GPU_BLENDFACTOR_SRC_ALPHA, /* SC_BLEND_FACTOR_SRC_ALPHA */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, /* SC_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA */
    SDL_GPU_BLENDFACTOR_DST_ALPHA, /* SC_BLEND_FACTOR_DST_ALPHA */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA, /* SC_BLEND_FACTOR_ONE_MINUS_DST_ALPHA */
    SDL_GPU_BLENDFACTOR_CONSTANT_COLOR, /* SC_BLEND_FACTOR_CONSTANT_COLOR */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR, /* SC_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR */
    SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE, /* SC_BLEND_FACTOR_SRC_ALPHA_SATURATE */
};

static const SDL_GPUBlendOp sc_to_sdl_blend_op[] = {
    SDL_GPU_BLENDOP_INVALID, /* SC_BLEND_OP_INVALID */
    SDL_GPU_BLENDOP_ADD, /* SC_BLEND_OP_ADD */
    SDL_GPU_BLENDOP_SUBTRACT, /* SC_BLEND_OP_SUBTRACT */
    SDL_GPU_BLENDOP_REVERSE_SUBTRACT, /* SC_BLEND_OP_REV_SUBTRACT */
    SDL_GPU_BLENDOP_MIN, /* SC_BLEND_OP_MIN */
    SDL_GPU_BLENDOP_MAX, /* SC_BLEND_OP_MAX */
};

static const SDL_GPUSampleCount sc_to_sdl_sample_count[] = {
    SDL_GPU_SAMPLECOUNT_1, /* 0 */
    SDL_GPU_SAMPLECOUNT_1, /* 1 */
    SDL_GPU_SAMPLECOUNT_2, /* 2 */
    SDL_GPU_SAMPLECOUNT_1, /* 3 */
    SDL_GPU_SAMPLECOUNT_4, /* 4 */
    SDL_GPU_SAMPLECOUNT_1, /* 5 */
    SDL_GPU_SAMPLECOUNT_1, /* 6 */
    SDL_GPU_SAMPLECOUNT_1, /* 7 */
    SDL_GPU_SAMPLECOUNT_8, /* 8 */
};

void sc_sdl_prefill_vertex_shader(SDL_GPUShaderCreateInfo *info, SC_Result *compiled) {
    memset(info, 0, sizeof(*info));
    info->code = (Uint8*)compiled->spirv_vertex_code;
    info->code_size = compiled->spirv_vertex_code_size;
    info->entrypoint = "main";
    info->format = SDL_GPU_SHADERFORMAT_SPIRV;
    info->stage = SDL_GPU_SHADERSTAGE_VERTEX;
    info->num_samplers = compiled->num_vertex_samplers;
    info->num_storage_textures = compiled->num_vertex_images;
    info->num_storage_buffers = compiled->num_vertex_buffers;
    info->num_uniform_buffers = compiled->num_vertex_uniforms;
    /* TODO: set name property */
}

void sc_sdl_prefill_fragment_shader(SDL_GPUShaderCreateInfo *info, SC_Result *compiled) {
    memset(info, 0, sizeof(*info));
    if (!compiled->has_fragment_shader) return;
    info->code = (Uint8*)compiled->spirv_fragment_code;
    info->code_size = compiled->spirv_fragment_code_size;
    info->entrypoint = "main";
    info->format = SDL_GPU_SHADERFORMAT_SPIRV;
    info->stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info->num_samplers = compiled->num_fragment_samplers;
    info->num_storage_textures = compiled->num_fragment_images;
    info->num_storage_buffers = compiled->num_fragment_buffers;
    info->num_uniform_buffers = compiled->num_fragment_uniforms;
    /* TODO: set name property */
}

void sc_sdl_prefill_pipeline(SDL_GPUGraphicsPipelineCreateInfo *info, SC_Result *sc) {
    memset(info, 0, sizeof(*info));
    SC_Arena *arena = sc->arena;

    /* set vertex_input_state */
    SDL_GPUVertexInputState* vert_info = &info->vertex_input_state;
    /* buffer descriptions */
    vert_info->num_vertex_buffers = sc->num_vertex_input_buffers;
    int num_vertex_buffers = sc->num_vertex_input_buffers;
    SDL_GPUVertexBufferDescription *vertex_buffer_descriptions = SC_ALLOC(SDL_GPUVertexBufferDescription, arena, vert_info->num_vertex_buffers);
    for (int i = 0; i < sc->num_vertex_input_buffers; ++i) {
        SDL_GPUVertexBufferDescription *desc = &vertex_buffer_descriptions[i];
        SC_VertexInputBuffer *buf = sc->vertex_input_buffers + i;
        desc->slot = buf->slot;
        desc->pitch = buf->stride;
        desc->input_rate = buf->instanced ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX;
    }
    vert_info->vertex_buffer_descriptions = vertex_buffer_descriptions;

    /* attributes */
    vert_info->num_vertex_attributes = sc->num_vertex_inputs;
    SDL_GPUVertexAttribute *vertex_attributes = SC_ALLOC(SDL_GPUVertexAttribute, arena, vert_info->num_vertex_attributes);
    for (int i = 0; i < sc->num_vertex_inputs; ++i) {
        SC_VertexInput *in = sc->vertex_inputs + i;
        SDL_GPUVertexAttribute *desc = &vertex_attributes[i];
        desc->location = i;
        desc->buffer_slot = in->buffer_slot;
        desc->offset = in->offset;
        desc->format = sc_to_sdl_vertex_element_format[in->format];
    }
    vert_info->vertex_attributes = vertex_attributes;

    /* rasterization */
    SDL_GPURasterizerState *rast_info = &info->rasterizer_state;
    rast_info->cull_mode = sc_to_sdl_cull_mode[sc->cull_mode];
    rast_info->enable_depth_clip = sc->depth_clip;

    /* multisampling */
    SDL_GPUMultisampleState *ms_info = &info->multisample_state;
    ms_info->sample_count = sc_to_sdl_sample_count[sc->multisample_count];

    /* depth stencil */
    SDL_GPUDepthStencilState *ds_info = &info->depth_stencil_state;
    ds_info->compare_op = sc_to_sdl_compare_op[sc->depth_cmp];
    ds_info->enable_depth_test = sc->depth_cmp != SC_COMPARE_OP_INVALID;
    ds_info->enable_depth_write = sc->depth_write;

    /* targets */
    SDL_GPUGraphicsPipelineTargetInfo *target_info = &info->target_info;
    target_info->num_color_targets = sc->num_fragment_outputs;
    if (target_info->num_color_targets) {
        SDL_GPUColorTargetDescription *color_target_descriptions = SC_ALLOC(SDL_GPUColorTargetDescription, arena, target_info->num_color_targets);
        for (int i = 0; i < (int)target_info->num_color_targets; ++i) {
            SDL_GPUColorTargetDescription *desc = &color_target_descriptions[i];
            SC_FragmentOutput *out = sc->fragment_outputs + i;
            desc->format = sc_to_sdl_texture_format[out->format];

            SDL_GPUColorTargetBlendState *blend = &desc->blend_state;
            blend->src_color_blendfactor = sc_to_sdl_blend_factor[out->blend_src];
            blend->dst_color_blendfactor = sc_to_sdl_blend_factor[out->blend_dst];
            blend->color_blend_op = sc_to_sdl_blend_op[out->blend_op];
            blend->src_alpha_blendfactor = sc_to_sdl_blend_factor[out->blend_src];
            blend->dst_alpha_blendfactor = sc_to_sdl_blend_factor[out->blend_dst];
            blend->alpha_blend_op = sc_to_sdl_blend_op[out->blend_op];
            blend->enable_blend = out->blend_op != SC_BLEND_OP_INVALID;
        }
        target_info->color_target_descriptions = color_target_descriptions;
    }
    target_info->depth_stencil_format = sc_to_sdl_texture_format[sc->depth_format];
    target_info->has_depth_stencil_target = sc->depth_cmp != SC_COMPARE_OP_INVALID;
}

#endif /* SDL_VERSION */
