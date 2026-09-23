/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* gputest.c - the GPU smoke test (--mode gputest).
 *
 * A deliberately busy scene to measure the Pi's headroom before the real
 * renderer exists: a full-screen animated honeycomb shader (the kind of pass
 * a bloom composite or neon background will be), then N moving, rotating,
 * alpha-blended sprites from a procedural 256x256 atlas, all in one batch.
 * Sprites are 32..112 px, so 1,000 of them cover the 1280x720 play space
 * about four times over. Motion is cosmetic (present_update) and seeded from
 * the session seed, so --lockstep runs are reproducible. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "rlgl.h"
#include "platform/app.h"
#include "platform/gputest.h"
#include "platform/screen.h"
#include "platform/texreg.h"

#define ATLAS 256
#define CELL 64

typedef struct {
    float x, y, vx, vy, rot, vrot, size;
    uint8_t cell;
    Color tint;
} Sprite;

static Sprite g_spr[GPUTEST_MAX_SPRITES];
static int g_nspr = 1000;
static int g_use_shader = 1;
static Texture2D g_atlas;
static Shader g_shader;
static int g_time_loc = -1;
static uint64_t g_rng;

void gputest_configure(int sprites, int shader)
{
    if (sprites < 0) sprites = 0;
    if (sprites > GPUTEST_MAX_SPRITES) sprites = GPUTEST_MAX_SPRITES;
    g_nspr = sprites;
    g_use_shader = shader;
}

/* splitmix64: cosmetic randomness with its own stream (CONTRACT section 4). */
static uint64_t next_u64(void)
{
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (float)((double)(next_u64() >> 11) / 9007199254740992.0);
}

static void hex_fan(Image *img, float cx, float cy, float r, Color c)
{
    Vector2 pts[8];
    pts[0] = (Vector2){ cx, cy };
    for (int i = 0; i <= 6; i++) {
        /* Clockwise in image space so ImageDrawTriangleFan fills it. */
        float a = (float)(-i) * PI / 3.0f + PI / 6.0f;
        pts[i + 1] = (Vector2){ cx + cosf(a) * r, cy + sinf(a) * r };
    }
    ImageDrawTriangleFan(img, pts, 8, c);
}

static Image build_atlas(void)
{
    Image img = GenImageColor(ATLAS, ATLAS, BLANK);
    const Color honey = { 232, 170, 40, 255 }, amber = { 255, 128, 16, 255 };
    const Color magenta = { 255, 40, 200, 255 }, cyan = { 40, 230, 255, 255 };
    const Color gold = { 255, 210, 90, 255 }, red = { 200, 30, 40, 255 };
    for (int i = 0; i < 16; i++) {
        float cx = (float)((i % 4) * CELL) + CELL / 2.0f, cy = (float)((i / 4) * CELL) + CELL / 2.0f;
        switch (i % 8) {
        case 0: hex_fan(&img, cx, cy, 30, honey); hex_fan(&img, cx, cy, 22, amber); break;
        case 1: /* coin */
            ImageDrawCircle(&img, (int)cx, (int)cy, 29, gold);
            ImageDrawCircle(&img, (int)cx, (int)cy, 22, honey);
            ImageDrawCircleLines(&img, (int)cx, (int)cy, 18, amber);
            break;
        case 2: /* chip */
            ImageDrawCircle(&img, (int)cx, (int)cy, 29, red);
            for (int k = 0; k < 6; k++) {
                float a = k * PI / 3.0f;
                ImageDrawCircle(&img, (int)(cx + cosf(a) * 24), (int)(cy + sinf(a) * 24), 4, RAYWHITE);
            }
            ImageDrawCircle(&img, (int)cx, (int)cy, 16, (Color){ 240, 240, 240, 255 });
            break;
        case 3: /* spark */
            ImageDrawCircle(&img, (int)cx, (int)cy, 10, (Color){ 255, 255, 255, 255 });
            ImageDrawRectangle(&img, (int)cx - 30, (int)cy - 2, 60, 4, Fade(cyan, 0.8f));
            ImageDrawRectangle(&img, (int)cx - 2, (int)cy - 30, 4, 60, Fade(cyan, 0.8f));
            break;
        case 4: /* card back */
            ImageDrawRectangle(&img, (int)cx - 20, (int)cy - 29, 40, 58, RAYWHITE);
            ImageDrawRectangle(&img, (int)cx - 17, (int)cy - 26, 34, 52, magenta);
            hex_fan(&img, cx, cy, 12, gold);
            break;
        case 5: /* honey droplet */
            ImageDrawCircle(&img, (int)cx, (int)cy + 8, 18, Fade(amber, 0.9f));
            ImageDrawTriangle(&img, (Vector2){ cx, cy - 28 }, (Vector2){ cx - 16, cy + 4 },
                              (Vector2){ cx + 16, cy + 4 }, Fade(amber, 0.9f));
            break;
        case 6: /* confetti */
            ImageDrawRectangle(&img, (int)cx - 24, (int)cy - 8, 48, 16, (i & 8) ? cyan : magenta);
            break;
        default: /* soft glow */
            for (int r = 30; r > 0; r -= 3)
                ImageDrawCircle(&img, (int)cx, (int)cy, r, Fade(honey, 0.10f));
            break;
        }
    }
    return img;
}

static const char *const g_frag_body =
    "IN vec2 fragTexCoord;\n"
    "IN vec4 fragColor;\n"
    "uniform float uTime;\n"
    "void main() {\n"
    "    vec2 uv = fragTexCoord * vec2(16.0, 9.0);\n"
    "    vec2 r = vec2(1.0, 1.7320508);\n"
    "    vec2 h = r * 0.5;\n"
    "    vec2 a = mod(uv, r) - h;\n"
    "    vec2 b = mod(uv - h, r) - h;\n"
    "    vec2 g = dot(a, a) < dot(b, b) ? a : b;\n"
    "    vec2 p = abs(g);\n"
    "    float d = 0.5 - max(dot(p, normalize(r)), p.x);\n"
    "    vec2 id = uv - g;\n"
    "    float wave = 0.5 + 0.5 * sin(uTime * 1.7 + id.x * 0.6 + id.y * 0.9);\n"
    "    float edge = smoothstep(0.06, 0.0, d);\n"
    "    vec3 honey = vec3(0.91, 0.66, 0.16);\n"
    "    vec3 neon = mix(vec3(1.0, 0.16, 0.78), vec3(0.16, 0.9, 1.0), 0.5 + 0.5 * sin(uTime * 0.5 + id.x * 0.3));\n"
    "    vec3 col = vec3(0.06, 0.05, 0.07) + honey * 0.25 * wave * (1.0 - edge) + neon * edge * (0.35 + 0.65 * wave);\n"
    "    FRAG_OUT = vec4(col, 1.0) * fragColor;\n"
    "}\n";

static void gputest_init(AppCtx *ctx)
{
    Image img = build_atlas();
    g_atlas = texreg_load_image("gputest atlas 256", img);
    UnloadImage(img);
    SetTextureFilter(g_atlas, TEXTURE_FILTER_BILINEAR);

    /* The fragment shader must match rlgl's default vertex shader version. */
    const char *head;
    switch (rlGetVersion()) {
    case RL_OPENGL_ES_20:
        head = "#version 100\nprecision mediump float;\n#define IN varying\n#define FRAG_OUT gl_FragColor\n";
        break;
    case RL_OPENGL_ES_30:
        head = "#version 300 es\nprecision mediump float;\n#define IN in\nout vec4 finalColor;\n#define FRAG_OUT finalColor\n";
        break;
    default:
        head = "#version 330\n#define IN in\nout vec4 finalColor;\n#define FRAG_OUT finalColor\n";
        break;
    }
    static char src[4096];
    snprintf(src, sizeof src, "%s%s", head, g_frag_body);
    g_shader = LoadShaderFromMemory(NULL, src);
    g_time_loc = GetShaderLocation(g_shader, "uTime");

    g_rng = ctx->session_seed ^ 0x6770757465737421ULL;   /* "gputest!" */
    for (int i = 0; i < GPUTEST_MAX_SPRITES; i++) {
        Sprite *s = &g_spr[i];
        s->size = frand(32, 112);
        s->x = frand(0, PLAY_W);
        s->y = frand(0, PLAY_H);
        float a = frand(0, 2 * PI), v = frand(60, 320);
        s->vx = cosf(a) * v;
        s->vy = sinf(a) * v;
        s->rot = frand(0, 360);
        s->vrot = frand(-180, 180);
        s->cell = (uint8_t)(next_u64() & 15u);
        s->tint = (Color){ (unsigned char)frand(180, 255), (unsigned char)frand(180, 255),
                           (unsigned char)frand(180, 255), (unsigned char)frand(150, 255) };
    }
}

static void gputest_tick(AppCtx *ctx, const InputFrame *in)
{
    if (in->pressed & BTN_BACK) app_request(ctx, APP_ATTRACT);
}

static void gputest_update(const AppCtx *ctx, const GameEvent *ev, int nev, float dt)
{
    (void)ctx; (void)ev; (void)nev;
    for (int i = 0; i < g_nspr; i++) {
        Sprite *s = &g_spr[i];
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        s->rot += s->vrot * dt;
        if (s->x < 0) { s->x = 0; s->vx = -s->vx; }
        if (s->x > PLAY_W) { s->x = PLAY_W; s->vx = -s->vx; }
        if (s->y < 0) { s->y = 0; s->vy = -s->vy; }
        if (s->y > PLAY_H) { s->y = PLAY_H; s->vy = -s->vy; }
    }
}

static void gputest_draw(const AppCtx *ctx)
{
    if (g_use_shader && g_shader.id) {
        float t = (float)ctx->time;
        SetShaderValue(g_shader, g_time_loc, &t, SHADER_UNIFORM_FLOAT);
        /* rlgl's 1x1 white texture drawn with 0..1 texture coordinates, so the
         * shader gets a clean UV across the whole play space. */
        Texture2D white = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        BeginShaderMode(g_shader);
        DrawTexturePro(white, (Rectangle){ 0, 0, 1, 1 }, (Rectangle){ 0, 0, PLAY_W, PLAY_H },
                       (Vector2){ 0, 0 }, 0, WHITE);
        EndShaderMode();
    }
    for (int i = 0; i < g_nspr; i++) {
        const Sprite *s = &g_spr[i];
        Rectangle src = { (float)((s->cell % 4) * CELL), (float)((s->cell / 4) * CELL), CELL, CELL };
        DrawTexturePro(g_atlas, src, (Rectangle){ s->x, s->y, s->size, s->size },
                       (Vector2){ s->size / 2, s->size / 2 }, s->rot, s->tint);
    }
    char buf[96];
    snprintf(buf, sizeof buf, "GPU TEST  %d sprites  shader %s", g_nspr, g_use_shader ? "on" : "off");
    DrawRectangle(12, 12, MeasureText(buf, 20) + 16, 32, Fade(BLACK, 0.6f));
    DrawText(buf, 20, 18, 20, RAYWHITE);
}

static void gputest_shutdown(void)
{
    if (g_shader.id) UnloadShader(g_shader);
    texreg_unload(g_atlas);
    memset(&g_atlas, 0, sizeof g_atlas);
}

const AppMode mode_gputest = {
    .name = "gputest", .init = gputest_init, .tick = gputest_tick,
    .present_update = gputest_update, .present_draw = gputest_draw, .shutdown = gputest_shutdown,
};
