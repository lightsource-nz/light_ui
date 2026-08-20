#ifndef _LIGHT_CANVAS_H
#define _LIGHT_CANVAS_H

#include <light.h>
#include <light_display.h>
#include <light_draw.h>

#include <stdint.h>
#include <stdbool.h>

// how many disjoint dirty regions one frame can accumulate before the list collapses to a
// single full-canvas rect. disjoint regions are pushed separately (see
// light_canvas_frame_end()), and past a handful of them the per-region overhead outweighs
// what tracking them separately saves
#define LIGHT_CANVAS_MAX_REGIONS        8

// an inclusive rectangle in LOGICAL light_draw coordinates -- the space the caller draws in,
// post-rotation, never the physical buffer's.
//
// signed, unlike light_draw_point2d: a region covering content near an edge extends past it, and
// uint16_t would wrap those coordinates into huge positive values instead of letting the
// clip see them as negative
struct canvas_region {
        int16_t x0;
        int16_t y0;
        int16_t x1;
        int16_t y1;
};

struct canvas_context {
        struct light_draw_context *render;
        // displays this canvas is presented on. the array is borrowed, not copied --
        // callers pass the same static array they hand light_display_set_render_context()
        struct display_device **display;
        uint8_t display_count;
        bool double_buffered;

        // 0 means unpaced: every light_canvas_frame_begin() is allowed as soon as the
        // displays are ready to accept one
        uint32_t frame_interval_ms;
        uint32_t next_frame_ms;
        uint32_t frame_counter;

        // invalidated during the frame currently open
        struct canvas_region region[LIGHT_CANVAS_MAX_REGIONS];
        uint8_t region_count;
        // what the CALLER invalidated last frame -- deliberately not what was pushed last
        // frame, which already included the frame before that. see light_canvas_frame_end()
        struct canvas_region carried[LIGHT_CANVAS_MAX_REGIONS];
        uint8_t carried_count;
        bool frame_open;
};

extern void light_canvas_init();

// binds a render context to the displays it is presented on. the render context's geometry
// and rotation should already be set; light_canvas only ever reads them
extern struct canvas_context *light_canvas_create(struct light_draw_context *render,
                                        struct display_device **display, uint8_t display_count);
// enables double buffering on the underlying render context, and makes frame_begin() swap
// buffers rather than wait out the previous update. worth it whenever a frame's transfer is
// long enough to be worth overlapping with drawing the next one
extern void light_canvas_enable_double_buffer(struct canvas_context *ctx);
// suspends or resumes the front/back swap without freeing light_draw's second buffer. suspending
// leaves the back buffer untouched frame to frame, which is what lets a caller hold a
// rendered frame there and read from it while drawing -- an animation compositing against
// its own previous output, for instance. frames then serialise against the transfer, the
// same as a genuinely single-buffered canvas.
//
// re-enabling only takes effect if the render context actually has a second buffer;
// enabling it on a context that never had one is a no-op and warns
extern void light_canvas_set_double_buffer(struct canvas_context *ctx, bool enable);
// frames per second. 0 leaves the canvas unpaced, for a caller driven by events rather than
// a clock (redraw when something changed, as fast as the display will take it)
extern void light_canvas_set_frame_rate(struct canvas_context *ctx, uint32_t frame_rate);

// true when a frame may be drawn NOW: the frame deadline has passed and no display is still
// reading the buffer. false means neither -- the caller should do nothing and try again on
// the next tick, which is how a display that can't keep up skips frames instead of tearing.
//
// on true the buffer has already been prepared and cleared, so the caller draws
// immediately. **every frame is a full repaint** -- that is the contract, not an
// inefficiency. under double buffering the buffer being drawn into was last touched two
// frames ago, so it cannot be patched incrementally; redrawing it entirely is what makes
// the region tracking below sound, because it leaves the panel as the only stale thing
extern bool light_canvas_frame_begin(struct canvas_context *ctx);

// marks a region of the PANEL as no longer matching what the caller is drawing, so that
// frame_end() pushes it. call between frame_begin() and frame_end(), as you draw.
//
// note this means "the panel is wrong here", not "I drew here" -- drawing something
// identical to what is already on the panel needs no invalidation. the distinction matters
// for content that MOVES: the area it vacated is wrong too. callers do not have to track
// that themselves, because frame_end() also pushes whatever was invalidated in the PREVIOUS
// frame, which is exactly where the content used to be
extern void light_canvas_invalidate(struct canvas_context *ctx, light_draw_point2d p0, light_draw_point2d p1);
extern void light_canvas_invalidate_rect(struct canvas_context *ctx, struct canvas_region region);
// marks the whole canvas. needed for a first frame, since nothing on the panel corresponds
// to what is about to be drawn
extern void light_canvas_invalidate_all(struct canvas_context *ctx);
// forgets every accumulated and carried region without pushing them. call it when the
// canvas GEOMETRY changes -- a rotation swaps dim_x and dim_y -- because a region recorded
// in the previous coordinate space does not describe an area of the new one, so carrying it
// forward is meaningless rather than merely conservative. pair it with invalidate_all(),
// since after a change like that the whole panel needs repainting anyway
extern void light_canvas_reset_regions(struct canvas_context *ctx);

// closes the frame and pushes it. overlapping regions are merged and disjoint ones sent
// separately -- their union is often nearly the whole canvas (content at opposite edges in
// consecutive frames), almost all of it unchanged pixels, and a tall narrow union is
// precisely the shape drivers chunk row-by-row, costing a poll per row
extern void light_canvas_frame_end(struct canvas_context *ctx);

// how many frames have been drawn. for callers that want to report or throttle on it; the
// canvas itself only counts
static inline uint32_t light_canvas_frame_count(const struct canvas_context *ctx)
{
        return ctx->frame_counter;
}

#endif
