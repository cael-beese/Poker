/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* controls_view.c - the CONTROLS page of the menu: the cabinet panel as
 * a drawing, both sides, with what every button does in Draw Poker (honey)
 * and in Hold'em (green), lit while it is held. See draw_view.h.
 *
 * The labels hang off the logical buttons, and panel.h says which logical
 * buttons each position drives, so the page follows the layout table there;
 * a position lights from its wired encoder input (panel_slot_down), and from
 * the keyboard through the logical buttons it drives. */
#include "render/draw_view.h"

#include <math.h>
#include <string.h>

#include "platform/panel.h"
#include "render/render.h"

#define C_DRAW    UI_HONEY
#define C_HOLDEM  ((Color){ 120, 255, 140, 255 })

/* What each logical button does: Draw Poker, Hold'em. */
static void names(uint32_t btn, const char **draw, const char **holdem)
{
    *draw = *holdem = "";
    switch (btn) {
    case BTN_HOLD1: *draw = "HOLD 1"; *holdem = "FOLD"; break;
    case BTN_HOLD2: *draw = "HOLD 2"; *holdem = "CHECK / CALL"; break;
    case BTN_HOLD3: *draw = "HOLD 3"; *holdem = "RAISE 1/2 POT"; break;
    case BTN_HOLD4: *draw = "HOLD 4"; *holdem = "RAISE 3/4 POT"; break;
    case BTN_HOLD5: *draw = "HOLD 5"; *holdem = "RAISE POT"; break;
    case BTN_DEAL: *draw = "DEAL / DRAW"; *holdem = "BET / RAISE"; break;
    case BTN_BET_ONE: *draw = "BET ONE"; *holdem = "+1 BIG BLIND"; break;
    case BTN_BET_MAX: *draw = "BET MAX"; *holdem = "ALL-IN"; break;
    case BTN_CASH_OUT: *draw = "CASH OUT"; *holdem = "LEAVE TABLE"; break;
    case BTN_COIN: *draw = "COIN"; *holdem = "(ADD CREDITS)"; break;
    case BTN_START: *draw = "START"; *holdem = ""; break;
    case BTN_SERVICE: *draw = "SERVICE"; *holdem = "(HOLD 3 S)"; break;
    default: break;
    }
}

static uint32_t lowest_bit(uint32_t m) { return m & (~m + 1u); }

static void centred(FontId f, const char *s, float cx, float y, float size, Color c, float alpha)
{
    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    ts.color = c;
    ts.opacity = alpha;
    ts.shadow = BLACK;
    ts.shadow_k = 0.7f;
    text_draw_ex(f, s, cx, y, size, &ts);
}

/* One round arcade button, r its radius. */
static void button(float cx, float cy, float r, int lit, int used, Color neon, double time)
{
    Color ring = used ? neon : (Color){ 90, 80, 96, 255 };
    if (lit) {
        gfx_spr_rot(sprite(SPR_GLOW), cx, cy, r * 5.2f, r * 5.2f, 0, gfx_add(neon, 1.1f));
        gfx_circle(cx, cy, r, gfx_col(neon));
        gfx_circle(cx - r * 0.18f, cy - r * 0.2f, r * 0.55f, gfx_add(WHITE, 0.55f));
    } else {
        float pulse = used ? 0.25f + 0.1f * sinf((float)time * 2.0f + cx * 0.01f) : 0.0f;
        gfx_circle(cx, cy, r, gfx_col((Color){ 30, 22, 34, 255 }));
        gfx_circle(cx, cy, r * 0.8f, gfx_add(neon, pulse * 0.5f));
        /* a highlight along the top of the cap */
        gfx_ring(cx, cy - r * 0.08f, r * 0.72f, 2, gfx_add(WHITE, 0.08f));
    }
    gfx_ring(cx, cy, r, 4, gfx_col(ring));
}

/* SELECT / START: a small pill. */
static void pill(float cx, float cy, float w, float h, int lit, int used, Color neon)
{
    const Nine *fill = sprite_nine(NINE_RRECT), *line = sprite_nine(NINE_RRECT_LINE);
    Color ring = used ? neon : (Color){ 90, 80, 96, 255 };
    if (lit) gfx_spr_rot(sprite(SPR_GLOW), cx, cy, w * 2.2f, h * 3.0f, 0, gfx_add(neon, 1.0f));
    gfx_nine(fill, cx - w / 2, cy - h / 2, w, h, h * 0.5f, lit ? gfx_col(neon) : gfx_col((Color){ 30, 22, 34, 255 }));
    gfx_nine(line, cx - w / 2, cy - h / 2, w, h, h * 0.55f, gfx_col(ring));
}

void controls_view_draw(const MenuViewInfo *v, double time)
{
    render_begin();
    ui_background(time, 0.8f);
    text_neon(FONT_NEON_L, "CONTROLS", 640, 52, 52, UI_CYAN, 0.9f, 1.0f);
    {
        TextStyle ts;
        memset(&ts, 0, sizeof ts);
        ts.align = ALIGN_CENTER;
        ts.spacing = 2;
        ts.color = C_DRAW;
        text_draw_ex(FONT_UI_M, "DRAW POKER", 560, 92, 21, &ts);
        ts.color = (Color){ 150, 140, 150, 255 };
        text_draw_ex(FONT_UI_M, "/", 640, 92, 21, &ts);
        ts.color = C_HOLDEM;
        text_draw_ex(FONT_UI_M, "HOLD'EM", 712, 92, 21, &ts);
    }

    for (int side = 0; side < 2; side++) {
        float ox = side ? 660 : 40, oy = 132, pw = 580, ph = 440;
        ui_panel((Rectangle){ ox, oy, pw, ph }, side ? UI_MAGENTA : UI_HONEY, 0.35f, 1);
        centred(FONT_UI_M, side ? "PLAYER 2 SIDE" : "PLAYER 1 SIDE", ox + pw / 2, oy + 10, 20, (Color){ 190, 180, 200, 255 }, 1);

        /* The stick: menus, and the Hold'em bet amount. */
        float sx = ox + 82, sy = oy + 205;
        gfx_circle(sx, sy, 42, gfx_col((Color){ 26, 20, 30, 255 }));
        gfx_ring(sx, sy, 42, 4, gfx_col((Color){ 90, 80, 96, 255 }));
        int stick = (v->down & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) != 0;
        float kx = sx + (v->down & BTN_LEFT ? -14.0f : v->down & BTN_RIGHT ? 14.0f : 0.0f);
        float ky = sy + (v->down & BTN_UP ? -14.0f : v->down & BTN_DOWN ? 14.0f : 0.0f);
        gfx_circle(kx, ky, 20, gfx_col(stick ? (Color){ 255, 80, 90, 255 } : (Color){ 200, 40, 50, 255 }));
        gfx_circle(kx - 6, ky - 7, 7, gfx_add(WHITE, 0.35f));
        centred(FONT_UI_S, "STICK", sx, sy + 52, 16, (Color){ 170, 160, 175, 255 }, 1);
        centred(FONT_UI_S, "CHOOSE", sx, sy + 70, 15, C_DRAW, 1);
        centred(FONT_UI_S, "BET AMOUNT", sx, sy + 86, 15, C_HOLDEM, 1);

        for (int k = 0; k < PS_PER_SIDE; k++) {
            PanelSlot s = (PanelSlot)(side * PS_PER_SIDE + k);
            uint32_t does = panel_slot_buttons(s);
            int lit = panel_slot_down(s) || (does & v->down & ~(uint32_t)(BTN_OK | BTN_BACK));
            const char *dn, *hn;
            names(lowest_bit(does), &dn, &hn);
            Color neon = does ? (side ? UI_MAGENTA : UI_HONEY) : (Color){ 90, 80, 96, 255 };
            if (does & BTN_DEAL) neon = UI_GOLD;
            if (k < 6) {
                float cx = ox + 230 + (float)(k % 3) * 120, cy = oy + 132 + (float)(k / 3) * 128;
                button(cx, cy, 34, lit, does != 0, neon, time);
                /* Top row: names above; bottom row: below. */
                float ty = k < 3 ? cy - 84 : cy + 42;
                if (!does) {
                    centred(FONT_UI_S, "-", cx, ty + 10, 18, (Color){ 110, 100, 110, 255 }, 1);
                    continue;
                }
                centred(FONT_DISP_S, dn, cx, ty, dn[0] && strlen(dn) > 8 ? 15 : 17, lit ? WHITE : C_DRAW, 1);
                centred(FONT_UI_S, hn, cx, ty + 22, 16, C_HOLDEM, 1);
            } else {
                float cx = ox + 290 + (float)(k - 6) * 180, cy = oy + 382;
                pill(cx, cy, 70, 26, lit, does != 0, neon);
                centred(FONT_UI_S, k == 6 ? "SELECT" : "START", cx - 64, cy - 9, 15, (Color){ 170, 160, 175, 255 }, 1);
                if (does & BTN_SERVICE) {
                    centred(FONT_UI_S, "SERVICE: HOLD 3 S", cx, cy + 20, 15, (Color){ 150, 140, 150, 255 }, 1);
                } else {
                    centred(FONT_DISP_S, dn, cx, cy + 18, 16, lit ? WHITE : C_DRAW, 1);
                }
            }
        }
    }

    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    ts.color = (Color){ 230, 220, 235, 255 };
    text_draw_ex(FONT_UI_M, "PRESS ANY BUTTON: IT LIGHTS UP HERE", 640, 592, 24, &ts);
    ts.color = (Color){ 170, 160, 175, 255 };
    text_draw_ex(FONT_UI_M, "DRAW POKER: BET, DEAL, HOLD THE CARDS YOU KEEP, DRAW.   HOLD'EM: THE BUTTONS UNDER THE TABLE SAY WHAT THEY DO.",
                 640, 624, 17, &ts);
    text_draw_ex(FONT_UI_M, "LEAVE THE GAME: HOLD COIN + START TOGETHER", 640, 648, 17, &ts);

    /* The way out, lit where it is. */
    float pulse = 0.65f + 0.35f * sinf((float)time * 4);
    const float gh = 24, gw = ui_panel_glyph_w(gh);
    const char *out = "CASH OUT: BACK TO THE MENU";
    float tw = text_width(FONT_NEON_M, out, 28, 0);
    float x0 = 640 - (gw + 14 + tw) * 0.5f;
    ui_panel_glyph(x0, 680 - gh * 0.5f, gh, BTN_CASH_OUT, UI_CYAN, 1);
    text_neon(FONT_NEON_M, out, x0 + gw + 14 + tw * 0.5f, 680, 28, UI_CYAN, pulse, 0.9f);
    render_end();
    dv_fill_account(DV_FILL_MENU);
}
