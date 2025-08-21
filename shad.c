#include "shad.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/******

    Common utilities

******/

#ifdef _WIN32
    #define SHAD_ALLOCA(type, count) ((type*)memset(_alloca(sizeof(type) * (count)), 0, sizeof(type) * (count)))
#else
    #define SHAD_ALLOCA(type, size) ((type*)memset(alloca(sizeof(type) * (count)), 0, sizeof(type) * (count)))
#endif

#define SHAD_MAX(a,b) ((a) < (b) ? (b) : (a))

typedef struct ShadArenaBlock {
    struct ShadArenaBlock *next;
    char data;
} ShadArenaBlock;

typedef struct ShadArena {
    ShadArenaBlock *blocks;
    char *curr;
    char *end;
} ShadArena;

void* shad__alloc(ShadArena* arena, int size, int align) {
    char *curr = NULL;
    if (!size) return NULL;
    curr = (char*)(((size_t)arena->curr + (align - 1)) & ~((size_t)align - 1));
    if (curr + size > arena->end) {
        int block_size = (size + 1024)*2;
        ShadArenaBlock *block = (ShadArenaBlock*)malloc(block_size);
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
    #define SHAD_ALIGNOF(type) __alignof(type)
#else
    #define SHAD_ALIGNOF(type) __alignof__(type)
#endif
#define SHAD_ALLOC(type, a, n) (type*)memset(shad__alloc(a, sizeof(type)*(n), SHAD_ALIGNOF(type)), 0, sizeof(type)*(n))
#define SHAD_STREQ(a,b) (!strcmp(a,b))

void shad__arena_destroy(ShadArena *a) {
    ShadArenaBlock *b = a->blocks, *next = NULL;
    for (; b; b = next) {
        next = b->next;
        free(b);
    }
}

typedef struct ShadWriter {
    ShadArena *arena;
    char *buf;
    int len;
    int cap;
} ShadWriter;

int shad__vsnprintf(char *buffer, int size, const char *fmt, va_list args) {
    char *out = buffer;
    char *out_end = out + size;
    char buf[64];
    int i;
    unsigned u;
    int n;
    int neg;
    char *s;

    while (*fmt) {
        if (*fmt != '%') {buf[0] = *fmt++; s = buf; n = 1; goto add_ns;}
        ++fmt;
        if (*fmt == 's') {++fmt; s = va_arg(args, char*); goto add_s;}
        if (*fmt == 'S') {++fmt; s = va_arg(args, char*); n = (int)(va_arg(args, char*) - s); goto add_ns;}
        if (*fmt == 'i') {++fmt; i = va_arg(args, int); goto add_i;}
        if (*fmt == 'x') {++fmt; u = va_arg(args, unsigned); goto add_x;}

        add_i:
            s = buf + sizeof(buf);
            if (i < 0) neg = 1, i = -i; else neg = 0;
            if (!i) *--s = '0';
            while (i) *--s = '0' + i%10, i /= 10;
            if (neg) *--s = '-';
            n = (int)(buf + sizeof(buf) - s);
            goto add_ns;

        add_x:
            s = buf + sizeof(buf);
            if (!u) *--s = '0';
            while (u) *--s = "0123456789abcdef"[u%16], u /= 16;
            *--s = 'x';
            *--s = '0';
            n = (int)(buf + sizeof(buf) - s);
            goto add_ns;

        add_ns:
            while (n && out < out_end) *out++ = *s++, --n;
            out += n;
            continue;

        add_s:
            while (*s && out < out_end) *out++ = *s++;
            while (*s) ++out, ++s;
            continue;
    }

    if (out < out_end) *out = 0;

    return out - buffer;
}

int shad__snprintf(char *buffer, int size, const char *fmt, ...) {
    va_list args;
    int len;
    va_start(args, fmt);
    len = shad__vsnprintf(buffer, size, fmt, args);
    va_end(args);
    return len;
}

void shad__writer_print(ShadWriter *w, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    int len = shad__vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (w->len + len >= w->cap) {
        char *buf = NULL;
        w->cap = (w->cap + len + 64)*2;
        buf = SHAD_ALLOC(char, w->arena, w->cap);
        memcpy(buf, w->buf, w->len);
        buf[w->len] = 0;
        w->buf = buf;
    }

    va_start(args, fmt);
    shad__vsnprintf(w->buf + w->len, len+1, fmt, args);
    va_end(args);

    w->len += len;
}

void shad__writer_push(ShadWriter *w, char *data, int len) {
    if (w->len + len >= w->cap) {
        char *buf = NULL;
        w->cap = (w->cap + len + 64)*2;
        buf = SHAD_ALLOC(char, w->arena, w->cap);
        memcpy(buf, w->buf, w->len);
        buf[w->len] = 0;
        w->buf = buf;
    }
    memcpy(w->buf + w->len, data, len);
    w->len += len;
    w->buf[w->len] = 0;
}

void shad_result_free(ShadResult *r) {
    if (r->arena)
        shad__arena_destroy(r->arena);
}

/******

    Compiler mode

******/

#ifdef SHAD_COMPILER

/* we use glslang to cross-compile from GLSL to SPIRV */
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#ifdef _WIN32
#pragma comment(lib, "glslang-default-resource-limits.lib")
#pragma comment(lib, "glslang.lib")
#pragma comment(lib, "SPIRV-Tools-opt.lib")
#pragma comment(lib, "SPIRV-Tools.lib")
#endif

typedef struct ShadFile {
    struct ShadFile *next;
    char *path;
    char *prev_data;
    char *prev_s;
} ShadFile;

typedef struct ShadParser {
    char *data;
    char *s;
    ShadArena *arena;
    ShadFile *file;
} ShadParser;

ShadBool shad__isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

ShadBool shad__isalnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

ShadBool shad__isdigit(char c) {
    return c >= '0' && c <= '9';
}

ShadBool shad__match(ShadParser *p, char *token) {
    char *s;
    char *t;

    s = p->s;
    t = token;
    while (shad__isspace(*s)) if (*s++ == '\n') ++s;
    while (*s && *t && *s == *t) ++s, ++t;
    if (*t) return 0;
    p->s = s;
    return 1;
}

ShadBool shad__match_identifier(ShadParser *p, char *identifier) {
    char *s;
    char *t;

    s = p->s;
    t = identifier;
    while (shad__isspace(*s)) if (*s++ == '\n') ++s;
    while (*s && *t && *s == *t) ++s, ++t;
    if (*t) return 0;
    if (shad__isalnum(*s) || *s == '_') return 0;
    p->s = s;
    return 1;
}

char* shad__strcpy(ShadArena *arena, char *start, char *end) {
    int len;
    char *result;

    len = end - start;
    if (!len) return "";
    result = SHAD_ALLOC(char, arena, len+1);
    memcpy(result, start, len);
    result[len] = 0;
    return result;
}

char* shad__strcat(ShadArena *arena, ...) {
    va_list args;
    int len;
    char *curr;
    char *result;

    /* calc length */
    va_start(args, arena);
    len = 0;
    while (1) {
        char *a;
        char *b;

        a = va_arg(args, char*);
        if (!a) break;
        b = va_arg(args, char*);
        len += b-a;
    }
    va_end(args);

    /* memcpy */
    va_start(args, arena);
    curr = result = SHAD_ALLOC(char, arena, len+1);
    while (1) {
        char *a;
        char *b;

        a = va_arg(args, char*);
        if (!a) break;
        b = va_arg(args, char*);
        memcpy(curr, a, b-a);
        curr += b-a;
    }
    va_end(args);
    *curr = 0;
    return result;
}

void shad__filename(char *path, char **file_out, char **file_end_out) {
    char *path_end;
    char *file_start;
    char *file_end;

    path_end = file_end = path + strlen(path);
    while (file_end > path && *file_end != '.') --file_end;
    if (file_end == path) file_end = path_end;
    file_start = file_end;
    while (file_start > path && *file_start != '/' && *file_start != '\\') --file_start;
    if (file_start > path) ++file_start;
    *file_out = file_start;
    *file_end_out = file_end;
}

char* shad__consume_identifier(ShadParser *p) {
    char *s;
    char *start;
    char *end;

    s = p->s;
    while (shad__isspace(*s)) ++s;
    start = s;
    while (shad__isalnum(*s) || *s == '_') ++s;
    end = s;
    if (start == end) return NULL;

    p->s = s;
    return shad__strcpy(p->arena, start, end);
}

int shad__parse_f64(char *str, double *result) {
    char *s;
    double res;
    double mul;
    int neg;

    s = str;
    res = 0;
    neg = 0;
    if (*s == '-') neg = 1, ++s;
    else if (*s == '+') ++s;
    if (!shad__isdigit(*s)) return 0;
    for (; shad__isdigit(*s); ++s)
        res *= 10, res += *s - '0';
    mul = 0.1;
    if (*s == '.')
        for (++s; shad__isdigit(*s); ++s)
            res += mul * (*s - '0'), mul *= 0.1;
    *result = neg ? -res : res;
    return (int)(s - str);
}

int shad__parse_int(const char *str, int *result) {
    int res;
    const char *s;
    int neg;

    s = str;
    neg = 1;
    if (*s == '-') neg = -1, ++s;
    else if (*s == '+') neg = 1, ++s;
    if (!shad__isdigit(*s)) return 0;
    res = 0;
    while (shad__isdigit(*s)) res *= 10, res += *s - '0', ++s;
    *result = res * neg;
    return 1;
}

ShadBool shad__consume_float(ShadParser *p, float *float_out) {
    char *s;
    double result;
    int len;

    s = p->s;
    while (shad__isspace(*s)) ++s;
    len = shad__parse_f64(s, &result);
    if (!len) return 0;
    *float_out = (float)result;
    p->s = s + len;
    return 1;
}

ShadBool shad__consume_integer(ShadParser *p, int *int_out) {
    char *s;
    int len;

    s = p->s;
    while (shad__isspace(*s)) ++s;
    len = shad__parse_int(s, int_out);
    if (!len) return 0;
    p->s = s + len;
    return 1;
}

void shad__consume_whitespace(ShadParser *p) {
    char *s;

    s = p->s;
    while (shad__isspace(*s)) ++s;
    p->s = s;
}

void shad__errlog(ShadCodeLocation loc, const char *fmt, ...) {
    int line_number;
    char *line;
    va_list args;
    int line_len;
    char *iter;

    line_number = 1;
    line = loc.start;
    for (iter = loc.start; iter < loc.pos; ++iter)
        if (*iter == '\n') ++line_number, line = iter+1;
    fprintf(stderr, "Error %s:%i: ", loc.path, line_number);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    line_len = 0;
    while (line[line_len] && line[line_len] != '\n' && line[line_len] != '\r') ++line_len;
    fprintf(stderr, "\n\n%.*s\n", line_len, line);
}
#define SHAD_ERROR(loc, ...) do{shad__errlog(loc, __VA_ARGS__); goto error;} while(0)
#define SHAD_PARSE_ERROR(...) do{ShadCodeLocation loc = {p.file->path, p.data, p.s}; SHAD_ERROR(loc, __VA_ARGS__);} while(0)

char* shad__read_file(ShadArena *a, const char *path, int *len_out) {
    FILE *file = NULL;
    long old_pos;
    long count;
    char* bytes;

    file = fopen(path, "rb");

    if (!file)
        return 0;

    /* get file size */
    old_pos = ftell(file);
    if (fseek(file, 0, SEEK_END))
        goto err;
    count = ftell(file);
    if (fseek(file, old_pos, SEEK_SET))
        goto err;
    if (count <= 0)
        goto err;

    bytes = SHAD_ALLOC(char, a, count+1);
    if (fread(bytes, count, 1, file) != 1) goto err;
    bytes[count] = 0;
    fclose(file);
    if (len_out) *len_out = (int)count;
    return bytes;

    err:
    fclose(file);
    return 0;
}

static const char shad__texture_format_options[] = "r8, rg8, rgba8, r16, rg16, rgba16, r16f, rg16f, rgba16f, r32f, rg32f, rgba32f, r11g11b10f";
static const char shad__depth_format_options[] = "d16, d24, d32f, d24_s8, d32f_s8";

ShadTextureFormat shad__consume_texture_format(ShadParser *p) {
    if (shad__match_identifier(p, "r8")) return SHAD_TEXTURE_FORMAT_R8;
    if (shad__match_identifier(p, "rg8")) return SHAD_TEXTURE_FORMAT_RG8;
    if (shad__match_identifier(p, "rgba8")) return SHAD_TEXTURE_FORMAT_RGBA8;
    if (shad__match_identifier(p, "r16")) return SHAD_TEXTURE_FORMAT_R16;
    if (shad__match_identifier(p, "rg16")) return SHAD_TEXTURE_FORMAT_RG16;
    if (shad__match_identifier(p, "rgba16")) return SHAD_TEXTURE_FORMAT_RGBA16;
    if (shad__match_identifier(p, "r16f")) return SHAD_TEXTURE_FORMAT_R16F;
    if (shad__match_identifier(p, "rg16f")) return SHAD_TEXTURE_FORMAT_RG16F;
    if (shad__match_identifier(p, "rgba16f")) return SHAD_TEXTURE_FORMAT_RGBA16F;
    if (shad__match_identifier(p, "r32f")) return SHAD_TEXTURE_FORMAT_R32F;
    if (shad__match_identifier(p, "rg32f")) return SHAD_TEXTURE_FORMAT_RG32F;
    if (shad__match_identifier(p, "rgba32f")) return SHAD_TEXTURE_FORMAT_RGBA32F;
    if (shad__match_identifier(p, "r11g11b10f")) return SHAD_TEXTURE_FORMAT_R11G11B10F;
    if (shad__match_identifier(p, "d16")) return SHAD_TEXTURE_FORMAT_D16;
    if (shad__match_identifier(p, "d24")) return SHAD_TEXTURE_FORMAT_D24;
    if (shad__match_identifier(p, "d32f")) return SHAD_TEXTURE_FORMAT_D32F;
    if (shad__match_identifier(p, "d24_s8")) return SHAD_TEXTURE_FORMAT_D24_S8;
    if (shad__match_identifier(p, "d32f_s8")) return SHAD_TEXTURE_FORMAT_D32F_S8;
    return SHAD_TEXTURE_FORMAT_INVALID;
}

ShadCompareOp shad__consume_compare_op(ShadParser *p) {
    if (shad__match_identifier(p, "never")) return SHAD_COMPARE_OP_NEVER;
    if (shad__match_identifier(p, "less")) return SHAD_COMPARE_OP_LESS;
    if (shad__match_identifier(p, "equal")) return SHAD_COMPARE_OP_EQUAL;
    if (shad__match_identifier(p, "less_or_equal")) return SHAD_COMPARE_OP_LESS_OR_EQUAL;
    if (shad__match_identifier(p, "greater")) return SHAD_COMPARE_OP_GREATER;
    if (shad__match_identifier(p, "not_equal")) return SHAD_COMPARE_OP_NOT_EQUAL;
    if (shad__match_identifier(p, "greater_or_equal")) return SHAD_COMPARE_OP_GREATER_OR_EQUAL;
    if (shad__match_identifier(p, "always")) return SHAD_COMPARE_OP_ALWAYS;
    return SHAD_COMPARE_OP_INVALID;
}

ShadCullMode shad__consume_cull_mode(ShadParser *p) {
    if (shad__match_identifier(p, "none")) return SHAD_CULL_MODE_NONE;
    if (shad__match_identifier(p, "front")) return SHAD_CULL_MODE_FRONT;
    if (shad__match_identifier(p, "back")) return SHAD_CULL_MODE_BACK;
    return SHAD_CULL_MODE_INVALID;
}

ShadPrimitive shad__consume_primitive(ShadParser *p) {
    if (shad__match_identifier(p, "triangle_list")) return SHAD_PRIMITIVE_TRIANGLE_LIST;
    if (shad__match_identifier(p, "triangle_strip")) return SHAD_PRIMITIVE_TRIANGLE_STRIP;
    if (shad__match_identifier(p, "line_list")) return SHAD_PRIMITIVE_LINE_LIST;
    if (shad__match_identifier(p, "line_strip")) return SHAD_PRIMITIVE_LINE_STRIP;
    if (shad__match_identifier(p, "point_list")) return SHAD_PRIMITIVE_POINT_LIST;
    return SHAD_PRIMITIVE_INVALID;
}

ShadBlendFactor shad__consume_blend_factor(ShadParser *p) {
    if (shad__match_identifier(p, "zero")) return SHAD_BLEND_FACTOR_ZERO;
    if (shad__match_identifier(p, "one")) return SHAD_BLEND_FACTOR_ONE;
    if (shad__match_identifier(p, "src_color")) return SHAD_BLEND_FACTOR_SRC_COLOR;
    if (shad__match_identifier(p, "one_minus_src_color")) return SHAD_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    if (shad__match_identifier(p, "dst_color")) return SHAD_BLEND_FACTOR_DST_COLOR;
    if (shad__match_identifier(p, "one_minus_dst_color")) return SHAD_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    if (shad__match_identifier(p, "src_alpha")) return SHAD_BLEND_FACTOR_SRC_ALPHA;
    if (shad__match_identifier(p, "one_minus_src_alpha")) return SHAD_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (shad__match_identifier(p, "dst_alpha")) return SHAD_BLEND_FACTOR_DST_ALPHA;
    if (shad__match_identifier(p, "one_minus_dst_alpha")) return SHAD_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    if (shad__match_identifier(p, "constant_color")) return SHAD_BLEND_FACTOR_CONSTANT_COLOR;
    if (shad__match_identifier(p, "one_minus_constant_color")) return SHAD_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    if (shad__match_identifier(p, "src_alpha_saturate")) return SHAD_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    return SHAD_BLEND_FACTOR_INVALID;
}

ShadBlendOp shad__consume_blend_op(ShadParser *p) {
    if (shad__match_identifier(p, "add")) return SHAD_BLEND_OP_ADD;
    if (shad__match_identifier(p, "subtract")) return SHAD_BLEND_OP_SUBTRACT;
    if (shad__match_identifier(p, "rev_subtract")) return SHAD_BLEND_OP_REV_SUBTRACT;
    if (shad__match_identifier(p, "min")) return SHAD_BLEND_OP_MIN;
    if (shad__match_identifier(p, "max")) return SHAD_BLEND_OP_MAX;
    return SHAD_BLEND_OP_INVALID;
}

char* shad__consume_texture(ShadParser *p) {
    char *format = NULL;
    if (shad__match(p, "(")) {
        while (1) {
            if (shad__match(p, ",")) continue;
            else if (shad__match(p, ")")) break;
            else if (shad__match_identifier(p, "format")) {
                if (!shad__match(p, "=")) return 0;
                if (!(format = shad__consume_identifier(p))) return 0;
            }
        }
    }
    return format;
}

ShadBool shad__consume_blend(ShadParser *p, ShadCodeLocation *blend_code_location, ShadBlendFactor *blend_src, ShadBlendFactor *blend_dst, ShadBlendOp *blend_op) {
    blend_code_location->start = p->data;
    blend_code_location->pos = p->s;
    blend_code_location->path = p->file->path;

    if (shad__match_identifier(p, "default")) {
        *blend_src = SHAD_BLEND_FACTOR_SRC_ALPHA;
        *blend_dst = SHAD_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        *blend_op = SHAD_BLEND_OP_ADD;
    } else if (shad__match_identifier(p, "off")) {
        *blend_src = 0;
        *blend_dst = 0;
        *blend_op = 0;
    } else {
        *blend_src = shad__consume_blend_factor(p);
        *blend_dst = shad__consume_blend_factor(p);
        *blend_op = shad__consume_blend_op(p);
        if (!*blend_src) SHAD_ERROR(*blend_code_location, "Invalid source blend factor. Options are: zero, one, src_color, one_minus_src_color, dst_color, one_minus_dst_color, src_alpha, one_minus_src_alpha, dst_alpha, one_minus_dst_alpha, constant_color, one_minus_constant_color, src_alpha_saturate");
        if (!*blend_dst) SHAD_ERROR(*blend_code_location, "Invalid source blend factor. Options are: zero, one, src_color, one_minus_src_color, dst_color, one_minus_dst_color, src_alpha, one_minus_src_alpha, dst_alpha, one_minus_dst_alpha, constant_color, one_minus_constant_color, src_alpha_saturate");
        if (!*blend_op) SHAD_ERROR(*blend_code_location, "Invalid blend operation. Options are: add, subtract, rev_subtract, min, max");
    }
    return 1;

    error:
    return 0;
}

ShadBool shad__glslang(glslang_stage_t stage, ShadArena *arena, char *code, char **code_out, int *code_size_out) {
    glslang_input_t input = {0};
    glslang_shader_t* shader = NULL;
    glslang_program_t* program = NULL;
    const char *spirv_messages = NULL;
    int result_code_size = 0;
    char *result_code = NULL;

    input.language = GLSLANG_SOURCE_GLSL;
    input.stage = stage;
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_0;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_0;
    input.code = code;
    input.default_version = 100;
    input.default_profile = GLSLANG_NO_PROFILE;
    input.force_default_version_and_profile = false;
    input.forward_compatible = false;
    input.messages = GLSLANG_MSG_DEFAULT_BIT;
    input.resource = glslang_default_resource();

    shader = glslang_shader_create(&input);

    if (!glslang_shader_preprocess(shader, &input)) {
        fprintf(stderr, "GLSL preprocessing failed\n");
        fprintf(stderr, "%s\n", glslang_shader_get_info_log(shader));
        fprintf(stderr, "%s\n", glslang_shader_get_info_debug_log(shader));
        fprintf(stderr, "%s\n", input.code);
        goto error;
    }

    if (!glslang_shader_parse(shader, &input)) {
        fprintf(stderr, "GLSL parsing failed\n");
        fprintf(stderr, "%s\n", glslang_shader_get_info_log(shader));
        fprintf(stderr, "%s\n", glslang_shader_get_info_debug_log(shader));
        fprintf(stderr, "%s\n", glslang_shader_get_preprocessed_code(shader));
        goto error;
    }

    program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        fprintf(stderr, "GLSL linking failed\n");
        fprintf(stderr, "%s\n", glslang_program_get_info_log(program));
        fprintf(stderr, "%s\n", glslang_program_get_info_debug_log(program));
        goto error;
    }

    glslang_program_SPIRV_generate(program, stage);

    result_code_size = (int)glslang_program_SPIRV_get_size(program)*4;
    result_code = SHAD_ALLOC(char, arena, result_code_size);
    glslang_program_SPIRV_get(program, (unsigned*)result_code);

    spirv_messages = glslang_program_SPIRV_get_messages(program);
    if (spirv_messages)
        fprintf(stderr, "%s\b", spirv_messages);

    glslang_program_delete(program);
    glslang_shader_delete(shader);

    *code_out = result_code;
    *code_size_out = result_code_size;
    return 1;

    error:
    if (shader) glslang_shader_delete(shader);
    if (program) glslang_program_delete(program);
    return 0;
}

ShadBool shad_compile(const char *path, ShadOutputFormat output_format, ShadResult *result) {
    /* AST definitions */
    enum {
        ShadAstTextType,
        ShadAstVertTextType,
        ShadAstVertInType,
        ShadAstVertOutType,
        ShadAstVertTextureType,
        ShadAstVertBufferType,
        ShadAstVertSamplerType,
        ShadAstVertUniformType,
        ShadAstFragTextType,
        ShadAstFragTextureType,
        ShadAstFragBufferType,
        ShadAstFragSamplerType,
        ShadAstFragUniformType,
        ShadAstFragOutType,
        ShadAstType,
    };

    typedef struct ShadAst          {int type; struct ShadAst *next; ShadCodeLocation code_location;} ShadAst;
    typedef struct ShadAstText      {ShadAst base; char *start; char *end;} ShadAstText;
    typedef struct ShadAstVertText      {ShadAst base; char *start; char *end;} ShadAstVertText;
    typedef struct ShadAstFragText      {ShadAst base; char *start; char *end;} ShadAstFragText;
    typedef struct ShadAstVertIn    {ShadAst base; ShadVertexInput attr;} ShadAstVertIn;
    typedef struct ShadAstVertOut   {ShadAst base;} ShadAstVertOut;
    typedef struct ShadAstVertSampler   {ShadAst base;} ShadAstVertSampler;
    typedef struct ShadAstVertBuffer    {ShadAst base; ShadBool readonly; ShadBool writeonly;} ShadAstVertBuffer;
    typedef struct ShadAstVertTexture     {ShadAst base; char *format;} ShadAstVertTexture;
    typedef struct ShadAstVertUniform   {ShadAst base;} ShadAstVertUniform;
    typedef struct ShadAstFragOut   {ShadAst base; ShadFragmentOutput out;} ShadAstFragOut;
    typedef struct ShadAstFragSampler   {ShadAst base;} ShadAstFragSampler;
    typedef struct ShadAstFragBuffer    {ShadAst base; ShadBool readonly; ShadBool writeonly;} ShadAstFragBuffer;
    typedef struct ShadAstFragTexture     {ShadAst base; char *format;} ShadAstFragTexture;
    typedef struct ShadAstFragUniform   {ShadAst base;} ShadAstFragUniform;

    ShadArena tmp;
    ShadArena arena;
    ShadParser p;
    ShadCodeLocation curr_blend_code_location;
    ShadBlendFactor curr_blend_src;
    ShadBlendFactor curr_blend_dst;
    ShadBlendOp curr_blend_op;
    ShadAst *ast_root;
    ShadAst **ast_ptr;
    ShadAst *ast;
    char *last_pos;
    enum {START, VERT, MID, FRAG, END} state;
    int i, j, count;
    ShadWriter vertex_output;
    ShadWriter fragment_output;

    /* init */
    memset(&tmp, 0, sizeof(tmp));
    memset(&arena, 0, sizeof(arena));
    memset(&p, 0, sizeof(p));
    memset(&curr_blend_code_location, 0, sizeof(curr_blend_code_location));
    curr_blend_src = 0;
    curr_blend_dst = 0;
    curr_blend_op = 0;
    ast_root = NULL;
    ast_ptr = &ast_root;

    /* init result */
    memset(result, 0, sizeof(*result));

    /* init parser */
    memset(&p, 0, sizeof(p));
    p.arena = &arena;
    p.file = SHAD_ALLOC(ShadFile, &tmp, 1);
    p.file->path = (char*)path;
    p.s = p.data = shad__read_file(&tmp, path, NULL);
    if (!p.data) return fprintf(stderr, "%s: Couldn't open file\n", path), 0;

    #define SHAD_AST_PUSH(_type, ...) do { \
        _type _ast = {{_type##Type, NULL, {p.file->path, p.data, p.s}}, ##__VA_ARGS__}; \
        _type *data = (_type*)SHAD_ALLOC(_type, &tmp, 1); \
        memcpy(data, &_ast, sizeof(_ast)); \
        *data = _ast; \
        *ast_ptr = &data->base; \
        ast_ptr = &data->base.next; \
    } while (0)

    last_pos = p.s;
    state = START;
    while (p.file) {
        if (*p.s != '@' && *p.s != 0) {++p.s; continue;}

        if (state == VERT)
            SHAD_AST_PUSH(ShadAstVertText, last_pos, p.s);
        else if (state == FRAG)
            SHAD_AST_PUSH(ShadAstFragText, last_pos, p.s);
        else
            SHAD_AST_PUSH(ShadAstText, last_pos, p.s);

        if (!*p.s) {
            p.data = p.file->prev_data;
            p.s = p.file->prev_s;
            p.file = p.file->next;
            last_pos = p.s;
            continue;
        }

        if (shad__match_identifier(&p, "@import")) {
            char *import_start;
            char *import_end;
            const char *dir_start;
            const char *dir_end;
            char *file_path;
            char *file_contents;
            ShadFile *file;
            if (!shad__match(&p, "\"")) SHAD_PARSE_ERROR("Expected file to import");
            import_start = p.s;
            while (*p.s && *p.s != '"') ++p.s;
            import_end = p.s;
            if (!shad__match(&p, "\"")) SHAD_PARSE_ERROR("No end to import file path");

            /* find file */
            dir_start = path;
            dir_end = path + strlen(path);
            while (dir_end >= path && *dir_end != '/' && *dir_end != '\\') --dir_end;
            ++dir_end;

            file_path = shad__strcat(&tmp, dir_start, dir_end, import_start, import_end, 0);
            file_contents = shad__read_file(&tmp, file_path, NULL);
            if (!file_contents) SHAD_PARSE_ERROR("'%s': No such file", file_path);

            /* splice in the contents. This is so that the imported code also gets parsed */
            file = SHAD_ALLOC(ShadFile, &tmp, 1);
            file->next = p.file;
            file->path = file_path;
            file->prev_data = p.data;
            file->prev_s = p.s;
            p.file = file;
            p.data = file_contents;
            p.s = file_contents;
            goto next_token;
        }

        if (shad__match_identifier(&p, "@blend")) {
            if (!shad__consume_blend(&p, &curr_blend_code_location, &curr_blend_src, &curr_blend_dst, &curr_blend_op))
                goto error;
            goto next_token;
        }

        switch (state) {
        case START:
            if (shad__match_identifier(&p, "@vert")) {
                state = VERT;
            }
            else if (shad__match_identifier(&p, "@depth")) {
                result->depth_code_location.path = p.file->path;
                result->depth_code_location.pos = p.s;
                result->depth_code_location.start = p.data;
                /* compare op */
                if (shad__match_identifier(&p, "default")) {
                    result->depth_cmp = SHAD_COMPARE_OP_LESS;
                } else {
                    result->depth_cmp = shad__consume_compare_op(&p);
                    if (!result->depth_cmp) SHAD_PARSE_ERROR("Invalid depth compare function. Options are: never, less, equal, less_or_equal, greater, not_equal, greater_or_equal, always");
                }

                /* read/write */
                if (shad__match_identifier(&p, "write"))
                    result->depth_write = 1;
                else if (shad__match_identifier(&p, "read"))
                    result->depth_write = 0;
                else
                    SHAD_PARSE_ERROR("Expected depth read/write flag. Options are: read, write");

                result->depth_format = shad__consume_texture_format(&p);
                if (!result->depth_format) SHAD_PARSE_ERROR("Expected depth format after read/write. Valid formats are: %s", shad__depth_format_options);

                /* clip/clamp */
                if (shad__match_identifier(&p, "clip"))
                    result->depth_clip = 1;
                else if (shad__match_identifier(&p, "clamp"))
                    result->depth_clip = 0;
                else
                    SHAD_PARSE_ERROR("Invalid depth format. Options are: clamp, clip");
            }
            else if (shad__match_identifier(&p, "@cull")) {
                result->cull_code_location.path = p.file->path;
                result->cull_code_location.pos = p.s;
                result->cull_code_location.start = p.data;
                result->cull_mode = shad__consume_cull_mode(&p);
                if (!result->cull_mode) SHAD_PARSE_ERROR("Invalid cull mode value. Options are: none, front, back");
            }
            else if (shad__match_identifier(&p, "@primitive")) {
                result->primitive = shad__consume_primitive(&p);
                if (!result->primitive) SHAD_PARSE_ERROR("Invalid primitive value. Options are: triangle_list, triangle_strip, line_list, line_strip, point_list");
            }
            else if (shad__match_identifier(&p, "@multisample")) {
                int ms;
                if (!shad__consume_integer(&p, &ms)) SHAD_PARSE_ERROR("Expected number of samples.\nExample:\n@multisample 4");
                if (ms != 1 && ms != 2 && ms != 4 && ms != 8) SHAD_PARSE_ERROR("Invalid multisampling count. Supported values are 1,2,4,8");
                result->multisample_count = ms;
            }
            else {
                SHAD_PARSE_ERROR("Unknown command. Did you mean @vert to start the vertex shader?");
            }
            break;

        case VERT:
            if (shad__match_identifier(&p, "@in")) {
                ShadVertexInput attr = {0};
                attr.code_location.path = p.file->path;
                attr.code_location.pos = p.s;
                attr.code_location.start = p.data;
                if (shad__match(&p, "(")) {
                    while (1) {
                        if (shad__match(&p, ",")) continue;
                        else if (shad__match(&p, ")")) break;
                        else if (shad__match_identifier(&p, "buffer")) {
                            if (!shad__match(&p, "=")) SHAD_PARSE_ERROR("Expected '=' after 'buffer'. Example: @in(buffer=1) vec4 color;");
                            shad__consume_whitespace(&p);
                            if (!shad__isdigit(*p.s)) SHAD_PARSE_ERROR("Expected buffer number. Example: @in(buffer=1) vec4 color;");
                            attr.buffer_slot = 0;
                            while (shad__isdigit(*p.s))
                                attr.buffer_slot *= 10, attr.buffer_slot += *p.s - '0', ++p.s;
                        }
                        else if (shad__match_identifier(&p, "type")) {
                            if (!shad__match(&p, "=")) SHAD_PARSE_ERROR("Expected '=' after 'type'. Example: @in(type=u8) vec4 color;");
                            if (!(attr.component_type = shad__consume_identifier(&p))) SHAD_PARSE_ERROR("Expected component type. Example: @in(type=u8) vec4 color;");
                        }
                        else if (shad__match_identifier(&p, "instanced")) {
                            attr.instanced = 1;
                        }
                    }
                }

                attr.is_flat = shad__match_identifier(&p, "flat");

                if (!(attr.data_type = shad__consume_identifier(&p))) SHAD_PARSE_ERROR("Expected vertex attribute data type");
                if (!(attr.name = shad__consume_identifier(&p))) SHAD_PARSE_ERROR("Expected vertex attribute name");

                /* determine format of data */
                /* 1-component */
                if (SHAD_STREQ(attr.data_type, "float")) {
                    if (attr.component_type) SHAD_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SHAD_VERTEXELEMENTFORMAT_FLOAT, attr.size = 4, attr.align = 4;
                } else if (SHAD_STREQ(attr.data_type, "int")) {
                    if (attr.component_type) SHAD_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SHAD_VERTEXELEMENTFORMAT_INT, attr.size = 4, attr.align = 4;
                } else if (SHAD_STREQ(attr.data_type, "uint")) {
                    if (attr.component_type) SHAD_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SHAD_VERTEXELEMENTFORMAT_UINT, attr.size = 4, attr.align = 4;
                /* 2-component */
                } else if (SHAD_STREQ(attr.data_type, "vec2")) {
                    if (!attr.component_type) attr.format = SHAD_VERTEXELEMENTFORMAT_FLOAT2, attr.size = 8, attr.align = 4;
                    else if (SHAD_STREQ(attr.component_type, "u8")) attr.format = SHAD_VERTEXELEMENTFORMAT_UBYTE2_NORM, attr.size = 2, attr.align = 1;
                    else if (SHAD_STREQ(attr.component_type, "i8")) attr.format = SHAD_VERTEXELEMENTFORMAT_BYTE2_NORM, attr.size = 2, attr.align = 1;
                    else if (SHAD_STREQ(attr.component_type, "u16")) attr.format = SHAD_VERTEXELEMENTFORMAT_USHORT2_NORM, attr.size = 4, attr.align = 2;
                    else if (SHAD_STREQ(attr.component_type, "i16")) attr.format = SHAD_VERTEXELEMENTFORMAT_SHORT2_NORM, attr.size = 4, attr.align = 2;
                    else SHAD_PARSE_ERROR("Invalid attribute component_type. Possible values are u8, i8, u16, i16");
                } else if (SHAD_STREQ(attr.data_type, "ivec2")) {
                    if (!attr.component_type) attr.format = SHAD_VERTEXELEMENTFORMAT_INT2, attr.size = 8, attr.align = 4;
                    else if (SHAD_STREQ(attr.component_type, "i8")) attr.format = SHAD_VERTEXELEMENTFORMAT_BYTE2, attr.size = 2, attr.align = 1;
                    else if (SHAD_STREQ(attr.component_type, "i16")) attr.format = SHAD_VERTEXELEMENTFORMAT_SHORT2, attr.size = 4, attr.align = 2;
                    else SHAD_PARSE_ERROR("Invalid attribute component_type. Possible values are i8, i16");
                } else if (SHAD_STREQ(attr.data_type, "uvec2")) {
                    if (!attr.component_type) attr.format = SHAD_VERTEXELEMENTFORMAT_UINT2, attr.size = 8, attr.align = 4;
                    else if (SHAD_STREQ(attr.component_type, "u8")) attr.format = SHAD_VERTEXELEMENTFORMAT_UBYTE2, attr.size = 2, attr.align = 1;
                    else if (SHAD_STREQ(attr.component_type, "u16")) attr.format = SHAD_VERTEXELEMENTFORMAT_USHORT2, attr.size = 4, attr.align = 2;
                    else SHAD_PARSE_ERROR("Invalid attribute component_type. Possible values are u8, u16");
                /* 3-component */
                } else if (SHAD_STREQ(attr.data_type, "vec3")) {
                    if (attr.component_type) SHAD_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SHAD_VERTEXELEMENTFORMAT_FLOAT3, attr.size = 12, attr.align = 4;
                } else if (SHAD_STREQ(attr.data_type, "ivec3")) {
                    if (attr.component_type) SHAD_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SHAD_VERTEXELEMENTFORMAT_INT3, attr.size = 12, attr.align = 4;
                } else if (SHAD_STREQ(attr.data_type, "uvec3")) {
                    if (attr.component_type) SHAD_PARSE_ERROR("No component_type allowed for attribute type '%s' (only 2 and 4-component values allowed, mostly because of Metal)", attr.component_type);
                    attr.format = SHAD_VERTEXELEMENTFORMAT_UINT3, attr.size = 12, attr.align = 4;
                /* 4-component */
                } else if (SHAD_STREQ(attr.data_type, "vec4")) {
                    if (!attr.component_type) attr.format = SHAD_VERTEXELEMENTFORMAT_FLOAT4, attr.size = 16, attr.align = 4;
                    else if (SHAD_STREQ(attr.component_type, "u8")) attr.format = SHAD_VERTEXELEMENTFORMAT_UBYTE4_NORM, attr.size = 4, attr.align = 1;
                    else if (SHAD_STREQ(attr.component_type, "i8")) attr.format = SHAD_VERTEXELEMENTFORMAT_BYTE4_NORM, attr.size = 4, attr.align = 1;
                    else if (SHAD_STREQ(attr.component_type, "u16")) attr.format = SHAD_VERTEXELEMENTFORMAT_USHORT4_NORM, attr.size = 8, attr.align = 2;
                    else if (SHAD_STREQ(attr.component_type, "i16")) attr.format = SHAD_VERTEXELEMENTFORMAT_SHORT4_NORM, attr.size = 8, attr.align = 2;
                    else SHAD_PARSE_ERROR("Invalid attribute component_type. Possible values are u8, i8, u16, i16");
                } else if (SHAD_STREQ(attr.data_type, "ivec4")) {
                    if (!attr.component_type) attr.format = SHAD_VERTEXELEMENTFORMAT_INT4, attr.size = 16, attr.align = 4;
                    else if (SHAD_STREQ(attr.component_type, "i8")) attr.format = SHAD_VERTEXELEMENTFORMAT_BYTE4, attr.size = 4, attr.align = 1;
                    else if (SHAD_STREQ(attr.component_type, "i16")) attr.format = SHAD_VERTEXELEMENTFORMAT_SHORT4, attr.size = 8, attr.align = 2;
                    else SHAD_PARSE_ERROR("Invalid attribute component_type. Possible values are i8, i16");
                } else if (SHAD_STREQ(attr.data_type, "uvec4")) {
                    if (!attr.component_type) attr.format = SHAD_VERTEXELEMENTFORMAT_UINT4, attr.size = 16, attr.align = 4;
                    else if (SHAD_STREQ(attr.component_type, "u8")) attr.format = SHAD_VERTEXELEMENTFORMAT_UBYTE4, attr.size = 4, attr.align = 1;
                    else if (SHAD_STREQ(attr.component_type, "u16")) attr.format = SHAD_VERTEXELEMENTFORMAT_USHORT4, attr.size = 8, attr.align = 2;
                    else SHAD_PARSE_ERROR("Invalid attribute component_type. Possible values are u8, u16");
                } else {
                    SHAD_PARSE_ERROR("Unknown attribute type. Supported values are float, int, uint, vec[n], ivec[n], uvec[n] for n=2,3,4");
                }

                if (!attr.format) SHAD_PARSE_ERROR("Unknown error - failed to determine vertex attribute format");

                SHAD_AST_PUSH(ShadAstVertIn, attr);
            }
            else if (shad__match_identifier(&p, "@sampler")) {
                SHAD_AST_PUSH(ShadAstVertSampler);
            }
            else if (shad__match_identifier(&p, "@image")) {
                char *format = shad__consume_texture(&p);
                if (!format) SHAD_PARSE_ERROR("You must specify the texture format.\nExample:\n@image(format=rgba8) mytexture;\n\nValid formats are: %s", shad__texture_format_options);
                SHAD_AST_PUSH(ShadAstVertTexture, format);
            }
            else if (shad__match_identifier(&p, "@buffer")) {
                ShadBool readonly = shad__match_identifier(&p, "readonly");
                ShadBool writeonly = shad__match_identifier(&p, "writeonly");
                SHAD_AST_PUSH(ShadAstVertBuffer, readonly, writeonly);
            }
            else if (shad__match_identifier(&p, "@uniform")) {
                SHAD_AST_PUSH(ShadAstVertUniform);
            }
            else if (shad__match_identifier(&p, "@out")) {
                SHAD_AST_PUSH(ShadAstVertOut);
                while (*p.s && *p.s != ';') ++p.s;
            }
            else if (shad__match_identifier(&p, "@end")) {
                state = MID;
            }
            else
                SHAD_PARSE_ERROR("Unknown command");
            break;

        case MID:
            if (!shad__match_identifier(&p, "@frag")) SHAD_PARSE_ERROR("Expected @frag to start fragment shader (or end of file to omit fragment shader)");
            result->has_fragment_shader = 1;
            state = FRAG;
            break;

        case END:
            SHAD_PARSE_ERROR("Nothing allowed past @end");
            break;

        case FRAG:
            if (shad__match_identifier(&p, "@out")) {
                ShadFragmentOutput output = {0};
                output.code_location.path = p.file->path;
                output.code_location.pos = p.s;
                output.code_location.start = p.data;
                if (shad__match(&p, "(")) {
                    while (1) {
                        if (shad__match(&p, ",")) continue;
                        else if (shad__match(&p, ")")) break;
                        else if (shad__match_identifier(&p, "format")) {
                            if (!shad__match(&p, "=")) SHAD_PARSE_ERROR("Expected '=' after 'format'. Example: @out(format=u8) vec4 color;");
                            if (!(output.format = shad__consume_texture_format(&p))) SHAD_PARSE_ERROR("Expected data format. Example: @out(format=rgba8) vec4 color; Valid formats are: %s", shad__texture_format_options);
                        }
                    }
                }
                output.blend_code_location = curr_blend_code_location;
                output.blend_src = curr_blend_src;
                output.blend_dst = curr_blend_dst;
                output.blend_op = curr_blend_op;
                SHAD_AST_PUSH(ShadAstFragOut, output);
            }
            else if (shad__match_identifier(&p, "@sampler")) {
                SHAD_AST_PUSH(ShadAstFragSampler);
            }
            else if (shad__match_identifier(&p, "@image")) {
                char *format = shad__consume_texture(&p);
                if (!format) SHAD_PARSE_ERROR("You must specify the texture format.\nExample:\n@image(format=rgba8) mytexture;\n\nValid formats are: %s", shad__texture_format_options);
                SHAD_AST_PUSH(ShadAstFragTexture, format);
            }
            else if (shad__match_identifier(&p, "@buffer")) {
                ShadBool readonly = shad__match_identifier(&p, "readonly");
                ShadBool writeonly = shad__match_identifier(&p, "writeonly");
                SHAD_AST_PUSH(ShadAstFragBuffer, readonly, writeonly);
            }
            else if (shad__match_identifier(&p, "@uniform")) {
                SHAD_AST_PUSH(ShadAstFragUniform);
            }
            else if (shad__match_identifier(&p, "@end")) {
                state = END;
            }
            else
                SHAD_PARSE_ERROR("Unknown command");
            break;
        }

        next_token:;
        last_pos = p.s;
        continue;
    }

    /* count number of stuff */
    for (ast = ast_root; ast; ast = ast->next) {
        result->num_vertex_inputs     += ast->type == ShadAstVertInType;
        result->num_vertex_outputs    += ast->type == ShadAstVertOutType;
        result->num_vertex_samplers   += ast->type == ShadAstVertSamplerType;
        result->num_vertex_images     += ast->type == ShadAstVertTextureType;
        result->num_vertex_buffers    += ast->type == ShadAstVertBufferType;
        result->num_vertex_uniforms   += ast->type == ShadAstVertUniformType;
        result->num_fragment_outputs  += ast->type == ShadAstFragOutType;
        result->num_fragment_samplers += ast->type == ShadAstFragSamplerType;
        result->num_fragment_images   += ast->type == ShadAstFragTextureType;
        result->num_fragment_buffers  += ast->type == ShadAstFragBufferType;
        result->num_fragment_uniforms += ast->type == ShadAstFragUniformType;
    }

    /* gather all the vertex inputs */
    result->vertex_inputs = SHAD_ALLOC(ShadVertexInput, &arena, result->num_vertex_inputs);
    for (i = 0, ast = ast_root; ast; ast = ast->next)
        if (ast->type == ShadAstVertInType)
            result->vertex_inputs[i++] = ((ShadAstVertIn*)ast)->attr;

    /* gather vertex input buffers */
    for (i = 0, count = 0; i < result->num_vertex_inputs; ++i)
        count = SHAD_MAX(count, result->vertex_inputs[i].buffer_slot+1);
    result->vertex_input_buffers = SHAD_ALLOC(ShadVertexInputBuffer, &arena, count);
    for (i = 0; i < result->num_vertex_inputs; ++i) {
        ShadVertexInput *in = result->vertex_inputs + i;
        ShadVertexInputBuffer *buffer = NULL;
        for (j = 0; j < result->num_vertex_input_buffers; ++j)
            if (result->vertex_input_buffers[j].slot == in->buffer_slot)
                {buffer = &result->vertex_input_buffers[j]; break;}
        if (!buffer) {
            buffer = &result->vertex_input_buffers[result->num_vertex_input_buffers++];
            buffer->slot = in->buffer_slot;
            buffer->instanced = in->instanced;
        }
        else if (buffer->instanced != in->instanced)
            SHAD_ERROR(in->code_location, "All attributes for buffer %i must be specified as instanced", in->buffer_slot);
    }

    /* calculate stride of vertex input buffers */
    for (i = 0; i < result->num_vertex_input_buffers; ++i) {
        int size = 0, align = 0;
        ShadVertexInputBuffer *bi = result->vertex_input_buffers + i;
        for (j = 0; j < result->num_vertex_inputs; ++j) {
            ShadVertexInput *vi = &result->vertex_inputs[j];
            if (vi->buffer_slot != bi->slot) continue;
            size = ((size + vi->align - 1) & ~(vi->align - 1)) + vi->size;
            align = SHAD_MAX(align, vi->align);
        }
        size = (size + align - 1) & ~(align - 1);
        bi->stride = size;
    }

    /* gather fragment outputs */
    result->fragment_outputs = SHAD_ALLOC(ShadFragmentOutput, &arena, result->num_fragment_outputs);
    for (i = 0, ast = ast_root; ast; ast = ast->next)
        if (ast->type == ShadAstFragOutType)
            result->fragment_outputs[i++] = ((ShadAstFragOut*)ast)->out;

    /* write output shaders */
    memset(&vertex_output, 0, sizeof(vertex_output));
    memset(&fragment_output, 0, sizeof(fragment_output));
    vertex_output.arena = &arena;
    fragment_output.arena = &arena;
    shad__writer_print(&vertex_output, "#version 450\n");
    shad__writer_print(&fragment_output, "#version 450\n");

    switch (output_format) {
        case SHAD_OUTPUT_FORMAT_SDL: {
            int vertex_input_index = 0;
            int vertex_output_index = 0;
            int vertex_sampler_index = 0;
            int vertex_image_index = result->num_vertex_samplers;
            int vertex_buffer_index = result->num_vertex_samplers + result->num_vertex_images;
            int vertex_uniform_index = 0;
            int fragment_output_index = 0;
            int fragment_sampler_index = 0;
            int fragment_image_index = result->num_fragment_samplers;
            int fragment_buffer_index = result->num_fragment_samplers + result->num_fragment_images;
            int fragment_uniform_index = 0;
            for (ast = ast_root; ast; ast = ast->next) {
                switch (ast->type) {
                    case ShadAstTextType:
                    case ShadAstVertTextType:
                    case ShadAstFragTextType: {
                        ShadAstText *text = (ShadAstText*)ast;
                        if (ast->type == ShadAstVertTextType || ast->type == ShadAstTextType)
                            shad__writer_print(&vertex_output, "%S", text->start, text->end);
                        if (ast->type == ShadAstFragTextType || ast->type == ShadAstTextType)
                            shad__writer_print(&fragment_output, "%S", text->start, text->end);
                        break;
                    }
                    case ShadAstVertInType: {
                        ShadAstVertIn *in = (ShadAstVertIn*)ast;
                        shad__writer_print(&vertex_output, "layout(location = %i) in%s%s %s", vertex_input_index, in->attr.is_flat ? " flat " : " ", in->attr.data_type, in->attr.name);
                        ++vertex_input_index;
                        break;
                    }
                    case ShadAstVertOutType: {
                        ShadAstVertOut *out = (ShadAstVertOut*)ast;
                        char *rest = out->base.code_location.pos;
                        char *rest_end = rest;
                        while (*rest_end && *rest_end != ';') ++rest_end;
                        shad__writer_print(&vertex_output, "layout(location = %i) out%S", vertex_output_index, rest, rest_end);
                        shad__writer_print(&fragment_output, "layout(location = %i) in%S;", vertex_output_index, rest, rest_end);
                        ++vertex_output_index;
                        break;
                    }
                    case ShadAstVertSamplerType: shad__writer_print(&vertex_output, "layout(set = 0, binding = %i) uniform", vertex_sampler_index), ++vertex_sampler_index; break;
                    case ShadAstVertTextureType: {
                        ShadAstVertTexture *tex = (ShadAstVertTexture*)ast;
                        shad__writer_print(&vertex_output, "layout(set = 0, binding = %i, %s) uniform image2D", vertex_image_index, tex->format), ++vertex_image_index; break;
                        break;
                    }
                    case ShadAstVertBufferType: {
                        ShadAstVertBuffer *buf = (ShadAstVertBuffer*)ast;
                        shad__writer_print(&vertex_output, "layout(std140, set = 0, binding = %i) buffer%s Buffer%i", vertex_buffer_index, buf->readonly ? " readonly" : buf->writeonly ? "writeonly" : "", vertex_buffer_index);
                        ++vertex_buffer_index;
                        break;
                    }
                    case ShadAstVertUniformType: shad__writer_print(&vertex_output, "layout(std140, set = 1, binding = %i) uniform Uniform%i", vertex_uniform_index, vertex_uniform_index), ++vertex_uniform_index; break;
                    case ShadAstFragOutType:     shad__writer_print(&fragment_output, "layout(location = %i) out", fragment_output_index), ++fragment_output_index; break;
                    case ShadAstFragSamplerType: shad__writer_print(&fragment_output, "layout(set = 2, binding = %i) uniform", fragment_sampler_index), ++fragment_sampler_index; break;
                    case ShadAstFragTextureType: {
                        ShadAstFragTexture *tex = (ShadAstFragTexture*)ast;
                        shad__writer_print(&fragment_output, "layout(set = 2, binding = %i, %s) uniform image2D", fragment_image_index, tex->format), ++fragment_image_index; break;
                        break;
                    }
                    case ShadAstFragBufferType: {
                        ShadAstFragBuffer *buf = (ShadAstFragBuffer*)ast;
                        shad__writer_print(&fragment_output, "layout(std140, set = 2, binding = %i) buffer%s Buffer%i", fragment_buffer_index, buf->readonly ? " readonly" : buf->writeonly ? "writeonly" : "", fragment_buffer_index);
                        ++fragment_buffer_index;
                        break;
                    }
                    case ShadAstFragUniformType: shad__writer_print(&fragment_output, "layout(std140, set = 3, binding = %i) uniform Uniform%i", fragment_uniform_index, fragment_uniform_index), ++fragment_uniform_index; break;
                }
            }
            break;
        }
        default: return fprintf(stderr, "Invalid output format"), 0;
    }

    /* convert output code to SPIRV */
    result->vertex_code = vertex_output.buf;
    result->vertex_code_size = vertex_output.len;
    if (!shad__glslang(GLSLANG_STAGE_VERTEX, &arena, result->vertex_code, &result->spirv_vertex_code, &result->spirv_vertex_code_size))
        return 0;
    if (result->has_fragment_shader) {
        result->fragment_code = fragment_output.buf;
        result->fragment_code_size = fragment_output.len;
        if (!shad__glslang(GLSLANG_STAGE_FRAGMENT, &arena, result->fragment_code, &result->spirv_fragment_code, &result->spirv_fragment_code_size))
            return 0;
    }

    /* move the arena into the result */
    ShadArena *arena_cpy = SHAD_ALLOC(ShadArena, &arena, 1);
    *arena_cpy = arena;
    result->arena = arena_cpy;

    /* destroy temp arena */
    shad__arena_destroy(&tmp);
    return 1;

    error: {
        /* all memory lives in these two arenas */
        shad__arena_destroy(&tmp);
        shad__arena_destroy(&arena);
        return 0;
    }

    #undef SHAD_ERROR
    #undef SHAD_PARSE_ERROR
}

#endif /* SHAD_COMPILER */

void shad__serialize_spirv_to_code(ShadWriter *w, char *spirv, int spirv_size) {
    while (spirv_size) shad__writer_print(w, "%x,", *(unsigned*)spirv), spirv += 4, spirv_size -= 4;
}

void shad_serialize_to_c(const ShadResult *shad_result, const char *name, char **code_out, int *num_bytes_out) {
    ShadResult *result = (ShadResult*)shad_result;
    ShadArena tmp = {0};
    ShadWriter writer = {&tmp, NULL, 0, 0};
    int i;

    /* vertex spirv */
    shad__writer_print(&writer, "unsigned shad__spirv_vertex_code_%s[%i] = {", name, (int)result->spirv_vertex_code_size/4);
    shad__serialize_spirv_to_code(&writer, result->spirv_vertex_code, result->spirv_vertex_code_size);
    shad__writer_print(&writer, "};\n");

    /* fragment spirv */
    if (result->has_fragment_shader) {
        shad__writer_print(&writer, "unsigned shad__spirv_fragment_code_%s[%i] = {", name, (int)result->spirv_fragment_code_size/4);
        shad__serialize_spirv_to_code(&writer, result->spirv_fragment_code, result->spirv_fragment_code_size);
        shad__writer_print(&writer, "};\n");
    } else {
        shad__writer_print(&writer, "unsigned shad__spirv_fragment_code_%s[1] = {0}", name);
    }

    /* vertex inputs */
    if (result->num_vertex_inputs) {
        shad__writer_print(&writer, "ShadVertexInput shad__vertex_inputs_%s[%i] = {\n", name, (int)result->num_vertex_inputs);
        for (i = 0; i < result->num_vertex_inputs; ++i) {
            ShadVertexInput *in = result->vertex_inputs + i;
            shad__writer_print(&writer,
            "    {\n"
            "        {0},                         /* code_location */\n"
            "        NULL,                        /* component_type */\n"
            "        NULL,                        /* data_type */\n"
            "        NULL,                        /* name */\n"
            "        (ShadVertexElementFormat)%i, /* format */\n"
            "        %i,                          /* buffer_slot */\n"
            "        0,                           /* size */\n"
            "        0,                           /* align */\n"
            "        %i                           /* offset */\n"
            "        0,                           /* is_flat */\n"
            "        %i,                          /* instanced */\n"
            "    },\n",
            (int)in->format,
            (int)in->buffer_slot,
            (int)in->offset,
            (int)in->instanced);
        }
        shad__writer_print(&writer, "};\n");
    } else {
        shad__writer_print(&writer, "ShadVertexInput shad__vertex_inputs_%s[1] = {0};\n", name);
    }

    /* vertex input buffers */
    if (result->num_vertex_input_buffers) {
        shad__writer_print(&writer, "ShadVertexInputBuffer shad__vertex_input_buffers_%s[%i] = {\n", name, (int)result->num_vertex_input_buffers);
        for (i = 0; i < result->num_vertex_input_buffers; ++i) {
            ShadVertexInputBuffer *in = result->vertex_input_buffers + i;
            shad__writer_print(&writer,
            "    {\n"
            "        %i, /* slot */\n"
            "        %i, /* instanced */\n"
            "        %i, /* stride */\n"
            "    },\n",
            (int)in->slot,
            (int)in->instanced,
            (int)in->stride);
        }
        shad__writer_print(&writer, "};\n");
    } else {
        shad__writer_print(&writer, "ShadVertexInputBuffer shad__vertex_input_buffers_%s[1] = {0};\n", name);
    }

    /* fragment outputs */
    if (result->num_fragment_outputs) {
        shad__writer_print(&writer, "ShadFragmentOutput shad__fragment_outputs_%s[%i] = {\n", name, (int)result->num_fragment_outputs);
        for (i = 0; i < result->num_fragment_outputs; ++i) {
            ShadFragmentOutput *fout = result->fragment_outputs + i;
            shad__writer_print(&writer,
            "    {\n"
            "         {0},                   /* code_location */\n"
            "         (ShadTextureFormat)%i, /* format */\n"
            "         {0},                   /* blend_code_location */\n"
            "         (ShadBlendFactor)%i,   /* blend_src */\n"
            "         (ShadBlendFactor)%i,   /* blend_dst */\n"
            "         (ShadBlendOp)%i,       /* blend_op */\n"
            "    },\n",
            (int)fout->format,
            (int)fout->blend_src,
            (int)fout->blend_dst,
            (int)fout->blend_op);
        }
        shad__writer_print(&writer, "};\n");
    } else {
        shad__writer_print(&writer, "ShadFragmentOutput shad__fragment_outputs_%s[1] = {0};\n", name);
    }

    /* ShadResult */
    shad__writer_print(&writer,
        "ShadResult shad__result_%s = {\n"
        "    NULL, /* vertex_code */\n"
        "    0, /* vertex_code_size */\n"
        "    (char*)shad__spirv_vertex_code_%s, /* spirv_vertex_code */\n"
        "    %i, /* spirv_vertex_code_size */\n"
        "    shad__vertex_inputs_%s, /* vertex_inputs */\n"
        "    %i, /* num_vertex_inputs */\n"
        "    shad__vertex_input_buffers_%s, /* vertex_input_buffers */\n"
        "    %i, /* num_vertex_input_buffers */\n"
        "    %i, /* num_vertex_outputs */\n"
        "    %i, /* num_vertex_samplers */\n"
        "    %i, /* num_vertex_images */\n"
        "    %i, /* num_vertex_buffers */\n"
        "    %i, /* num_vertex_uniforms */\n"
        "    %i, /* has_fragment_shader */\n"
        "    NULL, /* fragment_code */\n"
        "    0, /* fragment_code_size */\n"
        "    (char*)shad__spirv_fragment_code_%s, /* spirv_fragment_code */\n"
        "    %i, /* spirv_fragment_code_size */\n"
        "    shad__fragment_outputs_%s, /* fragment_outputs */\n"
        "    %i, /* num_fragment_outputs */\n"
        "    %i, /* num_fragment_samplers */\n"
        "    %i, /* num_fragment_images */\n"
        "    %i, /* num_fragment_buffers */\n"
        "    %i, /* num_fragment_uniforms */\n"
        "    {0}, /* depth_code_location */\n"
        "    %i, /* depth_write */\n"
        "    (ShadCompareOp)%i, /* depth_cmp */\n"
        "    (ShadTextureFormat)%i, /* depth_format */\n"
        "    %i, /* depth_clip */\n"
        "    {0}, /* cull_code_location */\n"
        "    (ShadCullMode)%i, /* cull_mode */\n"
        "    (ShadPrimitive)%i, /* primitive */\n"
        "    %i, /* multisample_count */\n"
        "    NULL, /* arena */\n"
        "};\n",
        name,
        name,
        result->spirv_vertex_code_size,
        name,
        result->num_vertex_inputs,
        name,
        (int)result->num_vertex_input_buffers,
        (int)result->num_vertex_outputs,
        (int)result->num_vertex_samplers,
        (int)result->num_vertex_images,
        (int)result->num_vertex_buffers,
        (int)result->num_vertex_uniforms,
        (int)result->has_fragment_shader,
        name,
        result->spirv_fragment_code_size,
        name,
        (int)result->num_fragment_outputs,
        (int)result->num_fragment_samplers,
        (int)result->num_fragment_images,
        (int)result->num_fragment_buffers,
        (int)result->num_fragment_uniforms,
        (int)result->depth_write,
        (int)result->depth_cmp,
        (int)result->depth_format,
        (int)result->depth_clip,
        (int)result->cull_mode,
        (int)result->primitive,
        (int)result->multisample_count
    );

    shad__writer_print(&writer, "ShadResult* shad_result_%s(void) {return &shad__result_%s;}\n", name, name);

    int len = writer.len;
    char *output = (char*)malloc(len+1);
    memcpy(output, writer.buf, len);
    output[len] = 0;
    *code_out = output;
    *num_bytes_out = len;

    shad__arena_destroy(&tmp);
}

void shad_serialize(const ShadResult *compiled, char **bytes_out, int *num_bytes_out) {
    ShadArena tmp = {0};
    ShadWriter writer = {&tmp, NULL, 0, 0};
    int i;

    #define SHAD_WRITE_N(ptr, size) shad__writer_push(&writer, (char*)(ptr), size);
    #define SHAD_WRITE(ptr) SHAD_WRITE_N(ptr, sizeof(*(ptr)))

    /* vertex shader info */
    SHAD_WRITE(&compiled->spirv_vertex_code_size);
    SHAD_WRITE_N(compiled->spirv_vertex_code, compiled->spirv_vertex_code_size);
    SHAD_WRITE(&compiled->num_vertex_inputs);
    for (i = 0; i < compiled->num_vertex_inputs; ++i) {
        SHAD_WRITE(&compiled->vertex_inputs[i].format);
        SHAD_WRITE(&compiled->vertex_inputs[i].buffer_slot);
        SHAD_WRITE(&compiled->vertex_inputs[i].offset);
        SHAD_WRITE(&compiled->vertex_inputs[i].instanced);
    }
    SHAD_WRITE(&compiled->num_vertex_input_buffers);
    for (i = 0; i < compiled->num_vertex_input_buffers; ++i) {
        SHAD_WRITE(&compiled->vertex_input_buffers[i].slot);
        SHAD_WRITE(&compiled->vertex_input_buffers[i].instanced);
        SHAD_WRITE(&compiled->vertex_input_buffers[i].stride);
    }
    SHAD_WRITE(&compiled->num_vertex_input_buffers);
    SHAD_WRITE(&compiled->num_vertex_outputs);
    SHAD_WRITE(&compiled->num_vertex_samplers);
    SHAD_WRITE(&compiled->num_vertex_images);
    SHAD_WRITE(&compiled->num_vertex_buffers);
    SHAD_WRITE(&compiled->num_vertex_uniforms);

    /* fragment shader info */
    SHAD_WRITE(&compiled->has_fragment_shader);
    SHAD_WRITE(&compiled->spirv_fragment_code_size);
    SHAD_WRITE_N(compiled->spirv_fragment_code, compiled->spirv_fragment_code_size);
    SHAD_WRITE(&compiled->num_fragment_outputs);
    for (i = 0; i < compiled->num_fragment_outputs; ++i) {
        SHAD_WRITE(&compiled->fragment_outputs[i].format);
        SHAD_WRITE(&compiled->fragment_outputs[i].blend_src);
        SHAD_WRITE(&compiled->fragment_outputs[i].blend_dst);
        SHAD_WRITE(&compiled->fragment_outputs[i].blend_op);
    }
    SHAD_WRITE(&compiled->num_fragment_samplers);
    SHAD_WRITE(&compiled->num_fragment_images);
    SHAD_WRITE(&compiled->num_fragment_buffers);
    SHAD_WRITE(&compiled->num_fragment_uniforms);

    /* depth */
    SHAD_WRITE(&compiled->depth_write);
    SHAD_WRITE(&compiled->depth_cmp);
    SHAD_WRITE(&compiled->depth_format);
    SHAD_WRITE(&compiled->depth_clip);

    /* culling */
    SHAD_WRITE(&compiled->cull_mode);

    /* primitive */
    SHAD_WRITE(&compiled->primitive);

    /* multisampling. Valid values are 1,2,4,8 */
    SHAD_WRITE(&compiled->multisample_count);

    /* copy out result */
    char *result = (char*)malloc(writer.len+1);
    memcpy(result, writer.buf, writer.len+1);
    *bytes_out = result;
    *num_bytes_out = writer.len;

    /* destroy arena */
    shad__arena_destroy(&tmp);
    return;

    #undef SHAD_WRITE_N
    #undef SHAD_WRITE
}

ShadBool shad_deserialize(char *bytes, int num_bytes, ShadResult *compiled) {
    ShadArena arena;
    int num_bytes_remaining;
    int i;

    memset(&arena, 0, sizeof(arena));
    num_bytes_remaining = (int)num_bytes;
    memset(compiled, 0, sizeof(*compiled));

    #define SHAD_READ_N(ptr, size) do { \
        int s = size; \
        if (num_bytes_remaining < s) goto err; \
        memcpy(ptr, bytes, s); \
        bytes += s; \
        num_bytes_remaining -= s; \
    } while (0)
    #define SHAD_READ(ptr) SHAD_READ_N(ptr, sizeof(*(ptr)))

    /* vertex shader info */
    SHAD_READ(&compiled->spirv_vertex_code_size);
    compiled->spirv_vertex_code = SHAD_ALLOC(char, &arena, compiled->spirv_vertex_code_size);
    SHAD_READ_N(compiled->spirv_vertex_code, compiled->spirv_vertex_code_size);
    SHAD_READ(&compiled->num_vertex_inputs);
    compiled->vertex_inputs = SHAD_ALLOC(ShadVertexInput, &arena, compiled->num_vertex_inputs);
    for (i = 0; i < compiled->num_vertex_inputs; ++i) {
        SHAD_READ(&compiled->vertex_inputs[i].format);
        SHAD_READ(&compiled->vertex_inputs[i].buffer_slot);
        SHAD_READ(&compiled->vertex_inputs[i].offset);
        SHAD_READ(&compiled->vertex_inputs[i].instanced);
    }
    SHAD_READ(&compiled->num_vertex_input_buffers);
    compiled->vertex_input_buffers = SHAD_ALLOC(ShadVertexInputBuffer, &arena, compiled->num_vertex_input_buffers);
    for (i = 0; i < compiled->num_vertex_input_buffers; ++i) {
        SHAD_READ(&compiled->vertex_input_buffers[i].slot);
        SHAD_READ(&compiled->vertex_input_buffers[i].instanced);
        SHAD_READ(&compiled->vertex_input_buffers[i].stride);
    }
    SHAD_READ(&compiled->num_vertex_input_buffers);
    SHAD_READ(&compiled->num_vertex_outputs);
    SHAD_READ(&compiled->num_vertex_samplers);
    SHAD_READ(&compiled->num_vertex_images);
    SHAD_READ(&compiled->num_vertex_buffers);
    SHAD_READ(&compiled->num_vertex_uniforms);

    /* fragment shader info */
    SHAD_READ(&compiled->has_fragment_shader);
    SHAD_READ(&compiled->spirv_fragment_code_size);
    compiled->spirv_fragment_code = SHAD_ALLOC(char, &arena, compiled->spirv_fragment_code_size);
    SHAD_READ_N(compiled->spirv_fragment_code, compiled->spirv_fragment_code_size);
    SHAD_READ(&compiled->num_fragment_outputs);
    compiled->fragment_outputs = SHAD_ALLOC(ShadFragmentOutput, &arena, compiled->num_fragment_outputs);
    for (i = 0; i < compiled->num_fragment_outputs; ++i) {
        SHAD_READ(&compiled->fragment_outputs[i].format);
        SHAD_READ(&compiled->fragment_outputs[i].blend_src);
        SHAD_READ(&compiled->fragment_outputs[i].blend_dst);
        SHAD_READ(&compiled->fragment_outputs[i].blend_op);
    }
    SHAD_READ(&compiled->num_fragment_samplers);
    SHAD_READ(&compiled->num_fragment_images);
    SHAD_READ(&compiled->num_fragment_buffers);
    SHAD_READ(&compiled->num_fragment_uniforms);

    /* depth */
    SHAD_READ(&compiled->depth_write);
    SHAD_READ(&compiled->depth_cmp);
    SHAD_READ(&compiled->depth_format);
    SHAD_READ(&compiled->depth_clip);

    /* culling */
    SHAD_READ(&compiled->cull_mode);

    /* primitive */
    SHAD_READ(&compiled->primitive);

    /* multisampling. Valid values are 1,2,4,8 */
    SHAD_READ(&compiled->multisample_count);

    if (num_bytes_remaining) goto err;

    {
        ShadArena *arena_on_heap = SHAD_ALLOC(ShadArena, &arena, 1);
        *arena_on_heap = arena;
        compiled->arena = arena_on_heap;
    }

    return 1;

    err:
    shad__arena_destroy(&arena);
    return 0;

    #undef SHAD_READ
    #undef SHAD_READ_N
}

void shad_free(void *ptr) {
    free(ptr);
}

/* Runtime implementation */

#ifdef SHAD_RUNTIME

/* SDL Implementation */

#ifdef SDL_VERSION

static const SDL_GPUCullMode shad_to_sdl_cull_mode[] = {
    SDL_GPU_CULLMODE_NONE, /* SDL_GPU_CULLMODE_INVALID */
    SDL_GPU_CULLMODE_NONE, /* SDL_GPU_CULLMODE_NONE */
    SDL_GPU_CULLMODE_FRONT, /* SDL_GPU_CULLMODE_FRONT */
    SDL_GPU_CULLMODE_BACK, /* SDL_GPU_CULLMODE_BACK */
};

static const SDL_GPUVertexElementFormat shad_to_sdl_vertex_element_format[] = {
    SDL_GPU_VERTEXELEMENTFORMAT_INVALID, /* SHAD_VERTEXELEMENTFORMAT_INVALID */
    SDL_GPU_VERTEXELEMENTFORMAT_INT, /* SHAD_VERTEXELEMENTFORMAT_INT */
    SDL_GPU_VERTEXELEMENTFORMAT_INT2, /* SHAD_VERTEXELEMENTFORMAT_INT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_INT3, /* SHAD_VERTEXELEMENTFORMAT_INT3 */
    SDL_GPU_VERTEXELEMENTFORMAT_INT4, /* SHAD_VERTEXELEMENTFORMAT_INT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_UINT, /* SHAD_VERTEXELEMENTFORMAT_UINT */
    SDL_GPU_VERTEXELEMENTFORMAT_UINT2, /* SHAD_VERTEXELEMENTFORMAT_UINT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_UINT3, /* SHAD_VERTEXELEMENTFORMAT_UINT3 */
    SDL_GPU_VERTEXELEMENTFORMAT_UINT4, /* SHAD_VERTEXELEMENTFORMAT_UINT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, /* SHAD_VERTEXELEMENTFORMAT_FLOAT */
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, /* SHAD_VERTEXELEMENTFORMAT_FLOAT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, /* SHAD_VERTEXELEMENTFORMAT_FLOAT3 */
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, /* SHAD_VERTEXELEMENTFORMAT_FLOAT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE2, /* SHAD_VERTEXELEMENTFORMAT_BYTE2 */
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE4, /* SHAD_VERTEXELEMENTFORMAT_BYTE4 */
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2, /* SHAD_VERTEXELEMENTFORMAT_UBYTE2 */
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, /* SHAD_VERTEXELEMENTFORMAT_UBYTE4 */
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM, /* SHAD_VERTEXELEMENTFORMAT_BYTE2_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM, /* SHAD_VERTEXELEMENTFORMAT_BYTE4_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM, /* SHAD_VERTEXELEMENTFORMAT_UBYTE2_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, /* SHAD_VERTEXELEMENTFORMAT_UBYTE4_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT2, /* SHAD_VERTEXELEMENTFORMAT_SHORT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT4, /* SHAD_VERTEXELEMENTFORMAT_SHORT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, /* SHAD_VERTEXELEMENTFORMAT_USHORT2 */
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT4, /* SHAD_VERTEXELEMENTFORMAT_USHORT4 */
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM, /* SHAD_VERTEXELEMENTFORMAT_SHORT2_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM, /* SHAD_VERTEXELEMENTFORMAT_SHORT4_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM, /* SHAD_VERTEXELEMENTFORMAT_USHORT2_NORM */
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM, /* SHAD_VERTEXELEMENTFORMAT_USHORT4_NORM */
};

static const SDL_GPUCompareOp shad_to_sdl_compare_op[] = {
    SDL_GPU_COMPAREOP_INVALID, /* SHAD_COMPARE_OP_INVALID */
    SDL_GPU_COMPAREOP_NEVER, /* SHAD_COMPARE_OP_NEVER */
    SDL_GPU_COMPAREOP_LESS, /* SHAD_COMPARE_OP_LESS */
    SDL_GPU_COMPAREOP_EQUAL, /* SHAD_COMPARE_OP_EQUAL */
    SDL_GPU_COMPAREOP_LESS_OR_EQUAL, /* SHAD_COMPARE_OP_LESS_OR_EQUAL */
    SDL_GPU_COMPAREOP_GREATER, /* SHAD_COMPARE_OP_GREATER */
    SDL_GPU_COMPAREOP_NOT_EQUAL, /* SHAD_COMPARE_OP_NOT_EQUAL */
    SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, /* SHAD_COMPARE_OP_GREATER_OR_EQUAL */
    SDL_GPU_COMPAREOP_ALWAYS, /* SHAD_COMPARE_OP_ALWAYS */
};

static const SDL_GPUTextureFormat shad_to_sdl_texture_format[] = {
    SDL_GPU_TEXTUREFORMAT_INVALID, /* SHAD_TEXTURE_FORMAT_INVALID */
    SDL_GPU_TEXTUREFORMAT_R8_UNORM, /* SHAD_TEXTURE_FORMAT_R8 */
    SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, /* SHAD_TEXTURE_FORMAT_RG8 */
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, /* SHAD_TEXTURE_FORMAT_RGBA8 */
    SDL_GPU_TEXTUREFORMAT_R16_UNORM, /* SHAD_TEXTURE_FORMAT_R16 */
    SDL_GPU_TEXTUREFORMAT_R16G16_UNORM, /* SHAD_TEXTURE_FORMAT_RG16 */
    SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM, /* SHAD_TEXTURE_FORMAT_RGBA16 */
    SDL_GPU_TEXTUREFORMAT_R16_FLOAT, /* SHAD_TEXTURE_FORMAT_R16F */
    SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT, /* SHAD_TEXTURE_FORMAT_RG16F */
    SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, /* SHAD_TEXTURE_FORMAT_RGBA16F */
    SDL_GPU_TEXTUREFORMAT_R32_FLOAT, /* SHAD_TEXTURE_FORMAT_R32F */
    SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT, /* SHAD_TEXTURE_FORMAT_RG32F */
    SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, /* SHAD_TEXTURE_FORMAT_RGBA32F */
    SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT, /* SHAD_TEXTURE_FORMAT_R11G11B10F */
    SDL_GPU_TEXTUREFORMAT_D16_UNORM, /* SHAD_TEXTURE_FORMAT_D16 */
    SDL_GPU_TEXTUREFORMAT_D24_UNORM, /* SHAD_TEXTURE_FORMAT_D24 */
    SDL_GPU_TEXTUREFORMAT_D32_FLOAT, /* SHAD_TEXTURE_FORMAT_D32F */
    SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, /* SHAD_TEXTURE_FORMAT_D24_S8 */
    SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, /* SHAD_TEXTURE_FORMAT_D32F_S8 */
};

static const SDL_GPUBlendFactor shad_to_sdl_blend_factor[] = {
    SDL_GPU_BLENDFACTOR_INVALID, /* SHAD_BLEND_FACTOR_INVALID */
    SDL_GPU_BLENDFACTOR_ZERO, /* SHAD_BLEND_FACTOR_ZERO */
    SDL_GPU_BLENDFACTOR_ONE, /* SHAD_BLEND_FACTOR_ONE */
    SDL_GPU_BLENDFACTOR_SRC_COLOR, /* SHAD_BLEND_FACTOR_SRC_COLOR */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR, /* SHAD_BLEND_FACTOR_ONE_MINUS_SRC_COLOR */
    SDL_GPU_BLENDFACTOR_DST_COLOR, /* SHAD_BLEND_FACTOR_DST_COLOR */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR, /* SHAD_BLEND_FACTOR_ONE_MINUS_DST_COLOR */
    SDL_GPU_BLENDFACTOR_SRC_ALPHA, /* SHAD_BLEND_FACTOR_SRC_ALPHA */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, /* SHAD_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA */
    SDL_GPU_BLENDFACTOR_DST_ALPHA, /* SHAD_BLEND_FACTOR_DST_ALPHA */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA, /* SHAD_BLEND_FACTOR_ONE_MINUS_DST_ALPHA */
    SDL_GPU_BLENDFACTOR_CONSTANT_COLOR, /* SHAD_BLEND_FACTOR_CONSTANT_COLOR */
    SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR, /* SHAD_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR */
    SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE, /* SHAD_BLEND_FACTOR_SRC_ALPHA_SATURATE */
};

static const SDL_GPUBlendOp shad_to_sdl_blend_op[] = {
    SDL_GPU_BLENDOP_INVALID, /* SHAD_BLEND_OP_INVALID */
    SDL_GPU_BLENDOP_ADD, /* SHAD_BLEND_OP_ADD */
    SDL_GPU_BLENDOP_SUBTRACT, /* SHAD_BLEND_OP_SUBTRACT */
    SDL_GPU_BLENDOP_REVERSE_SUBTRACT, /* SHAD_BLEND_OP_REV_SUBTRACT */
    SDL_GPU_BLENDOP_MIN, /* SHAD_BLEND_OP_MIN */
    SDL_GPU_BLENDOP_MAX, /* SHAD_BLEND_OP_MAX */
};

static const SDL_GPUSampleCount shad_to_sdl_sample_count[] = {
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

static const SDL_GPUPrimitiveType shad_to_sdl_primitive[] = {
    SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,  /* SHAD_PRIMITIVE_INVALID */
    SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,  /* SHAD_PRIMITIVE_TRIANGLE_LIST */
    SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP, /* SHAD_PRIMITIVE_TRIANGLE_STRIP */
    SDL_GPU_PRIMITIVETYPE_LINELIST,      /* SHAD_PRIMITIVE_LINE_LIST */
    SDL_GPU_PRIMITIVETYPE_LINESTRIP,     /* SHAD_PRIMITIVE_LINE_STRIP */
    SDL_GPU_PRIMITIVETYPE_POINTLIST,      /* SHAD_PRIMITIVE_POINT_LIST */
};

void shad_sdl_prefill_vertex_shader(SDL_GPUShaderCreateInfo *info, ShadResult *compiled) {
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

void shad_sdl_prefill_fragment_shader(SDL_GPUShaderCreateInfo *info, ShadResult *compiled) {
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

void shad_sdl_prefill_pipeline(SDL_GPUGraphicsPipelineCreateInfo *info, ShadResult *compiled) {
    int i;

    memset(info, 0, sizeof(*info));

    if (!compiled->arena) {
        ShadArena arena = {0};
        ShadArena *arena_on_heap = SHAD_ALLOC(ShadArena, &arena, 1);
        *arena_on_heap = arena;
        compiled->arena = arena_on_heap;
    }

    ShadArena *arena = compiled->arena;

    /* set vertex_input_state */
    /* buffer descriptions */
    info->vertex_input_state.num_vertex_buffers = compiled->num_vertex_input_buffers;
    SDL_GPUVertexBufferDescription *vertex_buffer_descriptions = SHAD_ALLOC(SDL_GPUVertexBufferDescription, arena, info->vertex_input_state.num_vertex_buffers);
    for (i = 0; i < compiled->num_vertex_input_buffers; ++i) {
        SDL_GPUVertexBufferDescription *desc = &vertex_buffer_descriptions[i];
        ShadVertexInputBuffer *buf = compiled->vertex_input_buffers + i;
        desc->slot = buf->slot;
        desc->pitch = buf->stride;
        desc->input_rate = buf->instanced ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX;
    }
    info->vertex_input_state.vertex_buffer_descriptions = vertex_buffer_descriptions;

    /* attributes */
    info->vertex_input_state.num_vertex_attributes = compiled->num_vertex_inputs;
    SDL_GPUVertexAttribute *vertex_attributes = SHAD_ALLOC(SDL_GPUVertexAttribute, arena, info->vertex_input_state.num_vertex_attributes);
    for (i = 0; i < compiled->num_vertex_inputs; ++i) {
        ShadVertexInput *in = compiled->vertex_inputs + i;
        SDL_GPUVertexAttribute *desc = &vertex_attributes[i];
        desc->location = i;
        desc->buffer_slot = in->buffer_slot;
        desc->offset = in->offset;
        desc->format = shad_to_sdl_vertex_element_format[in->format];
    }
    info->vertex_input_state.vertex_attributes = vertex_attributes;

    /* rasterization */
    info->rasterizer_state.cull_mode = shad_to_sdl_cull_mode[compiled->cull_mode];
    info->rasterizer_state.enable_depth_clip = compiled->depth_clip;

    /* multisampling */
    info->multisample_state.sample_count = shad_to_sdl_sample_count[compiled->multisample_count];

    /* depth stencil */
    info->depth_stencil_state.compare_op = shad_to_sdl_compare_op[compiled->depth_cmp];
    info->depth_stencil_state.enable_depth_test = compiled->depth_cmp != SHAD_COMPARE_OP_INVALID;
    info->depth_stencil_state.enable_depth_write = compiled->depth_write;

    /* targets */
    info->target_info.num_color_targets = compiled->num_fragment_outputs;
    if (info->target_info.num_color_targets) {
        SDL_GPUColorTargetDescription *color_target_descriptions = SHAD_ALLOC(SDL_GPUColorTargetDescription, arena, info->target_info.num_color_targets);
        for (i = 0; i < (int)info->target_info.num_color_targets; ++i) {
            SDL_GPUColorTargetDescription *desc = &color_target_descriptions[i];
            ShadFragmentOutput *out = compiled->fragment_outputs + i;
            desc->format = shad_to_sdl_texture_format[out->format];

            SDL_GPUColorTargetBlendState *blend = &desc->blend_state;
            blend->src_color_blendfactor = shad_to_sdl_blend_factor[out->blend_src];
            blend->dst_color_blendfactor = shad_to_sdl_blend_factor[out->blend_dst];
            blend->color_blend_op = shad_to_sdl_blend_op[out->blend_op];
            blend->src_alpha_blendfactor = shad_to_sdl_blend_factor[out->blend_src];
            blend->dst_alpha_blendfactor = shad_to_sdl_blend_factor[out->blend_dst];
            blend->alpha_blend_op = shad_to_sdl_blend_op[out->blend_op];
            blend->enable_blend = out->blend_op != SHAD_BLEND_OP_INVALID;
        }
        info->target_info.color_target_descriptions = color_target_descriptions;
    }
    info->target_info.depth_stencil_format = shad_to_sdl_texture_format[compiled->depth_format];
    info->target_info.has_depth_stencil_target = compiled->depth_cmp != SHAD_COMPARE_OP_INVALID;

    /* primitive */
    info->primitive_type = shad_to_sdl_primitive[compiled->primitive];
}

#endif /* SDL_VERSION */

#endif /* SHAD_RUNTIME */
