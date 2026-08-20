#include <light_canvas.h>
#include <light_platform.h>

#include "light_canvas_internal.h"

void light_canvas_init()
{
        // nothing global to set up: a canvas_context owns everything, and an application
        // may create more than one (e.g. one per display group). kept for symmetry with
        // every other module's LF_EVENT_MODULE_LOAD hook
        light_trace("");
}

struct canvas_context *light_canvas_create(struct light_draw_context *render,
                                        struct display_device **display, uint8_t display_count)
{
        struct canvas_context *ctx = light_alloc(sizeof(struct canvas_context));
        ctx->render = render;
        ctx->display = display;
        ctx->display_count = display_count;
        // honours a context the caller already double-buffered themselves, rather than
        // silently treating it as single-buffered and waiting out every update
        ctx->double_buffered = render->buffer_back != NULL;
        ctx->frame_interval_ms = 0;
        ctx->next_frame_ms = 0;
        ctx->frame_counter = 0;
        ctx->region_count = 0;
        ctx->carried_count = 0;
        ctx->frame_open = false;
        return ctx;
}

void light_canvas_enable_double_buffer(struct canvas_context *ctx)
{
        light_draw_context_enable_double_buffer(ctx->render);
        ctx->double_buffered = true;
}

void light_canvas_set_double_buffer(struct canvas_context *ctx, bool enable)
{
        if(enable && !ctx->render->buffer_back) {
                light_warn("cannot enable double buffering: render context '%s' has only one buffer",
                                ctx->render->name);
                return;
        }
        ctx->double_buffered = enable;
}

void light_canvas_set_frame_rate(struct canvas_context *ctx, uint32_t frame_rate)
{
        ctx->frame_interval_ms = frame_rate ? 1000 / frame_rate : 0;
}

bool light_canvas_frame_begin(struct canvas_context *ctx)
{
        if(ctx->frame_open) {
                light_warn("canvas frame already open -- missing light_canvas_frame_end()?");
                return false;
        }

        uint32_t now = light_platform_get_time_since_init();
        if(ctx->frame_interval_ms && now < ctx->next_frame_ms)
                return false;

        if(ctx->double_buffered) {
                // the buffer about to be swapped into is the one an earlier update may
                // still be reading. skip the frame rather than draw over an in-flight DMA
                // transfer -- accumulated regions survive, so the frame simply happens later
                if(light_display_render_context_busy(ctx->render))
                        return false;
                light_draw_context_swap_buffers(ctx->render);
        } else {
                // single-buffered: the update reads the very buffer about to be cleared, so
                // it has to be finished rather than merely checked. cooperative, not a
                // hardware spin -- it runs the same poll the scheduler's periodic task does
                for(uint8_t i = 0; i < ctx->display_count; i++) {
                        if(ctx->display[i])
                                light_display_wait_for_update(ctx->display[i]);
                }
        }

        // set forward from now rather than accumulated, so a long stall doesn't leave a
        // backlog of frame deadlines to burn through at once
        ctx->next_frame_ms = now + ctx->frame_interval_ms;
        ctx->frame_counter++;
        ctx->frame_open = true;

        light_draw_draw_clear(ctx->render);
        return true;
}

void light_canvas_invalidate_rect(struct canvas_context *ctx, struct canvas_region r)
{
        // corners may arrive in either order -- normalise per axis before clipping, or a
        // reversed pair would read as empty and be silently dropped
        if(r.x1 < r.x0) { int16_t t = r.x0; r.x0 = r.x1; r.x1 = t; }
        if(r.y1 < r.y0) { int16_t t = r.y0; r.y0 = r.y1; r.y1 = t; }

        if(!_canvas_region_clip(ctx, &r))
                return;
        if(!_canvas_region_add(ctx->region, &ctx->region_count, LIGHT_CANVAS_MAX_REGIONS, r)) {
                // too fragmented to track separately. collapsing to the whole canvas is
                // always correct -- it can only push more than needed, never less
                light_debug("canvas region list full, collapsing to full-canvas update");
                light_canvas_invalidate_all(ctx);
        }
}

void light_canvas_invalidate(struct canvas_context *ctx, light_draw_point2d p0, light_draw_point2d p1)
{
        light_canvas_invalidate_rect(ctx, (struct canvas_region) {
                (int16_t)p0.x, (int16_t)p0.y, (int16_t)p1.x, (int16_t)p1.y });
}

void light_canvas_reset_regions(struct canvas_context *ctx)
{
        ctx->region_count = 0;
        ctx->carried_count = 0;
}

void light_canvas_invalidate_all(struct canvas_context *ctx)
{
        ctx->region[0] = (struct canvas_region) {
                0, 0, (int16_t)ctx->render->dim_x - 1, (int16_t)ctx->render->dim_y - 1
        };
        ctx->region_count = 1;
}

void light_canvas_frame_end(struct canvas_context *ctx)
{
        if(!ctx->frame_open) {
                light_warn("no canvas frame open -- missing light_canvas_frame_begin()?");
                return;
        }
        ctx->frame_open = false;

        // what actually goes to the panel is this frame's regions PLUS the previous
        // frame's, so content that moved is erased where it used to be. built as a separate
        // list rather than merged into ctx->region, because ctx->region is what gets carried
        // to the NEXT frame -- carrying the merged result instead would make the pushed area
        // grow monotonically as content moved, until every frame pushed the whole canvas
        // every region is re-clipped against the canvas as it is NOW, not as it was when
        // the region was recorded. invalidate_rect() clips on the way in, but the canvas can
        // change shape afterwards -- a rotation swaps dim_x and dim_y -- and a carried region
        // from the previous frame then describes a canvas that no longer exists. pushing one
        // is not merely wasteful: it transforms to a negative physical coordinate, wraps
        // through light_draw_point2d's uint16_t, and hands the driver a region it can never finish
        struct canvas_region push[LIGHT_CANVAS_MAX_REGIONS];
        uint8_t push_count = 0;
        bool fits = true;
        for(uint8_t i = 0; i < ctx->region_count; i++) {
                struct canvas_region r = ctx->region[i];
                if(_canvas_region_clip(ctx, &r))
                        fits &= _canvas_region_add(push, &push_count, LIGHT_CANVAS_MAX_REGIONS, r);
        }
        for(uint8_t i = 0; i < ctx->carried_count; i++) {
                struct canvas_region r = ctx->carried[i];
                if(_canvas_region_clip(ctx, &r))
                        fits &= _canvas_region_add(push, &push_count, LIGHT_CANVAS_MAX_REGIONS, r);
        }
        if(!fits) {
                push_count = 1;
                push[0] = (struct canvas_region) {
                        0, 0, (int16_t)ctx->render->dim_x - 1, (int16_t)ctx->render->dim_y - 1
                };
        }

        // regions outer, displays inner: starting an update on a device drains whatever
        // that device still had in flight, so pushing region N to every device before
        // moving to region N+1 lets the devices transfer concurrently
        for(uint8_t i = 0; i < push_count; i++) {
                for(uint8_t d = 0; d < ctx->display_count; d++) {
                        if(!ctx->display[d])
                                continue;
                        light_display_command_update_region_async(ctx->display[d],
                                (light_draw_point2d) { (uint16_t)push[i].x0, (uint16_t)push[i].y0 },
                                (light_draw_point2d) { (uint16_t)push[i].x1, (uint16_t)push[i].y1 });
                }
        }

        // only what the caller invalidated this frame -- i.e. where the content is now,
        // which is where it will have been by the next frame
        for(uint8_t i = 0; i < ctx->region_count; i++)
                ctx->carried[i] = ctx->region[i];
        ctx->carried_count = ctx->region_count;
        ctx->region_count = 0;
}
