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
static inline struct light_draw_context *_ui_render(const struct ui_context *ui)
{
        return ui->canvas->render;
}

static inline int16_t _ui_window_inset_x(const struct ui_window *win)
{
        return (int16_t)win->padding + (win->border ? 1 : 0);
}

// integer square root, for the corner arithmetic below. Newton's method, converging in a
// handful of iterations for anything a display can hold
static inline uint32_t _ui_isqrt(uint32_t n)
{
        if(n == 0)
                return 0;
        uint32_t x = n, y = (x + 1) / 2;
        while(y < x) {
                x = y;
                y = (x + n / x) / 2;
        }
        return x;
}

// --- clearing a rounded corner, which can be paid for in either axis ---
//
// there are two ways to keep something inside a corner arc, and which one is right depends on
// the shape of the thing. a short string can simply start further RIGHT; a full-width row
// cannot, so it has to start further DOWN. the two helpers below are the same circle solved
// for each axis, and using the wrong one is what makes a rounded frame either clip its
// content or waste a band the width of the radius.
//
// how far the arc still intrudes horizontally, `dy` rows above the arc's centre:
// x = r - sqrt(r^2 - dy^2). this is what lets a title sit at the very TOP of a rounded frame
// -- it is pushed sideways by exactly as much as the curve reaches in at its own topmost row,
// rather than the whole header being dropped below the curve entirely
static inline int16_t _ui_corner_indent(const struct ui_window *win, int16_t dy)
{
        int32_t r = win->corner_radius;
        if(r <= 0 || dy <= 0)
                return 0;
        if(dy >= r)
                return (int16_t)r;
        // isqrt truncates, so the indent comes out slightly LARGER than exact -- the error
        // is on the safe side, which is the direction to round in when the alternative is
        // text disappearing behind glass
        return (int16_t)(r - (int32_t)_ui_isqrt((uint32_t)(r * r - (int32_t)dy * dy)));
}
// the mirror, for content that must span the full width: given the horizontal inset such
// content sits at, how far down before the arc has come in that far.
// dy = sqrt(ix * (2r - ix)), the same circle solved for the vertical instead
static inline int16_t _ui_corner_drop(const struct ui_window *win, int16_t inset_x)
{
        int32_t r = win->corner_radius;
        int32_t ix = inset_x;
        if(r <= 0 || ix <= 0)
                return 0;
        if(ix >= r)
                return 0;
        // truncating again overestimates the drop, erring towards more clearance
        return (int16_t)(r - (int32_t)_ui_isqrt((uint32_t)(ix * (2 * r - ix))));
}

static inline bool _ui_rect_empty(const struct ui_rect *r)
{
        return r->x1 < r->x0 || r->y1 < r->y0;
}
static inline bool _ui_rect_contains(const struct ui_rect *r, int16_t x, int16_t y)
{
        return x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1;
}
// shrinks *r to its intersection with *clip. returns false when nothing survives, which is
// the caller's cue to skip whatever *r described
static inline bool _ui_rect_intersect(struct ui_rect *r, const struct ui_rect *clip)
{
        if(r->x0 < clip->x0) r->x0 = clip->x0;
        if(r->y0 < clip->y0) r->y0 = clip->y0;
        if(r->x1 > clip->x1) r->x1 = clip->x1;
        if(r->y1 > clip->y1) r->y1 = clip->y1;
        return !_ui_rect_empty(r);
}

//   a scrolling window's VIEWPORT: the area its content shows through, inside the border,
// padding, header band and corner clearance. This is what painting and hit-testing clip a
// scrolling window's children to, what the stack layout fills from, and what scroll offsets
// are clamped against -- one function so the four can never disagree about where content is
// allowed to be
extern void _ui_window_viewport(const struct ui_window *win, struct ui_rect *out);

#endif
