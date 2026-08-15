#include <light_ui.h>
#include <light_platform.h>

#include "light_ui_internal.h"

#include <string.h>

void light_ui_init()
{
        // nothing global to set up: a ui_context owns everything, and an application may
        // create more than one (e.g. one per display). kept for symmetry with every other
        // module's LF_EVENT_MODULE_LOAD hook, and as the place per-module state would go
        light_trace("");
}

bool _ui_clip_to_canvas(const struct ui_context *ui, struct ui_rect *r)
{
        const struct rend_context *render = _ui_render(ui);
        int16_t max_x = (int16_t)render->dim_x - 1;
        int16_t max_y = (int16_t)render->dim_y - 1;

        if(r->x0 < 0) r->x0 = 0;
        if(r->y0 < 0) r->y0 = 0;
        if(r->x1 > max_x) r->x1 = max_x;
        if(r->y1 > max_y) r->y1 = max_y;
        return !_ui_rect_empty(r);
}

struct ui_context *light_ui_create_context(struct canvas_context *canvas)
{
        struct ui_context *ui = light_alloc(sizeof(struct ui_context));
        ui->canvas = canvas;
        ui->root = NULL;
        ui->focused = NULL;
        ui->dirty = false;
        ui->rotating = false;
        ui->rotate_target = canvas->render->rotation;
        ui->rotate_degrees = 0;
        ui->rotate_start_ms = 0;
        ui->rotate_duration_ms = LIGHT_UI_ROTATE_MS;
        ui->safe_inset = 0;
        // light_alloc() does not zero, and navigate_back() reads both of these before anything
        // has necessarily navigated
        ui->page = NULL;
        ui->return_page = NULL;
        ui->page_moving = false;
        ui->page_move_dx = 0;
        ui->page_move_dy = 0;
        ui->page_move_span = 0;
        ui->page_move_start_ms = 0;
        ui->page_move_duration_ms = LIGHT_UI_PAGE_MOVE_MS;
        ui->rotate_deferred = false;
        ui->rotate_deferred_target = canvas->render->rotation;
        if(!canvas->render->font)
                light_warn("render context '%s' has no font -- widget labels will not render",
                                canvas->render->name);
        return ui;
}

// --- tree ---

static void _widget_init(struct ui_widget *w, struct ui_context *ui, struct ui_widget *parent,
                        uint8_t type, struct ui_rect rect, bool focusable)
{
        w->type = type;
        w->rect = rect;
        w->visible = true;
        w->focusable = focusable;
        w->enabled = true;
        w->ui = ui;
        w->parent = parent;
        w->next_sibling = NULL;
        w->first_child = NULL;

        if(!parent) {
                // TODO support more than one root widget per context
                if(ui->root)
                        light_warn("ui context already has a root widget; replacing it");
                ui->root = w;
                return;
        }
        // appended rather than prepended: sibling order is both paint order (later siblings
        // draw on top) and focus order, and neither reads correctly reversed
        struct ui_widget **slot = &parent->first_child;
        while(*slot)
                slot = &(*slot)->next_sibling;
        *slot = w;
}

struct ui_window *light_ui_window_create(struct ui_context *ui, struct ui_widget *parent,
                                        struct ui_rect rect, const uint8_t *title)
{
        struct ui_window *win = light_alloc(sizeof(struct ui_window));
        win->title = title;
        win->padding = 2;
        win->border = true;
        win->corner_radius = 0;
        // hand-placed until a layout call says otherwise, so relayout leaves its children
        // alone rather than rearranging rects somebody chose deliberately
        win->layout = UI_LAYOUT_NONE;
        win->layout_gap = 0;
        _widget_init(&win->widget, ui, parent, UI_WIDGET_WINDOW, rect, false);
        return win;
}

struct ui_button *light_ui_button_create(struct ui_context *ui, struct ui_widget *parent,
                                        struct ui_rect rect, const uint8_t *label,
                                        void (*on_press)(struct ui_button *, void *),
                                        void *user_data)
{
        struct ui_button *btn = light_alloc(sizeof(struct ui_button));
        btn->label = label;
        btn->on_press = on_press;
        btn->user_data = user_data;
        btn->corner_radius = 0;
        btn->corners = REND_CORNER_NONE;
        _widget_init(&btn->widget, ui, parent, UI_WIDGET_BUTTON, rect, true);
        // the first focusable widget created takes focus, so a two-button rig always has
        // somewhere to start cycling from without the application having to say so
        if(!ui->focused)
                ui->focused = &btn->widget;
        return btn;
}

struct ui_label *light_ui_label_create(struct ui_context *ui, struct ui_widget *parent,
                                        struct ui_rect rect, const uint8_t *text)
{
        struct ui_label *lbl = light_alloc(sizeof(struct ui_label));
        lbl->text = text;
        _widget_init(&lbl->widget, ui, parent, UI_WIDGET_LABEL, rect, false);
        return lbl;
}

static struct ui_widget *_build_desc(struct ui_context *ui, struct ui_widget *parent,
                                const struct ui_desc *desc)
{
        struct ui_widget *w;

        switch(desc->type) {
        case UI_WIDGET_WINDOW:; {
                struct ui_window *win = light_ui_window_create(ui, parent, desc->rect, desc->text);
                // before the children are created, not after: set_corner_radius() re-lays-out,
                // so applying it later would lay the same stack out twice, and the first of
                // those passes would divide a content area that had not yet been shrunk to
                // clear the curve
                if(desc->corner_radius)
                        light_ui_window_set_corner_radius(win, desc->corner_radius);
                w = &win->widget;
                break;
        }
        case UI_WIDGET_BUTTON:; {
                struct ui_button *btn = light_ui_button_create(ui, parent, desc->rect, desc->text,
                                                                desc->on_press, desc->user_data);
                w = &btn->widget;
                break;
        }
        case UI_WIDGET_LABEL:; {
                struct ui_label *lbl = light_ui_label_create(ui, parent, desc->rect, desc->text);
                w = &lbl->widget;
                break;
        }
        default:
                light_error("ui descriptor names unknown widget type 0x%x", desc->type);
                return NULL;
        }

        // every widget struct embeds its ui_widget FIRST, so this is the widget's own address
        // and the caller's typed pointer needs no adjustment -- guaranteed by C, not by luck
        if(desc->bind)
                *desc->bind = w;

        for(uint8_t i = 0; i < desc->child_count; i++)
                _build_desc(ui, w, desc->children[i]);

        // after the children, since a stack divides the content area between them. a
        // descriptor that names no layout leaves the window at UI_LAYOUT_NONE, which is what
        // keeps hand-placed rects where they were put
        if(desc->type == UI_WIDGET_WINDOW && desc->layout == UI_LAYOUT_STACK)
                light_ui_window_layout_stack(to_ui_window(w), desc->layout_gap);

        return w;
}

struct ui_widget *light_ui_build(struct ui_context *ui, struct ui_widget *parent,
                                const struct ui_desc *desc)
{
        struct ui_widget *w = _build_desc(ui, parent, desc);

        //   a root descriptor carries no rect -- it cannot, since the canvas size is a runtime
        // fact and the descriptor is a compile-time constant -- so the tree is sized here,
        // against the canvas as it actually is. this is also what makes one descriptor work
        // unchanged on a 64x128 OLED and a 240x280 panel.
        //   it has to happen HERE rather than being left to the caller. the obvious candidate,
        // light_ui_set_safe_inset(), returns early when the inset is unchanged, and an inset of
        // 0 (every square-cornered rig) matches the value a fresh context already holds -- so
        // relying on it would leave the root at its zeroed rect on exactly the boards that
        // never set one, and the symptom would be a blank panel rather than any error
        if(!parent && w)
                light_ui_relayout(ui);

        return w;
}

// --- teardown and navigation ---

static void _widget_destroy_subtree(struct ui_widget *w)
{
        //   children first: each is freed before the parent that names it, so no pointer is
        // followed after the memory behind it has gone
        struct ui_widget *child = w->first_child;
        while(child) {
                struct ui_widget *next = child->next_sibling;
                _widget_destroy_subtree(child);
                child = next;
        }
        //   focus is cleared here rather than by the caller: it can point at any widget in the
        // subtree, not just its root, and this is the one walk that visits all of them
        if(w->ui->focused == w)
                w->ui->focused = NULL;
        light_free(w);
}

void light_ui_widget_destroy(struct ui_widget *w)
{
        if(!w)
                return;

        struct ui_context *ui = w->ui;

        //   unlinked BEFORE anything is freed, so the tree is never left holding a pointer to
        // released memory even momentarily
        if(!w->parent) {
                if(ui->root == w)
                        ui->root = NULL;
        } else {
                struct ui_widget **slot = &w->parent->first_child;
                while(*slot && *slot != w)
                        slot = &(*slot)->next_sibling;
                if(*slot)
                        *slot = w->next_sibling;
        }
        _widget_destroy_subtree(w);

        //   whatever the subtree occupied has to be repainted; the widget that owned that area
        // no longer exists to invalidate it
        light_ui_invalidate(ui);
}

//   captures the frame on screen and sets the outgoing image sliding, so the incoming page is
// revealed rather than replacing it between one frame and the next. `back` picks which way it
// leaves: forward pushes it toward logical -x so the new page arrives from the right, and a
// return sends it the other way, which is what makes the two directions distinguishable
static void _begin_page_move(struct ui_context *ui, bool back)
{
        struct rend_context *render = _ui_render(ui);

        //   nothing to slide before the first page exists, and nowhere to hold the image
        // without a second buffer. A rotation in progress already owns both the back buffer
        // and the frame, so a transfer during one simply snaps -- a correct change beats two
        // animations fighting over the same pixels
        if(!ui->root || !render->buffer_back || ui->rotating)
                return;

        //   the direction is chosen in LOGICAL terms and converted here, because the blit
        // works in physical buffer space. transform.a and .c are the physical components of
        // logical +x, which for a pure rotation makes exactly one of them non-zero -- so this
        // picks the physical axis the viewer would call horizontal, whatever the board's
        // orientation. Getting this wrong would slide the page sideways in portrait and
        // vertically in landscape, the same class of mistake as classifying a swipe by its
        // panel-frame direction
        const rend_transform_t *m = &render->transform;
        int8_t ux = (m->a > 0) - (m->a < 0);
        int8_t uy = (m->c > 0) - (m->c < 0);
        int8_t sign = back ? 1 : -1;

        memcpy(render->buffer_back, render->buffer, render->buffer_length);
        light_canvas_set_double_buffer(ui->canvas, false);

        ui->page_moving = true;
        ui->page_move_dx = (int8_t)(sign * ux);
        ui->page_move_dy = (int8_t)(sign * uy);
        ui->page_move_span = ux ? render->phys_dim_x : render->phys_dim_y;
        ui->page_move_start_ms = light_platform_get_time_since_init();
        ui->page_move_duration_ms = LIGHT_UI_PAGE_MOVE_MS;
}

static void _show_page(struct ui_context *ui, const struct ui_page *page,
                        const struct ui_page *return_page, bool back)
{
        if(!page || !page->content) {
                light_error("cannot navigate to a page with no content");
                return;
        }
        // before the old tree is destroyed: the image being captured is the one it drew
        _begin_page_move(ui, back);
        //   the old tree goes before the new one is built. Without this the context would
        // simply drop its root pointer and leak every widget under it, which is what happened
        // to anything that built a second root
        if(ui->root)
                light_ui_widget_destroy(ui->root);

        ui->page = page;
        ui->return_page = return_page;

        // builds under the (now empty) root and relayouts against the canvas
        light_ui_build(ui, NULL, page->content);
        light_ui_invalidate(ui);
}

void light_ui_navigate(struct ui_context *ui, const struct ui_page *page)
{
        // no return override: back from here follows the page's own parent
        _show_page(ui, page, NULL, false);
}

void light_ui_navigate_returning(struct ui_context *ui, const struct ui_page *page,
                                const struct ui_page *return_page)
{
        _show_page(ui, page, return_page, false);
}

bool light_ui_navigate_back(struct ui_context *ui)
{
        if(!ui->page)
                return false;

        //   the override wins over the structural parent, and only for this one page --
        // _show_page() clears it on the way out, so the page we return TO goes back to its own
        // parent rather than inheriting this address
        const struct ui_page *target = ui->return_page ? ui->return_page : ui->page->parent;
        if(!target)
                return false;

        _show_page(ui, target, NULL, true);
        return true;
}

// next widget in depth-first pre-order, which is both paint order and focus order. returns
// NULL once the walk has left the subtree rooted at `root`
static struct ui_widget *_tree_next(struct ui_widget *w, struct ui_widget *root)
{
        if(w->first_child)
                return w->first_child;
        while(w) {
                if(w == root)
                        return NULL;
                if(w->next_sibling)
                        return w->next_sibling;
                w = w->parent;
        }
        return NULL;
}

void light_ui_window_layout_stack(struct ui_window *win, uint8_t gap)
{
        // recorded before it is applied, so light_ui_relayout() can re-run exactly this
        // arrangement later without the application being asked to do it again
        win->layout = UI_LAYOUT_STACK;
        win->layout_gap = gap;

        struct ui_rect content = win->widget.rect;
        // the frame itself occupies the outermost pixel ring, so content starts inside it.
        // rows span the full width, so a rounded corner has to be cleared vertically for them
        // -- but only as far as the arc actually reaches in at inset_x, not by the whole
        // radius. at radius 40 with a 3px inset that is 25 rows rather than 40, and the 30px
        // it gives back over the two ends is a ninth of a 280px panel
        int16_t inset_x = _ui_window_inset_x(win);
        int16_t drop = _ui_corner_drop(win, inset_x);
        int16_t inset_y = drop > inset_x ? drop : inset_x;
        content.x0 += inset_x;
        content.y0 += inset_y;
        content.x1 -= inset_x;

        // the BOTTOM is different: rather than stopping short of the curve, the last row runs
        // all the way down and takes the container's curve as its own bottom corners. the
        // arithmetic works because insetting a rounded rect by inset_x leaves a rounded rect
        // of radius (R - inset_x) about the SAME arc centres, so a row whose bottom edge is at
        // y1 - inset_x and whose bottom corners have that radius traces the container exactly.
        //
        // it only works if the row is at least that tall, though. a shorter row's straight
        // left edge would begin inside the arc and poke out of the container, so the flush
        // treatment is offered and then withdrawn below if the rows come out too short
        int16_t flush_r = (int16_t)win->corner_radius - inset_x;
        if(flush_r < 0)
                flush_r = 0;
        content.y1 -= flush_r > 0 ? inset_x : inset_y;

        // a titled window loses the title row plus its separator line to the header. the
        // header sits at the TOP of the frame, above where the corner drop puts content, so
        // this is a lower bound on content.y0 rather than something added to it -- adding
        // would push content down twice over for the same corner
        const rend_font_t *font = _ui_render(win->widget.ui)->font;
        if(win->title && font) {
                int16_t header_bottom = (int16_t)(win->widget.rect.y0 + (win->border ? 1 : 0)
                                + font->char_height + 2);
                if(header_bottom > content.y0)
                        content.y0 = header_bottom;
        }

        uint16_t count = 0;
        for(struct ui_widget *c = win->widget.first_child; c; c = c->next_sibling) {
                if(c->visible)
                        count++;
        }
        if(!count || _ui_rect_empty(&content))
                return;

        int32_t total_h = (int32_t)content.y1 - content.y0 + 1;
        // gaps sit BETWEEN rows, so there are count-1 of them
        int32_t row_h = (total_h - (int32_t)gap * (count - 1)) / count;
        if(row_h < 1) {
                light_warn("window content (%d px) too short for %d stacked rows",
                                (int)total_h, count);
                row_h = 1;
        }
        // the withdrawal promised above: rows too short to contain the container's curve pull
        // the bottom back to the ordinary drop and lay out again, rather than drawing a row
        // that hangs outside the frame
        if(flush_r > 0 && row_h < flush_r) {
                content.y1 -= (int16_t)(inset_y - inset_x);
                flush_r = 0;
                total_h = (int32_t)content.y1 - content.y0 + 1;
                row_h = (total_h - (int32_t)gap * (count - 1)) / count;
                if(row_h < 1)
                        row_h = 1;
        }

        int16_t y = content.y0;
        uint16_t index = 0;
        for(struct ui_widget *c = win->widget.first_child; c; c = c->next_sibling) {
                if(!c->visible)
                        continue;
                c->rect.x0 = content.x0;
                c->rect.x1 = content.x1;
                c->rect.y0 = y;
                // the last row absorbs the remainder of the integer division as well as
                // reaching the bottom edge -- otherwise up to count-1 pixels of the container
                // would show through below a row that is supposed to be flush with it
                c->rect.y1 = (++index == count) ? content.y1 : (int16_t)(y + row_h - 1);
                if(c->type == UI_WIDGET_BUTTON) {
                        struct ui_button *btn = to_ui_button(c);
                        bool last = (index == count);
                        btn->corner_radius = (last && flush_r > 0) ? (uint8_t)flush_r : 0;
                        // only the two corners that actually touch the container curve; the
                        // top edge is shared with the row above and stays square
                        btn->corners = (last && flush_r > 0) ? REND_CORNER_BOTTOM : REND_CORNER_NONE;
                }
                y = (int16_t)(y + row_h + gap);
        }
        light_ui_invalidate_widget(&win->widget);
}

void light_ui_window_set_corner_radius(struct ui_window *win, uint8_t radius)
{
        if(win->corner_radius == radius)
                return;
        win->corner_radius = radius;
        // the content area just changed height, so any recorded arrangement has to be
        // re-applied against it -- a window whose children were hand-placed is left alone,
        // on the same terms as light_ui_relayout()
        if(win->layout == UI_LAYOUT_STACK)
                light_ui_window_layout_stack(win, win->layout_gap);
        light_ui_invalidate_widget(&win->widget);
}

void light_ui_set_safe_inset(struct ui_context *ui, uint8_t inset)
{
        if(ui->safe_inset == inset)
                return;
        ui->safe_inset = inset;
        light_ui_relayout(ui);
        light_ui_invalidate(ui);
}

void light_ui_relayout(struct ui_context *ui)
{
        if(!ui->root)
                return;

        // the root is resized to whatever the canvas is now -- which is the whole point,
        // since a rotation swaps dim_x and dim_y and leaves every absolute rect describing
        // a canvas that no longer exists. the safe inset comes off all four edges, so a
        // panel that doesn't show its own corners never has content placed in them
        const struct rend_context *render = _ui_render(ui);
        int16_t inset = (int16_t)ui->safe_inset;
        ui->root->rect = (struct ui_rect) {
                inset, inset,
                (int16_t)render->dim_x - 1 - inset, (int16_t)render->dim_y - 1 - inset
        };

        // pre-order, so a window is resized by its parent's layout before it lays out its
        // own children against that new rect -- the other order would have every nested
        // window arranging itself inside its previous, stale bounds
        for(struct ui_widget *w = ui->root; w; w = _tree_next(w, ui->root)) {
                if(w->type != UI_WIDGET_WINDOW)
                        continue;
                struct ui_window *win = to_ui_window(w);
                if(win->layout == UI_LAYOUT_STACK)
                        light_ui_window_layout_stack(win, win->layout_gap);
        }
}

// applies a rotation for real: swaps the canvas dimensions, drops regions measured against
// the old ones, re-lays-out and repaints everything
static void _commit_rotation(struct ui_context *ui, uint8_t rotation)
{
        struct rend_context *render = _ui_render(ui);

        rend_context_set_rotation(render, rotation);
        // regions accumulated before this point were measured against a canvas whose
        // dimensions have just swapped, so they describe nothing meaningful now -- drop them
        // rather than let them be carried into the next push
        light_canvas_reset_regions(ui->canvas);
        light_ui_relayout(ui);
        // everything moved, and the panel still shows the old arrangement in the old
        // orientation -- nothing short of the whole canvas is a safe region to push
        light_ui_invalidate(ui);
        light_debug("ui rotation now %d, canvas %dx%d",
                        rotation, render->dim_x, render->dim_y);
}

// the shortest signed turn between two REND_ROTATE_* quadrants, in degrees: -90, 0, +90 or
// 180. going the long way round would animate three quarters of a turn to reach a
// neighbouring orientation
static int16_t _rotation_delta_degrees(uint8_t from, uint8_t to)
{
        int16_t quadrants = (int16_t)((to + 4 - from) % 4);
        if(quadrants == 3)
                quadrants = -1;
        return (int16_t)(quadrants * 90);
}

void light_ui_set_rotation(struct ui_context *ui, uint8_t rotation)
{
        struct rend_context *render = _ui_render(ui);
        // compared against the target rather than the live rotation: mid-animation the live
        // one is still the OLD value, so without this a repeated orientation report would
        // restart the turn on every tick
        if(ui->rotate_target == rotation && (ui->rotating || render->rotation == rotation))
                return;

        // the animation reads the pre-rotation image out of the back buffer while drawing
        // into the front one. with no second buffer there is nowhere to hold it, so fall
        // back to what this used to do -- a correct snap beats a broken animation
        if(!render->buffer_back) {
                _commit_rotation(ui, rotation);
                ui->rotate_target = rotation;
                return;
        }

        // one copy of the current frame into the back buffer, then swapping is suspended so
        // it stays put for the duration. a memcpy of a frame costs a fraction of the
        // transfer that pushes it, and it avoids having to reason about swap ordering
        // against an update that may still be in flight
        //   a transition in flight keeps the frame; this rotation waits for it. Both animations
        // want the back buffer and the frame, so they cannot run together -- and of the two
        // ways to resolve that, deferring is the one that loses nothing: the transition is seen
        // through, and the board still ends up the right way up a fraction of a second later.
        //
        //   the target is recorded rather than the rotation being restarted from scratch, so a
        // board turned twice mid-transition settles on where it actually ended up
        if(ui->page_moving) {
                ui->rotate_deferred = true;
                ui->rotate_deferred_target = rotation;
                light_debug("rotation to %d deferred until the page transition finishes", rotation);
                return;
        }

        memcpy(render->buffer_back, render->buffer, render->buffer_length);
        light_canvas_set_double_buffer(ui->canvas, false);

        ui->rotating = true;
        ui->rotate_target = rotation;
        ui->rotate_degrees = _rotation_delta_degrees(render->rotation, rotation);
        ui->rotate_start_ms = light_platform_get_time_since_init();
        ui->dirty = true;
        light_debug("ui rotating %d degrees to %d", ui->rotate_degrees, rotation);
}

// --- invalidation ---

void light_ui_invalidate_widget(struct ui_widget *w)
{
        light_canvas_invalidate_rect(w->ui->canvas, (struct canvas_region) {
                w->rect.x0, w->rect.y0, w->rect.x1, w->rect.y1 });
        w->ui->dirty = true;
}

void light_ui_invalidate(struct ui_context *ui)
{
        light_canvas_invalidate_all(ui->canvas);
        ui->dirty = true;
}

// --- focus and input ---

static bool _is_focusable(const struct ui_widget *w)
{
        return w->focusable && w->visible && w->enabled;
}

// walks the tree once, collecting everything needed to answer "the focusable before/after
// the focused one" including both wrap cases. a single pass rather than two directional
// walks, since a backward pre-order traversal has no cheap formulation
static struct ui_widget *_focus_relative(struct ui_context *ui, bool forward)
{
        struct ui_widget *first = NULL, *last = NULL;
        struct ui_widget *before = NULL, *after = NULL;
        bool seen = false;

        for(struct ui_widget *w = ui->root; w; w = _tree_next(w, ui->root)) {
                if(!_is_focusable(w))
                        continue;
                if(!first)
                        first = w;
                if(seen && !after)
                        after = w;
                if(w == ui->focused) {
                        seen = true;
                        before = last;
                }
                last = w;
        }

        if(!first)
                return NULL;
        // no current focus, or the focused widget has since been hidden/disabled out of the
        // cycle -- either way, start over from the top rather than getting stuck
        if(!seen)
                return first;
        if(forward)
                return after ? after : first;
        return before ? before : last;
}

void light_ui_set_focus(struct ui_context *ui, struct ui_widget *w)
{
        if(ui->focused == w)
                return;
        // both the widget losing the highlight and the one gaining it change appearance, so
        // both have to reach the panel
        if(ui->focused)
                light_ui_invalidate_widget(ui->focused);
        ui->focused = w;
        if(w)
                light_ui_invalidate_widget(w);
}

void light_ui_input_focus_next(struct ui_context *ui)
{
        light_ui_set_focus(ui, _focus_relative(ui, true));
}
void light_ui_input_focus_prev(struct ui_context *ui)
{
        light_ui_set_focus(ui, _focus_relative(ui, false));
}

static void _activate(struct ui_widget *w)
{
        if(!w || w->type != UI_WIDGET_BUTTON)
                return;
        struct ui_button *btn = to_ui_button(w);
        light_debug("button '%s' activated", btn->label ? (const char *)btn->label : "(unlabelled)");
        if(btn->on_press)
                btn->on_press(btn, btn->user_data);
}

void light_ui_input_activate(struct ui_context *ui)
{
        if(ui->focused && _is_focusable(ui->focused))
                _activate(ui->focused);
}

uint8_t light_ui_swipe_direction(struct ui_context *ui, uint16_t start_x, uint16_t start_y,
                                uint16_t end_x, uint16_t end_y)
{
        //   discarded while the interface is turning, for the same reason a tap is (see
        // light_ui_input_press_at): the frame being shown is the pre-rotation image mid-turn,
        // so the axes the user swiped along are not the ones this would resolve against
        if(ui->rotating)
                return UI_SWIPE_NONE;

        //   both endpoints go through the same untransform a tap does, rather than the
        // direction being rotated by a table of its own. There is then only one place that
        // knows how panel coordinates relate to logical ones, so the two cannot drift apart --
        // and a swipe is classified in exactly the frame the user made it in
        rend_point2d a = rend_untransform_point(_ui_render(ui),
                                        (rend_point2d) { start_x, start_y });
        rend_point2d b = rend_untransform_point(_ui_render(ui),
                                        (rend_point2d) { end_x, end_y });

        int32_t dx = (int32_t)b.x - (int32_t)a.x;
        int32_t dy = (int32_t)b.y - (int32_t)a.y;
        int32_t adx = dx < 0 ? -dx : dx;
        int32_t ady = dy < 0 ? -dy : dy;

        //   no movement in either axis. rend_untransform_point() clamps to the canvas, so this
        // also covers a swipe whose ends both landed off the same edge -- a real possibility
        // where the touch panel's coordinate range is wider than the display's
        if(!adx && !ady)
                return UI_SWIPE_NONE;

        // the dominant axis decides, so a swipe that drifts diagonally still reads as the one
        // the user meant. ties go to horizontal, arbitrarily but consistently
        if(adx >= ady)
                return dx > 0 ? UI_SWIPE_RIGHT : UI_SWIPE_LEFT;
        return dy > 0 ? UI_SWIPE_DOWN : UI_SWIPE_UP;
}

bool light_ui_input_press_at(struct ui_context *ui, uint16_t x, uint16_t y)
{
        //   silently ignored while the interface is turning. Mid-rotation the panel is showing
        // the pre-rotation image being animated, while the widget tree is still laid out for
        // the OLD rotation and the transform has not been committed to the new one -- so a tap
        // would be resolved against a layout that matches neither what is on screen nor where
        // the interface is about to settle. There is no correct answer to give, and acting on
        // a wrong one presses a button the user could not see
        if(ui->rotating)
                return false;

        // the caller hands us the panel's own coordinates; widgets live in logical space,
        // and under rotation those are different points. doing this here rather than in
        // every application is the whole reason light_ui owns the render context
        rend_point2d logical = rend_untransform_point(_ui_render(ui), (rend_point2d) { x, y });
        x = logical.x;
        y = logical.y;

        struct ui_widget *hit = NULL;
        // last match wins: pre-order means deeper and later-drawn widgets are visited last,
        // and those are the ones actually on top where they overlap
        for(struct ui_widget *w = ui->root; w; w = _tree_next(w, ui->root)) {
                if(!_is_focusable(w))
                        continue;
                if(_ui_rect_contains(&w->rect, (int16_t)x, (int16_t)y))
                        hit = w;
        }
        if(!hit)
                return false;

        // a touch is a complete interaction, not a focus move: move the highlight so the
        // two input paths leave the UI in the same state, then fire
        light_ui_set_focus(ui, hit);
        _activate(hit);
        return true;
}

// --- mutators ---

void light_ui_widget_set_visible(struct ui_widget *w, bool visible)
{
        if(w->visible == visible)
                return;
        // invalidated before the change as well as after, so the area a widget is
        // disappearing FROM is repainted -- its rect is what needs covering, and that is
        // the same rect either way, but this keeps the rule uniform for future widgets
        // whose rect could change with visibility
        light_ui_invalidate_widget(w);
        w->visible = visible;
        light_ui_invalidate_widget(w);
        if(!visible && w->ui->focused == w)
                light_ui_input_focus_next(w->ui);
}
void light_ui_widget_set_enabled(struct ui_widget *w, bool enabled)
{
        if(w->enabled == enabled)
                return;
        w->enabled = enabled;
        light_ui_invalidate_widget(w);
        if(!enabled && w->ui->focused == w)
                light_ui_input_focus_next(w->ui);
}
void light_ui_button_set_label(struct ui_button *btn, const uint8_t *label)
{
        btn->label = label;
        light_ui_invalidate_widget(&btn->widget);
}
void light_ui_label_set_text(struct ui_label *lbl, const uint8_t *text)
{
        lbl->text = text;
        light_ui_invalidate_widget(&lbl->widget);
}

// --- render ---

// draws one step of the rotation animation. returns true once the turn has finished and the
// real rotation has been applied, so the caller knows an ordinary repaint is due
static bool _render_rotation_step(struct ui_context *ui)
{
        struct rend_context *render = _ui_render(ui);
        uint32_t elapsed = light_platform_get_time_since_init() - ui->rotate_start_ms;
        bool final = elapsed >= ui->rotate_duration_ms;

        if(!final) {
                if(!light_canvas_frame_begin(ui->canvas))
                        return false;

                // linear in time. an eased curve would look nicer, but at roughly ten frames
                // for the whole turn the difference is below what the eye can pick out, and
                // linear keeps the angle trivially predictable when checking it on hardware
                int16_t angle = (int16_t)(((int32_t)ui->rotate_degrees * (int32_t)elapsed)
                                / (int32_t)ui->rotate_duration_ms);
                // shrink to whatever still fits: a rectangle turned off-axis needs a bigger
                // box than the one it came from, so at 1:1 the corners would be sliced off
                // for most of the turn
                rend_blit_rotated(render, render->buffer_back,
                                angle, rend_scale_inscribed(render, angle));

                light_canvas_invalidate_all(ui->canvas);
                light_canvas_frame_end(ui->canvas);
                return false;
        }

        // the turn is over: put swapping back before committing, so the repaint that follows
        // runs against a normal double-buffered canvas again
        light_canvas_set_double_buffer(ui->canvas, true);
        ui->rotating = false;
        _commit_rotation(ui, ui->rotate_target);
        return true;
}

//   draws one step of a page transition. returns true once it has finished, so the caller knows
// an ordinary repaint is due -- the same contract as _render_rotation_step()
static bool _render_page_step(struct ui_context *ui)
{
        struct rend_context *render = _ui_render(ui);
        uint32_t elapsed = light_platform_get_time_since_init() - ui->page_move_start_ms;

        if(elapsed >= ui->page_move_duration_ms) {
                // swapping goes back on before the repaint that follows, so it runs against a
                // normally double-buffered canvas again
                light_canvas_set_double_buffer(ui->canvas, true);
                ui->page_moving = false;
                light_ui_invalidate(ui);

                //   a rotation that arrived mid-transition runs now. Cleared BEFORE the call,
                // not after: light_ui_set_rotation() checks page_moving, which is already
                // false, so it proceeds -- and leaving the flag set would re-arm the same
                // rotation the next time any transition ended
                if(ui->rotate_deferred) {
                        uint8_t target = ui->rotate_deferred_target;
                        ui->rotate_deferred = false;
                        light_ui_set_rotation(ui, target);
                }
                return true;
        }
        if(!light_canvas_frame_begin(ui->canvas))
                return false;

        //   the incoming page first, as an ordinary repaint of the live tree, then the
        // outgoing image over the top of it. rend_blit_offset() leaves the band it no longer
        // covers untouched, so what shows through there is the new page already drawn beneath
        if(ui->root)
                _ui_paint_widget(ui, ui->root);

        // linear, for the same reason the rotation is: at this duration the frames are few
        // enough that an eased curve is below what the eye resolves
        int32_t travel = ((int32_t)ui->page_move_span * (int32_t)elapsed)
                        / (int32_t)ui->page_move_duration_ms;
        rend_blit_offset(render, render->buffer_back,
                        (int32_t)ui->page_move_dx * travel, (int32_t)ui->page_move_dy * travel);

        light_canvas_invalidate_all(ui->canvas);
        light_canvas_frame_end(ui->canvas);
        return false;
}

void light_ui_render(struct ui_context *ui)
{
        //   the two animations are mutually exclusive by construction, from both ends:
        // _begin_page_move() declines while a rotation runs, and light_ui_set_rotation()
        // defers while a transition runs. So the order of these two blocks decides nothing --
        // it is written transition-first only because that is the one that hands a rotation
        // on when it finishes
        if(ui->page_moving) {
                if(!_render_page_step(ui))
                        return;
                // fell through on the final step, which restored swapping and marked
                // everything dirty; draw the settled page below rather than waiting a tick
        }

        if(ui->rotating) {
                // the animation owns the frame while it runs -- the widget tree is not drawn
                // at all, only the image of it captured before the turn began
                if(!_render_rotation_step(ui))
                        return;
                // fell through on the final step, which committed the rotation and marked
                // everything dirty; draw the real layout below rather than waiting a tick
        }

        if(!ui->dirty || !ui->root)
                return;
        // the canvas decides whether a frame happens at all: it owns the frame deadline and
        // the check for a display still reading the buffer. on false the dirty flag and the
        // accumulated regions both survive, so the repaint simply happens on a later tick
        if(!light_canvas_frame_begin(ui->canvas))
                return;

        _ui_paint_widget(ui, ui->root);

        light_canvas_frame_end(ui->canvas);
        ui->dirty = false;
}
