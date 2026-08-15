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

// how long a page transition takes. shorter than a rotation: a rotation is re-orienting the
// whole interface and wants to be followed, while a page change is a step through a structure
// the user already has in mind, and waiting for it is what makes an interface feel slow
#define LIGHT_UI_PAGE_MOVE_MS           180

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
struct ui_page;

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
        // 0 for a square button. set by light_ui_window_layout_stack() on a row that sits
        // flush against the inside of a rounded window, so the row follows the container's
        // curve instead of stopping short of it -- `corners` names only the ones that touch
        // it (REND_CORNER_* ), leaving the edge shared with the next row square
        uint8_t corner_radius;
        uint8_t corners;
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

        // --- navigation (see struct ui_page) ---
        // the page currently built into the tree, or NULL for a context whose tree was built
        // directly with light_ui_build() and is not navigating between pages at all
        const struct ui_page *page;
        //   where light_ui_navigate_back() goes, when it is not simply the current page's
        // parent. Set only by light_ui_navigate_returning(), and cleared by every ordinary
        // navigation, so an override applies to exactly the one transfer that asked for it
        const struct ui_page *return_page;

        // --- page transition animation, driven from light_ui_render() ---
        //   while active, each frame draws the incoming page and then slides the image
        // captured before the transfer off it, so the outgoing page appears to move away and
        // reveal the new one. Only the outgoing image is stored: the incoming page is the live
        // widget tree, redrawn each step, which is why this costs one buffer and not two
        bool page_moving;
        // PHYSICAL unit direction the outgoing image travels in, derived from the logical
        // direction so a transition reads the same way whatever the panel is rotated to
        int8_t page_move_dx;
        int8_t page_move_dy;
        // how far it has to travel to leave: the buffer's extent along that axis
        uint16_t page_move_span;
        uint32_t page_move_start_ms;
        uint16_t page_move_duration_ms;

        //   a rotation asked for while a transition was running, applied once it finishes.
        // The two animations both want the back buffer and the frame, so they cannot overlap;
        // deferring rather than dropping either one means the transition is seen through and
        // the device still ends up the right way up
        bool rotate_deferred;
        uint8_t rotate_deferred_target;

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

// a swipe's direction in the LOGICAL frame, i.e. the one the user is looking at
#define UI_SWIPE_NONE                   0
#define UI_SWIPE_UP                     1
#define UI_SWIPE_DOWN                   2
#define UI_SWIPE_LEFT                   3
#define UI_SWIPE_RIGHT                  4

//   classifies a swipe given its two endpoints in PANEL coordinates -- exactly what a touch
// controller reports -- and returns a UI_SWIPE_* direction in the logical frame.
//
//   this exists because a touch controller classifies gestures in the PANEL's frame, which is
// fixed to the glass, while the user is swiping relative to the interface, which rotates. At
// REND_ROTATE_0 the two agree and a controller's own "swipe right" is right; at 90 or 270 they
// are perpendicular, so a gesture code taken at face value acts on the wrong axis and appears
// to work only in one orientation. It is the same mistake light_ui_input_press_at() exists to
// prevent for taps, and it is solved the same way and in the same place: light_ui owns the
// render context and therefore the rotation, so it can do the conversion and the caller cannot.
//
//   pass the endpoints rather than the controller's own gesture code. The code has already
// discarded the direction into the panel's frame, and no amount of rotating a label recovers
// which way the finger actually moved.
//
// returns UI_SWIPE_NONE for a swipe with no dominant axis, which includes the degenerate case
// of both endpoints clamping to the same edge of the canvas
extern uint8_t light_ui_swipe_direction(struct ui_context *ui,
                                        uint16_t start_x, uint16_t start_y,
                                        uint16_t end_x, uint16_t end_y);

// --- declarative definitions -------------------------------------------------------------
//
// a widget tree can be written out as static data and realised in one call, instead of as a
// sequence of light_ui_*_create() calls. the shape of the interface then reads off the source
// the way font-crusher's command hierarchy reads off its Light_Command_Define() list:
//
//      Light_UI_Button_Define(_btn_ok,     "OK",     _on_press, (void *)0);
//      Light_UI_Button_Define(_btn_cancel, "Cancel", _on_press, (void *)1);
//      Light_UI_Window_Define(_main_window, "Title",
//              Light_UI_Rounded(8),
//              Light_UI_Stack(2),
//              Light_UI_Children(&_btn_ok, &_btn_cancel));
//
//      light_ui_build(ui, NULL, &_main_window);
//
// ONE DIFFERENCE FROM light_cli IS DELIBERATE, and it is the reason this does not simply copy
// that pattern. Light_Command_Define() names the PARENT in the child and collects siblings
// through a linker section, so sibling order is link order. A command tree does not care:
// commands are found by name. A widget tree cares a great deal -- sibling order is paint
// order, focus order, and the top-to-bottom order of rows in a stack layout all at once (see
// _widget_init()) -- and link order is not something a source file controls. So here the
// PARENT lists its children, in the order they are meant to appear.
//
// descriptors are const and contain no state: the same one can build the same subtree into
// two different contexts, and it can live in flash.

struct ui_desc {
        uint8_t type;
        // window title, button label, or label text, by type. one field rather than three
        // because every widget kind so far carries exactly one string, and a union of
        // identically-typed members is just a longer way to write this
        const uint8_t *text;
        void (*on_press)(struct ui_button *, void *);
        void *user_data;
        uint8_t corner_radius;
        uint8_t layout;
        uint8_t layout_gap;
        // left zeroed for anything a layout will place, which is the usual case. only
        // meaningful for a hand-placed widget under a UI_LAYOUT_NONE parent
        struct ui_rect rect;
        // where to store the created widget, for handlers that need to reach it later.
        // optional -- an on_press already receives its own button
        void **bind;
        const struct ui_desc *const *children;
        uint8_t child_count;
};

// the array is a file-scope compound literal, so it has static storage duration and its
// address is a constant expression -- which is what lets a const ui_desc be initialised with
// it. the sizeof is over a second, identical literal purely to count the arguments; the
// compiler folds it, and no second array survives into the image
#define Light_UI_Children(...) \
        .children = (const struct ui_desc *const[]){ __VA_ARGS__ }, \
        .child_count = (uint8_t)(sizeof((const struct ui_desc *const[]){ __VA_ARGS__ }) \
                                        / sizeof(const struct ui_desc *))

// modifiers, written as designated-initialiser fragments so they can be given in any order
// and omitted entirely
#define Light_UI_Stack(gap)             .layout = UI_LAYOUT_STACK, .layout_gap = (gap)
#define Light_UI_Rounded(radius)        .corner_radius = (radius)
#define Light_UI_Rect(_x0, _y0, _x1, _y1) \
        .rect = { (_x0), (_y0), (_x1), (_y1) }
#define Light_UI_Bind(ptr)              .bind = (void **)&(ptr)

#define Light_UI_Window_Define(sym, _title, ...) \
        static const struct ui_desc sym = { \
                .type = UI_WIDGET_WINDOW, .text = (const uint8_t *)(_title), __VA_ARGS__ }
#define Light_UI_Button_Define(sym, _label, _on_press, _user_data, ...) \
        static const struct ui_desc sym = { \
                .type = UI_WIDGET_BUTTON, .text = (const uint8_t *)(_label), \
                .on_press = (_on_press), .user_data = (_user_data), __VA_ARGS__ }
#define Light_UI_Label_Define(sym, _text, ...) \
        static const struct ui_desc sym = { \
                .type = UI_WIDGET_LABEL, .text = (const uint8_t *)(_text), __VA_ARGS__ }

// realises `desc` and its children under `parent` (NULL for the context's root widget), and
// returns the widget created for `desc` itself, or NULL if the descriptor names a type this
// build does not know.
//
// equivalent to the light_ui_*_create() calls it replaces, in the order that gets the details
// right: a window's corner radius is applied before its children exist, so the clearance the
// curve needs is already accounted for when the single layout pass runs, and the layout runs
// after them, since it divides the content area between them.
//
// building a ROOT (parent == NULL) also relayouts, sizing the tree to the canvas. a descriptor
// cannot carry the root's rect -- the canvas size is a runtime fact -- and that is what lets
// the same descriptor serve a 64x128 OLED and a 240x280 panel unchanged
extern struct ui_widget *light_ui_build(struct ui_context *ui, struct ui_widget *parent,
                                        const struct ui_desc *desc);

// --- pages and navigation ------------------------------------------------------------------
//
// a page is a named descriptor tree plus its place in the interface's structure. Navigating
// tears the current tree down and builds the new one, so only one page's widgets exist at a
// time -- which is what keeps a deep interface affordable on a part with 520K of RAM, and the
// reason descriptors were made const and reusable in the first place.
//
//      Light_UI_Page_Declare(page_settings);
//
//      Light_UI_Button_Define(_btn_settings, "Settings", _on_open_settings, NULL);
//      Light_UI_Window_Define(_home_window, "Home", Light_UI_Children(&_btn_settings));
//      Light_UI_Page_Define(page_home, NULL, _home_window);
//
//      Light_UI_Window_Define(_settings_window, "Settings", ...);
//      Light_UI_Page_Define(page_settings, &page_home, _settings_window);
//
//      static void _on_open_settings(struct ui_button *b, void *d)
//      { light_ui_navigate(b->widget.ui, &page_settings); }
//
//   PARENT, NOT HISTORY. `parent` describes where a page sits in the interface, the same way
// a directory knows its containing directory. Back therefore goes somewhere predictable no
// matter how the user arrived, and costs no stack -- a history list would have to be bounded,
// and the bound would be reached by exactly the aimless wandering it exists to serve.
//
//   Pages are non-static so they can reference each other across a cycle (a child names its
// parent, a parent's handler names the child); use Light_UI_Page_Declare() for the forward
// reference. They are const and hold no state, so they live in flash.
struct ui_page {
        // the widget tree this page shows. built under the context root on navigation
        const struct ui_desc *content;
        // where light_ui_navigate_back() goes by default. NULL for a top-level page, which
        // is what makes back a no-op there rather than an error
        const struct ui_page *parent;
};

#define Light_UI_Page_Declare(sym) \
        extern const struct ui_page sym
#define Light_UI_Page_Define(sym, _parent, _content) \
        const struct ui_page sym = { .content = &(_content), .parent = (_parent) }

//   builds `page` in place of whatever the context is showing, freeing the old tree. Back
// from here goes to page->parent.
//
// safe to call from a widget handler -- the handler's own widget is destroyed by this, so it
// must not touch it afterwards, which is why the navigating call is conventionally the last
// statement in the handler
extern void light_ui_navigate(struct ui_context *ui, const struct ui_page *page);

//   the same, but back from `page` goes to `return_page` rather than to page->parent. For a
// cross-tree jump: a page reached from somewhere other than its structural parent, which
// should return to where it was actually reached from.
//
// the override lasts exactly one page. Navigating onward from `page` clears it, so the page
// after that goes back to ITS parent, not to this return address
extern void light_ui_navigate_returning(struct ui_context *ui, const struct ui_page *page,
                                        const struct ui_page *return_page);

//   goes to the current page's return address if one was set, otherwise to its parent.
// Returns false and changes nothing when there is nowhere to go -- a top-level page, or a
// context not using pages at all -- so a caller can leave the gesture meaning nothing there
// rather than having to know the structure itself.
//
//   light_ui has no idea what a swipe is, for the same reason it has no idea what a button
// or an IMU is (see the input section above): an application maps its own hardware onto this,
// typically TOUCH_GESTURE_SWIPE_RIGHT
extern bool light_ui_navigate_back(struct ui_context *ui);

// the page currently shown, or NULL for a context built directly with light_ui_build()
static inline const struct ui_page *light_ui_current_page(struct ui_context *ui)
{
        return ui->page;
}

//   frees a widget and everything under it, unlinking it from its parent (or from the
// context root) first. Focus is cleared if it pointed anywhere inside.
//
// public because navigation is not the only reason a subtree stops being wanted, but note
// that widgets are owned by their context: this is the only correct way to release one, and
// nothing else frees them. Building a second root over the first used to simply drop it
extern void light_ui_widget_destroy(struct ui_widget *w);

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
