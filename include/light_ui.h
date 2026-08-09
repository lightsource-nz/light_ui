#ifndef _LIGHT_UI_H
#define _LIGHT_UI_H

#include <light.h>
#include <light_canvas.h>
#include <light_display.h>
#include <rend.h>

#include <stdint.h>
#include <stdbool.h>

// longest label/title light_ui will render. labels are truncated to whatever fits the
// widget anyway (see the fixed-pitch note below), so this only bounds the scratch buffer
// the truncated copy is built in
#define LIGHT_UI_TEXT_MAX               64

#define UI_WIDGET_WINDOW                0
#define UI_WIDGET_BUTTON                1
#define UI_WIDGET_LABEL                 2

// an inclusive rectangle in LOGICAL rend coordinates -- the same space the caller draws in
// (post-rotation, see rend_context_set_rotation()), never the physical buffer's.
//
// signed, unlike rend_point2d: a widget positioned partly off-canvas is clipped by
// light_ui before anything reaches rend, and uint16_t would wrap those coordinates into
// huge positive values instead of letting the clip see them as negative
struct ui_rect {
        int16_t x0;
        int16_t y0;
        int16_t x1;
        int16_t y1;
};

struct ui_widget;
struct ui_context;

// widgets are plain allocations owned by their ui_context, NOT light_objects. the object
// tree exists to name and refcount *devices* -- discoverable hardware whose add triggers
// driver init. a button is neither, and registering twenty of them per screen would fill
// the object registry with things nothing ever looks up by name
struct ui_widget {
        uint8_t type;
        // ABSOLUTE canvas coordinates, not parent-relative. one coordinate space for every
        // widget means a hit test, a clip and an invalidation are all the same arithmetic
        // wherever a widget sits in the tree. light_ui_window_layout_stack() exists so the
        // common case doesn't have to compute these by hand
        struct ui_rect rect;
        bool visible;
        bool focusable;
        bool enabled;
        struct ui_context *ui;
        struct ui_widget *parent;
        struct ui_widget *next_sibling;
        struct ui_widget *first_child;
};

struct ui_window {
        struct ui_widget widget;
        // NULL for an untitled frame
        const uint8_t *title;
        // gap kept between the window's frame and its content area, which is what
        // light_ui_window_layout_stack() divides up
        uint8_t padding;
        bool border;
};

struct ui_button {
        struct ui_widget widget;
        const uint8_t *label;
        void (*on_press)(struct ui_button *, void *);
        void *user_data;
};

struct ui_label {
        struct ui_widget widget;
        const uint8_t *text;
};

struct ui_context {
        // owns the render context, the displays it is presented on, frame pacing, buffer
        // swapping and dirty-region flushing. light_ui contributes only the widget tree and
        // which parts of it changed
        struct canvas_context *canvas;
        struct ui_widget *root;
        struct ui_widget *focused;

        // set by any invalidation, cleared once the repaint has been pushed. distinct from
        // the canvas's own region list: this answers "is there anything to draw", which
        // decides whether to open a frame at all
        bool dirty;
};

#define to_ui_window(ptr) container_of(ptr, struct ui_window, widget)
#define to_ui_button(ptr) container_of(ptr, struct ui_button, widget)
#define to_ui_label(ptr) container_of(ptr, struct ui_label, widget)

extern void light_ui_init();

// binds a UI to the canvas it is presented through. the canvas's render context must
// already have a font set (rend_context_set_font()) -- rend_draw_text() is a silent no-op
// without one, so an unfonted context renders frames with no labels in them
extern struct ui_context *light_ui_create_context(struct canvas_context *canvas);

// parent may be NULL for the root widget. rect is absolute (see struct ui_widget)
extern struct ui_window *light_ui_window_create(struct ui_context *ui, struct ui_widget *parent,
                                                struct ui_rect rect, const uint8_t *title);
extern struct ui_button *light_ui_button_create(struct ui_context *ui, struct ui_widget *parent,
                                                struct ui_rect rect, const uint8_t *label,
                                                void (*on_press)(struct ui_button *, void *),
                                                void *user_data);
extern struct ui_label *light_ui_label_create(struct ui_context *ui, struct ui_widget *parent,
                                                struct ui_rect rect, const uint8_t *text);

// divides the window's content area into equal-height rows, one per visible child, and
// assigns each child's rect accordingly. rows are separated by `gap` pixels.
//
// this is what makes a 64x128 OLED usable without hand-computing rects, and it is the
// natural companion to focus-cycling navigation: a vertical stack has an obvious visual
// order, and "next" means the row below
extern void light_ui_window_layout_stack(struct ui_window *win, uint8_t gap);

// --- input ---
// deliberately hardware-free: light_ui depends on neither light_touch nor light_button, so
// it builds and can be exercised on a host with no device modules present at all. each
// application maps its own devices onto these (a few lines per app -- see screen-test's UI
// demo apps for both the two-button and the touch wiring)
extern void light_ui_input_focus_next(struct ui_context *ui);
extern void light_ui_input_focus_prev(struct ui_context *ui);
// activates the focused widget, i.e. fires a button's on_press
extern void light_ui_input_activate(struct ui_context *ui);
// hit-tests (x, y) against the tree and, if it lands on an actionable widget, focuses AND
// activates it -- a touch is a complete interaction, not just a focus move. returns whether
// anything was hit.
//
// x/y are LOGICAL rend coordinates. light_touch reports in its device's own (physical
// panel) space, so a caller whose render context is rotated must map them first; light_ui
// has no way to invert rend's transform today. neither bring-up rig needs it
// (ws_touch169 is REND_ROTATE_0, and the rotated po13 has no touch panel), so the inverse
// belongs in rend alongside rend_transform_rect() whenever a rotated touch device appears
extern bool light_ui_input_press_at(struct ui_context *ui, uint16_t x, uint16_t y);

extern void light_ui_set_focus(struct ui_context *ui, struct ui_widget *w);
extern void light_ui_widget_set_visible(struct ui_widget *w, bool visible);
extern void light_ui_widget_set_enabled(struct ui_widget *w, bool enabled);
extern void light_ui_button_set_label(struct ui_button *btn, const uint8_t *label);
extern void light_ui_label_set_text(struct ui_label *lbl, const uint8_t *text);

// marks a widget's area as needing to be pushed to the panel. widget mutators above call
// this for you; it is public for anything that changes what a custom widget draws
extern void light_ui_invalidate_widget(struct ui_widget *w);
// marks the whole canvas. needed once at startup, before the first render, since nothing
// on the panel matches the (empty) tree yet
extern void light_ui_invalidate(struct ui_context *ui);

// repaints and pushes, if anything is dirty. call every tick from the application's
// periodic task -- the canvas's own frame pacing decides when a frame actually happens, and
// this is a no-op on the ticks in between.
//
// the ENTIRE tree is repainted, not just the dirty widgets, because light_canvas clears the
// buffer at the start of every frame (see light_canvas_frame_begin()). a retained UI only
// changes on input, so this runs a handful of times per second at most, and only the pushed
// REGION is optimised -- which is where the cost that actually scales with panel size lives
// (1KB for a 1bpp OLED, 134KB for a 240x280 RGB565 panel)
extern void light_ui_render(struct ui_context *ui);

#endif
