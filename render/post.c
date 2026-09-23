/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* post.c - see post.h. */
#include "render/post.h"

#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "rlgl.h"
#include "platform/fx_settings.h"
#include "platform/screen.h"
#include "platform/texreg.h"

static RenderTexture2D g_scene, g_q4[2], g_q8[2];
static Shader g_thresh, g_blur, g_blur_fast, g_comp;
static int g_loc_texel, g_loc_curve, g_loc_dir, g_loc_dir_fast, g_loc_bloom, g_loc_strength;
static BloomParams g_params = { 0.72f, 0.18f, 1.0f, BLOOM_Q4 };
static int g_ready, g_active;

static const char *head(void)
{
    switch (rlGetVersion()) {
    case RL_OPENGL_ES_20:
        return "#version 100\nprecision mediump float;\n#define IN varying\n#define FRAG_OUT gl_FragColor\n#define TEX texture2D\n";
    case RL_OPENGL_ES_30:
        return "#version 300 es\nprecision mediump float;\n#define IN in\nout vec4 finalColor;\n#define FRAG_OUT finalColor\n#define TEX texture\n";
    default:
        return "#version 330\n#define IN in\nout vec4 finalColor;\n#define FRAG_OUT finalColor\n#define TEX texture\n";
    }
}

static const char *const k_thresh =
    "IN vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 uTexel;\n"
    "uniform vec3 uCurve;\n"
    "void main() {\n"
    "    vec2 uv = fragTexCoord;\n"
    "    vec3 c = TEX(texture0, uv + vec2(-uTexel.x, -uTexel.y)).rgb + TEX(texture0, uv + vec2(uTexel.x, -uTexel.y)).rgb\n"
    "           + TEX(texture0, uv + vec2(-uTexel.x, uTexel.y)).rgb + TEX(texture0, uv + vec2(uTexel.x, uTexel.y)).rgb;\n"
    "    c *= 0.25;\n"
    "    float br = max(c.r, max(c.g, c.b));\n"
    "    float soft = clamp(br - uCurve.x + uCurve.y, 0.0, 2.0 * uCurve.y);\n"
    "    soft = soft * soft / (4.0 * uCurve.y + 0.0001);\n"
    "    float w = max(soft, br - uCurve.x) / max(br, 0.0001);\n"
    "    FRAG_OUT = vec4(c * w, 1.0);\n"
    "}\n";

static const char *const k_blur =
    "IN vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 uDir;\n"
    "void main() {\n"
    "    vec2 uv = fragTexCoord;\n"
    "    vec3 c = TEX(texture0, uv).rgb * 0.2270270;\n"
    "    c += (TEX(texture0, uv + uDir * 1.3846154).rgb + TEX(texture0, uv - uDir * 1.3846154).rgb) * 0.3162162;\n"
    "    c += (TEX(texture0, uv + uDir * 3.2307692).rgb + TEX(texture0, uv - uDir * 3.2307692).rgb) * 0.0702703;\n"
    "    FRAG_OUT = vec4(c, 1.0);\n"
    "}\n";

static const char *const k_blur_fast =
    "IN vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 uDir;\n"
    "void main() {\n"
    "    vec2 uv = fragTexCoord;\n"
    "    vec3 c = TEX(texture0, uv).rgb * 0.2941176;\n"
    "    c += (TEX(texture0, uv + uDir * 1.3333333).rgb + TEX(texture0, uv - uDir * 1.3333333).rgb) * 0.3529412;\n"
    "    FRAG_OUT = vec4(c, 1.0);\n"
    "}\n";

static const char *const k_comp =
    "IN vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D uBloom;\n"
    "uniform float uStrength;\n"
    "void main() {\n"
    "    vec3 c = TEX(texture0, fragTexCoord).rgb + TEX(uBloom, fragTexCoord).rgb * uStrength;\n"
    "    FRAG_OUT = vec4(c, 1.0);\n"
    "}\n";

static Shader load(const char *body)
{
    static char src[4096];
    snprintf(src, sizeof src, "%s%s", head(), body);
    return LoadShaderFromMemory(NULL, src);
}

static RenderTexture2D target(const char *tag, int w, int h)
{
    RenderTexture2D rt = texreg_load_rt(tag, w, h, 0);
    SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);
    return rt;
}

int post_init(void)
{
    g_thresh = load(k_thresh);
    g_blur = load(k_blur);
    g_blur_fast = load(k_blur_fast);
    g_comp = load(k_comp);
    if (!g_thresh.id || !g_blur.id || !g_comp.id || !g_blur_fast.id) {
        fprintf(stderr, "render: bloom shaders failed to compile, bloom disabled\n");
        return -1;
    }
    g_loc_texel = GetShaderLocation(g_thresh, "uTexel");
    g_loc_curve = GetShaderLocation(g_thresh, "uCurve");
    g_loc_dir = GetShaderLocation(g_blur, "uDir");
    g_loc_dir_fast = GetShaderLocation(g_blur_fast, "uDir");
    g_loc_bloom = GetShaderLocation(g_comp, "uBloom");
    g_loc_strength = GetShaderLocation(g_comp, "uStrength");
    g_scene = target("bloom scene 1280x720", PLAY_W, PLAY_H);
    g_q4[0] = target("bloom 1/4 a", PLAY_W / 4, PLAY_H / 4);
    g_q4[1] = target("bloom 1/4 b", PLAY_W / 4, PLAY_H / 4);
    g_q8[0] = target("bloom 1/8 a", PLAY_W / 8, PLAY_H / 8);
    g_q8[1] = target("bloom 1/8 b", PLAY_W / 8, PLAY_H / 8);
    g_ready = 1;
    return 0;
}

void post_shutdown(void)
{
    if (!g_ready) return;
    UnloadShader(g_thresh);
    UnloadShader(g_blur);
    UnloadShader(g_blur_fast);
    UnloadShader(g_comp);
    texreg_unload_rt(g_scene);
    for (int i = 0; i < 2; i++) { texreg_unload_rt(g_q4[i]); texreg_unload_rt(g_q8[i]); }
    g_ready = 0;
}

BloomParams *post_params(void) { return &g_params; }

const char *post_quality_name(BloomQuality q)
{
    switch (q) {
    case BLOOM_Q8: return "1/8";
    case BLOOM_Q4_FAST: return "1/4 5-tap";
    default: return "1/4 9-tap";
    }
}

int post_active(void) { return g_active; }

void post_begin(void)
{
    g_active = g_ready && g_effects.bloom;
    if (!g_active) return;
    rlDrawRenderBatchActive();
    BeginTextureMode(g_scene);
    ClearBackground((Color){ 14, 12, 16, 255 });
}

/* A full-target quad from src into dst through shader sh. Both targets keep
 * raylib's render-texture orientation, so every pass flips once in the source
 * rectangle and the image stays upright. */
static void pass(RenderTexture2D dst, Texture2D src, Shader sh)
{
    BeginTextureMode(dst);
    BeginShaderMode(sh);
    DrawTexturePro(src, (Rectangle){ 0, 0, (float)src.width, -(float)src.height },
                   (Rectangle){ 0, 0, (float)dst.texture.width, (float)dst.texture.height }, (Vector2){ 0, 0 }, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
}

void post_end(void)
{
    if (!g_active) return;
    EndTextureMode();

    int q8 = g_params.quality == BLOOM_Q8;
    RenderTexture2D *b = q8 ? g_q8 : g_q4;
    float sx = (q8 ? 2.0f : 1.0f) / PLAY_W, sy = (q8 ? 2.0f : 1.0f) / PLAY_H;
    float texel[2] = { sx, sy };
    float curve[3] = { g_params.threshold, g_params.knee, 0 };
    Shader blur = g_params.quality == BLOOM_Q4_FAST ? g_blur_fast : g_blur;
    int loc_dir = g_params.quality == BLOOM_Q4_FAST ? g_loc_dir_fast : g_loc_dir;

    rlDisableColorBlend();
    SetShaderValue(g_thresh, g_loc_texel, texel, SHADER_UNIFORM_VEC2);
    SetShaderValue(g_thresh, g_loc_curve, curve, SHADER_UNIFORM_VEC3);
    pass(b[0], g_scene.texture, g_thresh);
    float dh[2] = { 1.0f / (float)b[0].texture.width, 0 };
    float dv[2] = { 0, 1.0f / (float)b[0].texture.height };
    SetShaderValue(blur, loc_dir, dh, SHADER_UNIFORM_VEC2);
    pass(b[1], b[0].texture, blur);
    SetShaderValue(blur, loc_dir, dv, SHADER_UNIFORM_VEC2);
    pass(b[0], b[1].texture, blur);

    /* Composite into the play space: it replaces the whole frame, so no
     * blending and nothing to load from the target first. */
    screen_resume_play();
    BeginShaderMode(g_comp);
    SetShaderValueTexture(g_comp, g_loc_bloom, b[0].texture);
    SetShaderValue(g_comp, g_loc_strength, &g_params.strength, SHADER_UNIFORM_FLOAT);
    DrawTexturePro(g_scene.texture, (Rectangle){ 0, 0, PLAY_W, -PLAY_H }, (Rectangle){ 0, 0, PLAY_W, PLAY_H },
                   (Vector2){ 0, 0 }, 0, WHITE);
    EndShaderMode();
    rlEnableColorBlend();
}
