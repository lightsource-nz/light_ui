#include <light_ui.h>

#include "light_ui_internal.h"

void light_ui_init()
{
        // nothing global to set up: a ui_context owns everything, and an application may
        // create more than one (e.g. one per display). kept for symmetry with every other
        // module's LF_EVENT_MODULE_LOAD hook, and as the place per-module state would go
        light_trace("");
}

void _ui_rect_union(struct ui_rect *box, const struct ui_rect *add)
{
        if(_ui_rect_empty(add))
                return;
        if(_ui_rect_empty(box)) {
                *box = *add;
                return;
        }
        if(add->x0 < box->x0) box->x0 = add->x0;
        if(add->y0 < box->y0) box->y0 = add->y0;
        if(add->x1 > box->x1) box->x1 = add->x1;
        if(add->y1 > box->y1) box->y1 = add->y1;
}

bool _ui_clip_to_canvas(const struct ui_context *ui, struct ui_rect *r)
{
        int16_t max_x = (int16_t)ui->render->dim_x - 1;
        int16_t max_y = (int16_t)ui->render->dim_y - 1;

        if(r->x0 < 0) r->x0 = 0;
        if(r->y0 < 0) r->y0 = 0;
        if(r->x1 > max_x) r->x1 = max_x;
        if(r->y1 > max_y) r->y1 = max_y;
        return !_ui_rect_empty(r);
}

// an empty box in the "nothing accumulated yet" sense -- _ui_rect_union() replaces one of
// these outright rather than growing it
static const struct ui_rect _rect_none = { .x0 = 1, .y0 = 1, .x1 = 0, .y1 = 0 };

struct ui_context *light_ui_create_context(struct rend_context *render,
                                        struct display_device **display, uint8_t display_count)
{
        struct ui_context *ui = light_alloc(sizeof(struct ui_context));
        ui->render = render;
        ui->display = display;
        ui->display_count = display_count;
        ui->root = NULL;
        ui->focused = NULL;
        ui->dirty = false;
        ui->dirty_box = _rect_none;
        if(!render->font)
                light_warn("render context '%s' has no font -- widget labels will not render",
                                render->name);
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
        struct ui_rect content = win->widget.rect;
        // the frame itself occupies the outermost pixel ring, so content starts inside it
        int16_t inset = (int16_t)win->padding + (win->border ? 1 : 0);
        content.x0 += inset;
        content.y0 += inset;
        content.x1 -= inset;
        content.y1 -= inset;

        // a titled window loses the title row plus its separator line to the header
        if(win->title && win->widget.ui->render->font)
                content.y0 += win->widget.ui->render->font->char_height + 2;

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

        int16_t y = content.y0;
        for(struct ui_widget *c = win->widget.first_child; c; c = c->next_sibling) {
                if(!c->visible)
                        continue;
                c->rect.x0 = content.x0;
                c->rect.x1 = content.x1;
                c->rect.y0 = y;
                c->rect.y1 = (int16_t)(y + row_h - 1);
                y = (int16_t)(y + row_h + gap);
        }
        light_ui_invalidate_widget(&win->widget);
}

// --- invalidation ---

void light_ui_invalidate_widget(struct ui_widget *w)
{
        _ui_rect_union(&w->ui->dirty_box, &w->rect);
        w->ui->dirty = true;
}

void light_ui_invalidate(struct ui_context *ui)
{
        ui->dirty_box = (struct ui_rect) {
                0, 0, (int16_t)ui->render->dim_x - 1, (int16_t)ui->render->dim_y - 1
        };
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

bool light_ui_input_press_at(struct ui_context *ui, uint16_t x, uint16_t y)
{
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

static void _push_region(struct ui_context *ui, struct ui_rect r)
{
        if(!_ui_clip_to_canvas(ui, &r))
                return;
        for(uint8_t i = 0; i < ui->display_count; i++) {
                if(!ui->display[i])
                        continue;
                light_display_command_update_region_async(ui->display[i],
                        (rend_point2d) { (uint16_t)r.x0, (uint16_t)r.y0 },
                        (rend_point2d) { (uint16_t)r.x1, (uint16_t)r.y1 });
        }
}

void light_ui_render(struct ui_context *ui)
{
        if(!ui->dirty || !ui->root)
                return;
        // the buffer about to be swapped into is the one a previous update may still be
        // reading. skip this frame rather than draw over an in-flight DMA transfer -- the
        // dirty flag survives, so the repaint simply happens on the next call
        if(light_display_render_context_busy(ui->render))
                return;

        rend_context_swap_buffers(ui->render);
        rend_draw_clear(ui->render);
        _ui_paint_widget(ui, ui->root);

        _push_region(ui, ui->dirty_box);
        ui->dirty_box = _rect_none;
        ui->dirty = false;
}
