#include <light_canvas.h>

#include "light_canvas_internal.h"

bool _canvas_region_clip(const struct canvas_context *ctx, struct canvas_region *r)
{
        int16_t max_x = (int16_t)ctx->render->dim_x - 1;
        int16_t max_y = (int16_t)ctx->render->dim_y - 1;

        if(r->x0 < 0) r->x0 = 0;
        if(r->y0 < 0) r->y0 = 0;
        if(r->x1 > max_x) r->x1 = max_x;
        if(r->y1 > max_y) r->y1 = max_y;
        return !_canvas_region_empty(r);
}

void _canvas_region_union(struct canvas_region *box, const struct canvas_region *add)
{
        if(add->x0 < box->x0) box->x0 = add->x0;
        if(add->y0 < box->y0) box->y0 = add->y0;
        if(add->x1 > box->x1) box->x1 = add->x1;
        if(add->y1 > box->y1) box->y1 = add->y1;
}

bool _canvas_region_overlaps(const struct canvas_region *a, const struct canvas_region *b)
{
        // >= / <= rather than > / <: regions are INCLUSIVE, so a->x1 == b->x0 means they
        // share that column rather than merely abutting
        return a->x1 >= b->x0 && a->x0 <= b->x1
                        && a->y1 >= b->y0 && a->y0 <= b->y1;
}

bool _canvas_region_add(struct canvas_region *list, uint8_t *count, uint8_t capacity,
                        struct canvas_region r)
{
        // merge r into every region it touches. one addition can bridge two regions that
        // were disjoint from each other, so this restarts after each merge rather than
        // making a single pass -- otherwise the bridged pair would be left overlapping,
        // and frame_end() would push the shared pixels twice
        bool merged;
        do {
                merged = false;
                for(uint8_t i = 0; i < *count; i++) {
                        if(!_canvas_region_overlaps(&list[i], &r))
                                continue;
                        _canvas_region_union(&r, &list[i]);
                        // remove list[i] by pulling the tail down; r now covers it, and is
                        // appended once nothing else overlaps
                        for(uint8_t j = i; j + 1 < *count; j++)
                                list[j] = list[j + 1];
                        (*count)--;
                        merged = true;
                        break;
                }
        } while(merged);

        if(*count >= capacity)
                return false;

        list[(*count)++] = r;
        return true;
}
