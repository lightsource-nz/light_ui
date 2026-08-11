#ifndef _LIGHT_UI_INTERNAL_H
#define _LIGHT_UI_INTERNAL_H

#include <light_ui.h>

// --- shared between ui.c and ui_draw.c ---

// paints one widget and, recursively, its children -- parents first, so a window's frame
// is drawn under the buttons sitting inside it
extern void _ui_paint_widget(struct ui_context *ui, struct ui_widget *w);

// clamps a rect to the render context's logical canvas. returns false when nothing of it
// survives, which is the caller's cue to skip the widget entirely
extern bool _ui_clip_to_canvas(const struct ui_context *ui, struct ui_rect *r);

// the render context light_ui draws through. reached via the canvas rather than held
// directly, so there is one owner of it -- this exists only to keep the chain from being
// spelled out at every use site in ui_draw.c
static inline struct rend_context *_ui_render(const struct ui_context *ui)
{
        return ui->canvas->render;
}

// a window's inset from its own rect, per axis.
//
// the two differ because the clearance a rounded corner demands is not symmetric: an arc of
// radius r eats into the leftmost columns only for the r rows nearest the top and bottom
// edges, and below that the left edge is back at x0. keeping content out of the r rows at
// each END is therefore enough to let the FULL width be used in between -- insetting
// horizontally by the radius too would give back exactly the width a rounded frame exists to
// recover.
//
// the vertical one takes the caller's own base inset rather than computing one, because the
// two call sites legitimately differ: the title hugs the frame (border only) while content
// also clears the padding. what they must not differ on is the corner clearance, which is why
// that part lives here and not at either site.
//
// light_ui_window_layout_stack() and _paint_window()'s title placement both go through these.
// they already had to agree on the header's height; where the header starts is a second thing
// they cannot be allowed to disagree about
static inline int16_t _ui_window_inset_x(const struct ui_window *win)
{
        return (int16_t)win->padding + (win->border ? 1 : 0);
}
static inline int16_t _ui_window_inset_y(const struct ui_window *win, int16_t base)
{
        return win->corner_radius > base ? (int16_t)win->corner_radius : base;
}

static inline bool _ui_rect_empty(const struct ui_rect *r)
{
        return r->x1 < r->x0 || r->y1 < r->y0;
}
static inline bool _ui_rect_contains(const struct ui_rect *r, int16_t x, int16_t y)
{
        return x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1;
}

#endif
