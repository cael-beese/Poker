/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* test_panel.c - the control panel layer (platform/panel.h): the built-in
 * wiring, [panel] config keys, the bindings it adds to the input config
 * (SERVICE only when held), and panel.ini written by the wizard and read
 * back. No display, no joysticks. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform/input.h"
#include "platform/panel.h"
#include "test_util.h"

static int has_binding(const InputConfig *c, uint32_t btn, int joy, int button, int hold)
{
    for (int i = 0; i < INPUT_NBUTTONS; i++) {
        if (btn != (1u << i)) continue;
        for (int k = 0; k < INPUT_MAX_BINDINGS; k++) {
            const Binding *b = &c->bind[i][k];
            if (b->kind == BIND_JOY_BUTTON && b->joy == joy && b->code == button && b->hold == hold) return 1;
        }
    }
    return 0;
}

static int count_joy_bindings(const InputConfig *c)
{
    int n = 0;
    for (int i = 0; i < INPUT_NBUTTONS; i++)
        for (int k = 0; k < INPUT_MAX_BINDINGS; k++)
            if (c->bind[i][k].kind == BIND_JOY_BUTTON) n++;
    return n;
}

int main(void)
{
    /* Hold bindings in the config syntax. */
    Binding b;
    char s[48];
    CHECK(input_parse_binding("JOY1_B9@3", &b) == 0);
    CHECK(b.kind == BIND_JOY_BUTTON && b.joy == 1 && b.code == 9 && b.hold == 3);
    input_binding_str(&b, s, sizeof s);
    CHECK(strcmp(s, "JOY1_B9@3") == 0);
    CHECK(input_parse_binding("JOY1_B9@0", &b) != 0);
    CHECK(input_parse_binding("JOY1_B9@x", &b) != 0);
    CHECK(input_parse_binding("KEY_A", &b) == 0 && b.hold == 0);

    /* The layout: the top rows are HOLD 1-5 then DEAL, DEAL twice. */
    CHECK(panel_slot_of(BTN_HOLD1) == PS_P1_T1);
    CHECK(panel_slot_of(BTN_HOLD4) == PS_P2_T1);
    CHECK(panel_slot_of(BTN_DEAL) == PS_P2_T3);
    CHECK(panel_slot_buttons(PS_P2_B3) & BTN_DEAL);
    CHECK(panel_slot_of(BTN_CASH_OUT) == PS_P1_B1);
    CHECK(panel_slot_of(BTN_SERVICE) == PS_P2_START && panel_slot_hold(PS_P2_START) == 3);
    CHECK(panel_slot_buttons(PS_P2_B1) == 0);
    CHECK(panel_slot_of(BTN_COIN) == PS_P1_SELECT);

    /* The built-in guess, then a config key over it. */
    panel_defaults();
    CHECK(panel_wire(PS_P1_T1).joy == 0 && panel_wire(PS_P1_T1).button == 3);
    CHECK(panel_wire(PS_P2_START).joy == 1 && panel_wire(PS_P2_START).button == 9);
    CHECK(panel_config_set("P1_T1", "JOY0_B7") == 0);
    CHECK(panel_config_set("P2_B2", "NONE") == 0);
    CHECK(panel_config_set("P1_T1", "KEY_A") != 0);
    CHECK(panel_config_set("P1_T9", "JOY0_B1") != 0);
    CHECK(panel_wire(PS_P1_T1).button == 7);

    /* A fresh save directory: no panel.ini, so the configured wiring. */
    char dir[] = "/tmp/bpl-panel-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    InputConfig base, live;
    input_defaults(&base);
    CHECK(count_joy_bindings(&base) == 0);          /* the panel owns the buttons */
    panel_set_interactive(0);
    panel_init(dir, &base, &live);
    CHECK(!panel_learned());
    CHECK(has_binding(&live, BTN_HOLD1, 0, 7, 0));
    CHECK(has_binding(&live, BTN_DEAL, 1, 4, 0) && has_binding(&live, BTN_DEAL, 1, 5, 0));
    CHECK(has_binding(&live, BTN_OK, 1, 4, 0));
    CHECK(has_binding(&live, BTN_SERVICE, 1, 9, 3));    /* held 3 s, never at once */
    CHECK(!has_binding(&live, BTN_SERVICE, 1, 9, 0));
    CHECK(!has_binding(&live, BTN_START, 1, 9, 0));     /* P2 START is not START  */
    CHECK(has_binding(&live, BTN_START, 0, 9, 0));
    CHECK(has_binding(&live, BTN_COIN, 0, 8, 0) && has_binding(&live, BTN_COIN, 1, 8, 0));
    CHECK(!panel_wizard_due());                         /* not interactive          */

    /* Suspended while learning: keyboard only. */
    panel_suspend(1);
    CHECK(count_joy_bindings(&live) == 0);
    panel_suspend(0);
    CHECK(has_binding(&live, BTN_HOLD1, 0, 7, 0));

    /* The wizard's answer: applied at once, saved, and read back. */
    PanelWire w[PS_COUNT];
    for (int i = 0; i < PS_COUNT; i++) {
        w[i].joy = (int8_t)(i < PS_PER_SIDE ? 1 : 0);   /* sides swapped */
        w[i].button = (int8_t)(i % PS_PER_SIDE);
    }
    w[PS_P2_B2].button = -1;
    CHECK(panel_set_wiring(w) == 0);
    CHECK(panel_learned());
    CHECK(has_binding(&live, BTN_HOLD1, 1, 0, 0));
    CHECK(!has_binding(&live, BTN_HOLD1, 0, 7, 0));
    char path[128];
    snprintf(path, sizeof path, "%s/panel.ini", dir);
    CHECK(access(path, R_OK) == 0);

    panel_defaults();                                   /* forget it in memory */
    InputConfig live2;
    panel_init(dir, &base, &live2);
    CHECK(panel_learned());
    for (int i = 0; i < PS_COUNT; i++)
        CHECK(panel_wire((PanelSlot)i).joy == w[i].joy && panel_wire((PanelSlot)i).button == w[i].button);
    CHECK(has_binding(&live2, BTN_SERVICE, 0, 7, 3));    /* P2 START = JOY0_B7 now */

    /* The text it saves. */
    char txt[2048];
    CHECK(panel_format(txt, sizeof txt, w, 1, 1, 0) > 0);
    CHECK(strstr(txt, "P1_T1     = JOY1_B0") != NULL);
    CHECK(strstr(txt, "P2_B2     = NONE") != NULL);
    CHECK(strstr(txt, "learned = 1") != NULL);

    unlink(path);
    rmdir(dir);
    if (g_test_failures) {
        fprintf(stderr, "panel: %d failure(s)\n", g_test_failures);
        return 1;
    }
    printf("panel: ok\n");
    return 0;
}
