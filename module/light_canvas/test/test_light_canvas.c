// behavioural checks for light_canvas's region tracking, frame pacing and buffer handling.
//
// everything here runs against a canvas with ZERO displays: frame_begin()'s wait loop and
// frame_end()'s push loop both iterate over none, so the region arithmetic -- which is the
// part with subtle rules worth pinning -- is exercised with nothing to transfer to. the
// carried-region rule in particular ("carry what the CALLER invalidated, not what was
// pushed") is the one a refactor would most plausibly get wrong while looking right.
//
// RUN AS: ctest, or this binary directly. With no argument it runs everything; with a case
// name it runs just that one, which is how CTest registers them individually so a failure
// names itself instead of being one line in a wall of output.
#include <light_canvas.h>
#include <module/mod_light_canvas.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// the internal region helpers, tested directly: they are the unit the public API composes,
// and _canvas_region_add()'s restart-after-merge rule deserves a test that names it
#include "../src/light_canvas_internal.h"

// light_core's framework.c refers to `this_app`, which only an application defines -- so a
// test binary linking the real library has to be one. it is never started; see
// light_draw's suite for the full reasoning
static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
Light_Application_Define(test_light_canvas, _test_app_event, _test_app_main,
                                &light_canvas, &light_core);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                printf(__VA_ARGS__); printf("\n"); \
                failures++; \
        } \
} while(0)

static struct canvas_context *make_canvas(int w, int h)
{
        light_draw_context_t *render = light_draw_context_create((const uint8_t *)"test", w, h, 16);
        return light_canvas_create(render, NULL, 0);
}

static int region_equals(const struct canvas_region *r, int x0, int y0, int x1, int y1)
{
        return r->x0 == x0 && r->y0 == y0 && r->x1 == x1 && r->y1 == y1;
}

// --- region accumulation ---

static void test_invalidate_normalises_and_clips(void)
{
        struct canvas_context *c = make_canvas(100, 100);
        // reversed corners are normalised per axis rather than read as an empty rect
        light_canvas_invalidate_rect(c, (struct canvas_region) { 9, 9, 0, 0 });
        CHECK(c->region_count == 1, "reversed corners dropped (count %d)", c->region_count);
        CHECK(region_equals(&c->region[0], 0, 0, 9, 9),
                        "reversed corners not normalised: (%d,%d)-(%d,%d)",
                        c->region[0].x0, c->region[0].y0, c->region[0].x1, c->region[0].y1);

        // a region past the canvas edge is clamped to it, not dropped
        light_canvas_reset_regions(c);
        light_canvas_invalidate_rect(c, (struct canvas_region) { 90, 90, 150, 150 });
        CHECK(c->region_count == 1 && region_equals(&c->region[0], 90, 90, 99, 99),
                        "off-canvas region not clipped to the edge");

        // a region entirely off-canvas is dropped -- there is nothing it can describe
        light_canvas_reset_regions(c);
        light_canvas_invalidate_rect(c, (struct canvas_region) { 120, 120, 150, 150 });
        CHECK(c->region_count == 0, "entirely off-canvas region was kept");
}

static void test_overlapping_regions_merge(void)
{
        struct canvas_context *c = make_canvas(100, 100);
        light_canvas_invalidate_rect(c, (struct canvas_region) { 0, 0, 9, 9 });
        light_canvas_invalidate_rect(c, (struct canvas_region) { 5, 5, 19, 19 });
        CHECK(c->region_count == 1, "overlapping regions kept separate (count %d)", c->region_count);
        CHECK(region_equals(&c->region[0], 0, 0, 19, 19), "merge is not the union");

        // inclusive edges: sharing a column counts as overlap, not mere abutment
        light_canvas_reset_regions(c);
        light_canvas_invalidate_rect(c, (struct canvas_region) { 0, 0, 9, 9 });
        light_canvas_invalidate_rect(c, (struct canvas_region) { 9, 0, 19, 9 });
        CHECK(c->region_count == 1, "edge-sharing regions kept separate");
}

static void test_one_addition_bridges_two_regions(void)
{
        //   the restart-after-merge rule: a single new region can overlap two existing
        // disjoint ones, and the merge has to run again so the bridged pair collapses too --
        // a single pass would leave two overlapping regions and push the shared pixels twice
        struct canvas_context *c = make_canvas(100, 100);
        light_canvas_invalidate_rect(c, (struct canvas_region) { 0, 0, 9, 9 });
        light_canvas_invalidate_rect(c, (struct canvas_region) { 20, 0, 29, 9 });
        CHECK(c->region_count == 2, "disjoint regions merged prematurely");
        light_canvas_invalidate_rect(c, (struct canvas_region) { 5, 0, 24, 9 });
        CHECK(c->region_count == 1, "bridging region left the pair unmerged (count %d)",
                        c->region_count);
        CHECK(region_equals(&c->region[0], 0, 0, 29, 9), "bridged union is wrong");
}

static void test_region_overflow_collapses_to_full_canvas(void)
{
        struct canvas_context *c = make_canvas(200, 200);
        // LIGHT_CANVAS_MAX_REGIONS disjoint regions fill the list exactly
        for(int i = 0; i < LIGHT_CANVAS_MAX_REGIONS; i++)
                light_canvas_invalidate_rect(c, (struct canvas_region) {
                        (int16_t)(i * 20), 0, (int16_t)(i * 20 + 5), 5 });
        CHECK(c->region_count == LIGHT_CANVAS_MAX_REGIONS,
                        "expected a full list, got %d", c->region_count);
        // one more disjoint region cannot be tracked separately: the list collapses to a
        // single full-canvas rect, which can only push more than needed, never less
        light_canvas_invalidate_rect(c, (struct canvas_region) { 0, 100, 5, 105 });
        CHECK(c->region_count == 1 && region_equals(&c->region[0], 0, 0, 199, 199),
                        "overflow did not collapse to the full canvas");
}

// --- the carried-region rule ---

static void test_carry_is_what_the_caller_invalidated(void)
{
        struct canvas_context *c = make_canvas(100, 100);

        CHECK(light_canvas_frame_begin(c), "unpaced first frame refused");
        light_canvas_invalidate_rect(c, (struct canvas_region) { 0, 0, 9, 9 });
        light_canvas_frame_end(c);
        CHECK(c->carried_count == 1 && region_equals(&c->carried[0], 0, 0, 9, 9),
                        "first frame's invalidation not carried");
        CHECK(c->region_count == 0, "regions survived frame_end");

        //   the frame that pushes A-union-B must carry only B: carrying the union would make
        // the pushed area grow monotonically as content moved, until every frame pushed the
        // whole canvas. this is the subtle rule the whole mechanism turns on
        CHECK(light_canvas_frame_begin(c), "second frame refused");
        light_canvas_invalidate_rect(c, (struct canvas_region) { 50, 50, 59, 59 });
        light_canvas_frame_end(c);
        CHECK(c->carried_count == 1 && region_equals(&c->carried[0], 50, 50, 59, 59),
                        "carried region is not just this frame's invalidation");
}

static void test_reset_regions_forgets_everything(void)
{
        struct canvas_context *c = make_canvas(100, 100);
        CHECK(light_canvas_frame_begin(c), "frame refused");
        light_canvas_invalidate_rect(c, (struct canvas_region) { 0, 0, 9, 9 });
        light_canvas_frame_end(c);
        light_canvas_invalidate_rect(c, (struct canvas_region) { 20, 20, 29, 29 });
        light_canvas_reset_regions(c);
        CHECK(c->region_count == 0 && c->carried_count == 0,
                        "reset left regions behind (%d accumulated, %d carried)",
                        c->region_count, c->carried_count);
}

static void test_clip_follows_a_rotated_canvas(void)
{
        //   the reason frame_end() re-clips carried regions: a rotation swaps dim_x/dim_y,
        // and a region recorded against the old shape describes nothing on the new one --
        // unclipped it would wrap through uint16 and hand a driver a region it cannot finish
        struct canvas_context *c = make_canvas(100, 200);
        struct canvas_region r = { 0, 0, 99, 199 };
        CHECK(_canvas_region_clip(c, &r), "full-canvas region rejected");

        light_draw_context_set_rotation(c->render, LIGHT_DRAW_ROTATE_90);
        struct canvas_region r2 = { 0, 0, 99, 199 };
        CHECK(_canvas_region_clip(c, &r2), "region rejected after rotation");
        CHECK(r2.x1 == 99 && r2.y1 == 99,
                        "old-shape region not re-clipped to the rotated canvas: (%d,%d)",
                        r2.x1, r2.y1);
}

// --- frames and pacing ---

static void test_frame_open_guard(void)
{
        struct canvas_context *c = make_canvas(100, 100);
        CHECK(light_canvas_frame_begin(c), "first frame refused");
        CHECK(!light_canvas_frame_begin(c), "nested frame_begin allowed");
        light_canvas_frame_end(c);
        // an end without a begin warns and does nothing -- and must not crash
        light_canvas_frame_end(c);
        CHECK(light_canvas_frame_begin(c), "frame refused after mismatched end");
        light_canvas_frame_end(c);
}

static void test_frame_pacing_defers_the_next_frame(void)
{
        struct canvas_context *c = make_canvas(100, 100);
        // 1 fps: the second frame's deadline is a full second away, far beyond this test's
        // runtime, so the refusal is deterministic
        light_canvas_set_frame_rate(c, 1);
        CHECK(light_canvas_frame_begin(c), "paced first frame refused");
        light_canvas_frame_end(c);
        CHECK(!light_canvas_frame_begin(c), "frame allowed before its deadline");
        // unpacing lifts the deadline immediately
        light_canvas_set_frame_rate(c, 0);
        CHECK(light_canvas_frame_begin(c), "unpaced frame refused");
        light_canvas_frame_end(c);
        CHECK(c->frame_counter == 2, "frame counter %d, expected 2", c->frame_counter);
}

static void test_double_buffer_swaps_and_suspends(void)
{
        struct canvas_context *c = make_canvas(100, 100);
        light_canvas_enable_double_buffer(c);
        uint8_t *front = c->render->buffer;
        CHECK(light_canvas_frame_begin(c), "double-buffered frame refused");
        CHECK(c->render->buffer != front, "frame_begin did not swap buffers");
        light_canvas_frame_end(c);

        // suspending the swap holds the buffer still, without freeing the second buffer
        light_canvas_set_double_buffer(c, false);
        front = c->render->buffer;
        CHECK(light_canvas_frame_begin(c), "swap-suspended frame refused");
        CHECK(c->render->buffer == front, "suspended canvas still swapped");
        light_canvas_frame_end(c);

        // and resuming works, because the second buffer still exists
        light_canvas_set_double_buffer(c, true);
        front = c->render->buffer;
        CHECK(light_canvas_frame_begin(c), "resumed frame refused");
        CHECK(c->render->buffer != front, "resumed canvas did not swap");
        light_canvas_frame_end(c);
}

// enumerated rather than discovered -- see light_draw's suite for why. KEEP IN SYNC with
// the CMakeLists.txt list; `--list` prints the names the binary actually has
static const struct {
        const char *name;
        void (*fn)(void);
} test_cases[] = {
        { "invalidate_normalises_and_clips",     test_invalidate_normalises_and_clips },
        { "overlapping_regions_merge",           test_overlapping_regions_merge },
        { "one_addition_bridges_two_regions",    test_one_addition_bridges_two_regions },
        { "region_overflow_collapses_to_full_canvas", test_region_overflow_collapses_to_full_canvas },
        { "carry_is_what_the_caller_invalidated", test_carry_is_what_the_caller_invalidated },
        { "reset_regions_forgets_everything",    test_reset_regions_forgets_everything },
        { "clip_follows_a_rotated_canvas",       test_clip_follows_a_rotated_canvas },
        { "frame_open_guard",                    test_frame_open_guard },
        { "frame_pacing_defers_the_next_frame",  test_frame_pacing_defers_the_next_frame },
        { "double_buffer_swaps_and_suspends",    test_double_buffer_swaps_and_suspends },
};
#define TEST_CASE_COUNT (sizeof(test_cases) / sizeof(*test_cases))

int main(int argc, char **argv)
{
        if(argc > 1 && strcmp(argv[1], "--list") == 0) {
                for(size_t i = 0; i < TEST_CASE_COUNT; i++)
                        printf("%s\n", test_cases[i].name);
                return 0;
        }
        if(argc > 1) {
                for(size_t i = 0; i < TEST_CASE_COUNT; i++) {
                        if(strcmp(argv[1], test_cases[i].name) != 0)
                                continue;
                        test_cases[i].fn();
                        printf("%s: %s, %d failure(s)\n", test_cases[i].name,
                               failures ? "FAILED" : "PASSED", failures);
                        return failures ? 1 : 0;
                }
                printf("FAIL: no such test case '%s'\n", argv[1]);
                return 2;
        }
        for(size_t i = 0; i < TEST_CASE_COUNT; i++)
                test_cases[i].fn();
        printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
        return failures ? 1 : 0;
}
