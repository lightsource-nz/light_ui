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

// grows *box to cover *add. an invalid (empty) box is replaced outright rather than grown,
// so callers can start from a zeroed struct
extern void _ui_rect_union(struct ui_rect *box, const struct ui_rect *add);

static inline bool _ui_rect_empty(const struct ui_rect *r)
{
        return r->x1 < r->x0 || r->y1 < r->y0;
}
static inline bool _ui_rect_contains(const struct ui_rect *r, int16_t x, int16_t y)
{
        return x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1;
}

#endif
