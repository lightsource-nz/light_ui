#ifndef _LIGHT_CANVAS_INTERNAL_H
#define _LIGHT_CANVAS_INTERNAL_H

#include <light_canvas.h>

// --- region arithmetic, shared between canvas.c and canvas_region.c ---

// clamps a region to the render context's logical canvas. returns false when nothing of it
// survives, which is the caller's cue to drop it entirely
extern bool _canvas_region_clip(const struct canvas_context *ctx, struct canvas_region *r);
// grows *box to cover *add
extern void _canvas_region_union(struct canvas_region *box, const struct canvas_region *add);
// do these two share any pixel? touching-but-not-overlapping counts, since merging two
// abutting regions costs nothing and pushing them separately costs an extra transfer
extern bool _canvas_region_overlaps(const struct canvas_region *a, const struct canvas_region *b);
// adds r to the list, merging it into any region it already overlaps (and re-merging
// transitively, since one addition can bridge two previously disjoint regions). returns
// false if the list is full and the caller should collapse to a single full-canvas region
extern bool _canvas_region_add(struct canvas_region *list, uint8_t *count, uint8_t capacity,
                                struct canvas_region r);

static inline bool _canvas_region_empty(const struct canvas_region *r)
{
        return r->x1 < r->x0 || r->y1 < r->y0;
}

#endif
