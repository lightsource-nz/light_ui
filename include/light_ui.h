#ifndef _LIGHT_UI_H
#define _LIGHT_UI_H

#include <light.h>
#include <light_canvas.h>
#include <light_display.h>
#include <rend.h>

#include <stdint.h>
#include <stdbool.h>

// how long a rotation takes to animate. long enough to read as a turn rather than a glitch,
// short enough not to feel like waiting -- and the panel is unresponsive to nothing during
// it, since input is still collected, only the drawing is given over to the animation
#define LIGHT_UI_ROTATE_MS              280

// longest label/title light_ui will render. labels are truncated to whatever fits the
// widget anyway (see the fixed-pitch note below), so this only bounds the scratch buffer
// the truncated copy is built in
#define LIGHT_UI_TEXT_MAX               64

#define UI_WIDGET_WINDOW                0
#define UI_WIDGET_BUTTON                1
#define UI_WIDGET_LABEL                 2

// how a window arranges its children when layout is (re-)run. recorded on the window rather
// than passed in each time, so a rotation or resize can re-apply it without the application
// having to be told to lay everything out again
#define UI_LAYOUT_NONE                  0
#define UI_LAYOUT_STACK                 1

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
        // 0 for a square frame. non-zero draws the border as a rounded rectangle and keeps
        // content clear of the curve -- see light_ui_window_set_corner_radius()
        uint8_t corner_radius;
        // the arrangement to re-apply on light_ui_relayout(), and its parameter. set by
        // whichever light_ui_window_layout_*() call was last used; UI_LAYOUT_NONE for a
        // window whose children were placed by hand
        uint8_t layout;
        uint8_t layout_gap;
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

        // pixels kept clear on every edge, for panels whose glass does not show the whole
        // pixel grid (see light_ui_set_safe_inset())
        uint8_t safe_inset;

        // --- rotation animation, driven from light_ui_render() ---
        // while active, frames show the pre-rotation image turning rather than the widget
        // tree; the real rotation is applied once, on the final step
        bool rotating;
        uint8_t rotate_target;
        // total turn in degrees, signed: the shortest route between the two quadrants
        int16_t rotate_degrees;
        uint32_t rotate_start_ms;
        uint16_t rotate_duration_ms;
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

// rounds the window's frame, and keeps its content clear of the curve. meant for a root
// window drawn at the edge of glass that is itself rounded: the frame then follows the panel
// instead of floating in a square inside it, which is the band a uniform safe inset costs.
//
// the clearance this buys is NOT uniform, and that is the whole subtlety. a corner only eats
// into the rows within `radius` of the top and bottom edges; below that the left edge is back
// at x0. so content is pushed down and up by the radius, while the horizontal inset stays at
// border+padding -- pushing it in by the radius on all four sides would give back exactly the
// width this was supposed to recover.
//
// re-lays-out the window, since its content area has just changed
extern void light_ui_window_set_corner_radius(struct ui_window *win, uint8_t radius);

// re-runs layout against the canvas as it is NOW: the root widget is resized to fill it,
// then every window re-applies the arrangement it recorded. a UI_LAYOUT_NONE window's
// children are left exactly where they are, because light_ui has no idea what hand-placed
// rects were meant to express.
//
// called for you by light_ui_set_rotation(); public because a canvas can change size for
// other reasons
extern void light_ui_relayout(struct ui_context *ui);

// keeps `inset` pixels clear on every edge of the canvas, for a panel whose glass does not
// actually show the whole pixel grid. rounded corners are the usual reason: the pixels are
// addressable and get drawn, they are simply not visible, so content in a corner silently
// disappears rather than failing in any way the code could notice.
//
// UNIFORM rather than per-edge, and that is not laziness: the interface rotates, so an inset
// that differed between edges would be applied to the wrong ones the moment the board turned
// -- the corners are fixed in the PANEL's frame while the insets would be expressed in the
// logical one. a uniform inset is the only value invariant under rotation.
//
// set it to the corner radius. that costs a band of that width on all four sides, which is
// the price of every corner being safe in every orientation. applied by light_ui_relayout(),
// so it takes effect on the next layout
extern void light_ui_set_safe_inset(struct ui_context *ui, uint8_t inset);

// rotates the whole interface (a REND_ROTATE_* value), so it can be kept upright as the
// device is turned. a no-op if the rotation is unchanged; otherwise it re-lays-out (the
// canvas aspect has just flipped) and invalidates everything.
//
// safe between frames precisely because light_canvas clears and fully repaints every frame,
// so rend's usual "set rotation once, before any drawing" caveat does not apply -- there is
// no stale buffer content left to be transformed. do NOT call it from inside a frame, i.e.
// between light_canvas_frame_begin() and frame_end().
//
// light_ui deliberately knows nothing about orientation SENSORS: an application maps its own
// IMU_ORIENT_* (or anything else) onto a rotation, the same way it maps its own touch and
// button hardware onto the input calls below. that mapping depends on whether the panel is
// natively portrait or landscape, which is a board fact, not a UI one
extern void light_ui_set_rotation(struct ui_context *ui, uint8_t rotation);

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
// x/y are PANEL coordinates, i.e. the display's own physical frame, which is exactly what a
// touch controller reports -- pass them through unmodified. light_ui untransforms them into
// the logical space the widgets live in (rend_untransform_point()), which it can do and the
// caller cannot, since light_ui owns the render context and therefore the rotation.
//
// this took logical coordinates before rotation existed, which was indistinguishable from
// panel coordinates only because every rig ran REND_ROTATE_0. under rotation the two differ,
// and getting it wrong sends taps to the wrong widget -- so the conversion lives here rather
// than being a rule each application has to remember.
//
// the assumption is that the touch panel is aligned with the display and reports over the
// same coordinate range; where it isn't, scale before calling
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
