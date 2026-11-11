/* SPDX-License-Identifier: GPL-3.0-only
 *
 * pad-colors: recolor the performance pads and the pad-mode buttons from the Mod Menu.
 *
 * Core services only (rows, one frame filter, one preview page), no hooks, so
 * "firmware": ["any"].
 *   - Mod Menu > Couleurs des pads: on/off, a color for the 8 pads, a color for the 4
 *     pad-mode buttons, and a preview page.
 *   - A filter on the pad LED frame (AZ_BUS_ERP_LED) rewrites the cells of both sides just
 *     before the frame goes to the panel. It keeps what the deck itself decided: a pad or
 *     button the deck leaves dark stays dark, a dimmed one stays dimmed, and animations
 *     (blinks, fades) keep their rhythm, because only the hue is replaced.
 *   - LED bytes are linear light, not sRGB (docs/reference-frames.md): the palette below
 *     holds the converted bytes next to the sRGB color shown on the preview page.
 */
#include <stdio.h>
#include <string.h>

#include "azmod-mod.h"

#define MOD_ID "mvcai-pad-colors"

static const az_core_v1 *C;                 /* set once in init() */

/* The palette. Index 0 is "Defaut": the deck's own colors, nothing is rewritten. */
typedef struct {
    const char *name;
    uint32_t srgb;                          /* 0xRRGGBB, for the preview */
    unsigned char led[3];                   /* the same color as linear LED bytes */
} pal_t;
static const pal_t PAL[] = {
    { "Defaut", 0x808080, { 0, 0, 0 } },
    { "Rouge", 0xFF0000, { 255, 0, 0 } },
    { "Orange", 0xFF6000, { 255, 30, 0 } },
    { "Jaune", 0xFFD000, { 255, 161, 0 } },
    { "Vert", 0x00FF30, { 0, 255, 8 } },
    { "Bleu", 0x0020FF, { 0, 4, 255 } },
    { "Violet", 0xA000FF, { 90, 0, 255 } },
    { "Blanc", 0xFFFFFF, { 255, 255, 255 } },
};
#define N_PAL 8
static const char *const PAL_NAMES[N_PAL] = { "Defaut", "Rouge", "Orange", "Jaune",
                                              "Vert",   "Bleu",  "Violet", "Blanc" };

static int h_enabled = -1, h_pads = -1, h_modes = -1;   /* row handles */
static int filter_ok;                       /* frame_filter took the pad LED frame */

/* ---- the LED frame ----------------------------------------------------------------- */

#define FRAME_MIN 0x78                      /* bytes 0..0x77 hold the cells; 0x78.. is the CRC */
#define SIDE_STRIDE 0x38                    /* side 2's cells sit this far after side 1's */
#define MODE_BASE 0x0e                      /* 4 pad-mode buttons, 4 bytes each */
#define PAD_BASE 0x1e                       /* 8 performance pads, 4 bytes each */

static unsigned char max3(const unsigned char *c)
{
    unsigned char m = c[0];
    if (c[1] > m) m = c[1];
    if (c[2] > m) m = c[2];
    return m;
}

static void put_scaled(unsigned char *c, const unsigned char *led, unsigned scale)
{
    c[0] = (unsigned char)((led[0] * scale + 127) / 255);
    c[1] = (unsigned char)((led[1] * scale + 127) / 255);
    c[2] = (unsigned char)((led[2] * scale + 127) / 255);
}

/* Pads: keep the deck's brightness, replace its hue. A dark cell stays dark. */
static void recolor_pads(unsigned char *f, const pal_t *p)
{
    for (int side = 0; side < 2; side++)
        for (int k = 0; k < 8; k++) {
            unsigned char *c = f + PAD_BASE + SIDE_STRIDE * side + 4 * k;
            unsigned char m = max3(c);
            if (m) put_scaled(c, p->led, m);
        }
}

/* Mode buttons, by the deck's own convention: a channel at or above 0x40 is the selected
 * button (full color), all zero is off, anything else is dimmed (12/255 of the color).
 * The lime the deck shows on a secondary pad page (blue 0, green above red) is the only
 * sign of which page is active, so it is left alone. */
static void recolor_modes(unsigned char *f, const pal_t *p)
{
    for (int side = 0; side < 2; side++)
        for (int j = 0; j < 4; j++) {
            unsigned char *c = f + MODE_BASE + SIDE_STRIDE * side + 4 * j;
            unsigned char m = max3(c);
            if (m == 0) continue;
            if (c[2] == 0 && c[1] > c[0]) continue;      /* secondary page lime */
            put_scaled(c, p->led, m >= 0x40 ? 255u : 12u);
        }
}

/* Bus thread, microseconds: a few atomic loads and 24 small cells, no log, no I/O. */
static int led_filter(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    if (bus != AZ_BUS_ERP_LED || !f || len < FRAME_MIN) return AZ_FRAME_OK;
    if (h_enabled < 0 || h_pads < 0 || h_modes < 0) return AZ_FRAME_OK;
    if (C->row_value(h_enabled) != 1) return AZ_FRAME_OK;
    int pi = C->row_value(h_pads), mi = C->row_value(h_modes);
    if (pi > 0 && pi < N_PAL) recolor_pads(f, &PAL[pi]);
    if (mi > 0 && mi < N_PAL) recolor_modes(f, &PAL[mi]);
    return AZ_FRAME_OK;
}

/* A core without frame_filter still calls this one for the same bus; it stays quiet when
 * the filter is registered, so a frame is never recolored twice. */
static int on_frame(int bus, unsigned char *frame, int len)
{
    if (filter_ok) return AZ_FRAME_OK;
    return led_filter(bus, frame, len, NULL);
}

/* ---- the preview page -------------------------------------------------------------- */

/* Draw s, shortened from the end (whole UTF-8 characters) until it fits in max_w pixels:
 * the core cuts drawing off at the page's edge but does not shorten text to fit. */
static void draw_fitted(az_draw *d, int x, int y, const char *s, int px, uint32_t color, int max_w)
{
    char buf[96];
    snprintf(buf, sizeof buf, "%s", s);
    size_t len = strlen(buf);
    while (len > 0 && AZ_HAS(C, text_width) && C->text_width(buf, px) > max_w) {
        do len--;
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
        buf[len] = '\0';
    }
    if (len > 0) C->draw_text(d, x, y, buf, px, color);
}

static void swatches(az_draw *d, az_rect area, int y, int size, int n, int choice)
{
    const int gap = size / 4;
    int x = area.x + area.w / 3;
    const uint32_t dim = C->color(AZ_COL_TEXT_DIM);
    for (int i = 0; i < n; i++) {
        if (x + size > area.x + area.w) return;           /* never past the page's edge */
        if (choice > 0 && choice < N_PAL) {
            C->fill_rect(d, x, y, size, size, PAL[choice].srgb);
        } else {
            C->draw_rect(d, x, y, size, size, 2, dim);      /* the deck's own colors */
        }
        x += size + gap;
    }
}

static void on_draw(az_draw *d, az_rect area, void *user)
{
    (void)user;
    const uint32_t text = C->color(AZ_COL_TEXT);
    const uint32_t dim = C->color(AZ_COL_TEXT_DIM);
    const int row = C->row_height(d);
    int px = row / 2;
    if (px < 8) px = 8;
    if (px > 96) px = 96;
    const int pad = row / 4;
    const int bottom = area.y + area.h;
    int y = area.y;

    C->draw_header(d, "Couleurs des pads");
    y += row;

    if (h_pads < 0 || h_modes < 0) {
        if (y + row > bottom) return;
        draw_fitted(d, area.x + pad, y + pad, "Reglages indisponibles.", px, dim, area.w - 2 * pad);
        return;
    }
    const int pc = C->row_value(h_pads), mc = C->row_value(h_modes);
    const int on = h_enabled >= 0 && C->row_value(h_enabled) == 1;
    const int size = row - 2 * pad;

    if (y + row > bottom) return;
    draw_fitted(d, area.x + pad, y + pad, "Pads", px, on ? text : dim, area.w / 3 - pad);
    swatches(d, area, y + pad, size, 8, on ? pc : 0);
    y += row;

    if (y + row > bottom) return;
    draw_fitted(d, area.x + pad, y + pad, "Boutons", px, on ? text : dim, area.w / 3 - pad);
    swatches(d, area, y + pad, size, 4, on ? mc : 0);
}

static int on_input(const az_event *ev, void *user)
{
    (void)ev;
    (void)user;
    return 0;                                /* BACK and the rest: the core decides */
}

static const az_screen_v1 SCREEN = {
    .size = sizeof(az_screen_v1),
    .on_draw = on_draw,
    .on_input = on_input,
    .user = NULL,
};

/* ---- lifecycle --------------------------------------------------------------------- */

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;

    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        const az_row_v1 enabled = {
            .size = sizeof(az_row_v1),
            .key = "enabled",
            .label = "Couleurs des pads",
            .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON,
            .help = "Remplace la couleur des pads",
        };
        h_enabled = C->register_row(MOD_ID, &enabled);

        const az_row_v1 pads = {
            .size = sizeof(az_row_v1),
            .key = "pads",
            .label = "Pads de performance",
            .kind = AZ_ROW_CHOICE,
            .parent = h_enabled >= 0 ? "enabled" : NULL,
            .help = "Couleur des 8 pads",
            .choices = PAL_NAMES,
            .n_choices = N_PAL,
            .default_choice = 0,
        };
        h_pads = C->register_row(MOD_ID, &pads);

        const az_row_v1 modes = {
            .size = sizeof(az_row_v1),
            .key = "modes",
            .label = "Boutons de mode",
            .kind = AZ_ROW_CHOICE,
            .parent = h_enabled >= 0 ? "enabled" : NULL,
            .help = "Couleur des 4 boutons de mode",
            .choices = PAL_NAMES,
            .n_choices = N_PAL,
            .default_choice = 0,
        };
        h_modes = C->register_row(MOD_ID, &modes);

        const az_row_v1 preview = {
            .size = sizeof(az_row_v1),
            .key = "preview",
            .label = "Apercu",
            .kind = AZ_ROW_SCREEN,
            .parent = h_enabled >= 0 ? "enabled" : NULL,
            .help = "Les couleurs choisies",
            .screen = &SCREEN,
        };
        C->register_row(MOD_ID, &preview);
    } else if (C->register_screen(MOD_ID, &SCREEN) != 0) {
        C->log(MOD_ID, "register_screen refused; staying loaded without a page");
    }

    /* Last: a filter stays registered until EP147 restarts. */
    if (AZ_HAS(C, frame_filter) &&
        C->frame_filter(MOD_ID, 1u << AZ_BUS_ERP_LED, led_filter, NULL) == 0)
        filter_ok = 1;
    C->log(MOD_ID, "loaded, rows=%d, filter=%d", h_enabled >= 0, filter_ok);
    return 0;
}

/* "any": core services only. No stop(): a frame filter cannot be removed while EP147 runs;
 * the checkbox above switches the recoloring off live. */
static const char *const FW_TAGS[] = { "any", NULL };

static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = NULL,
    .on_frame = on_frame,
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
