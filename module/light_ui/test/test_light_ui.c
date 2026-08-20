// behavioural checks for the widget toolkit: stack layout with min/max constraints,
// scrolling (clamps, the flush stop, the end-cap corners), focus cycling and
// scroll-into-view, hit-testing against the viewport, the tap-versus-drag touch tracker,
// widget-attached commands, and the declarative build/page machinery.
//
// everything runs against a canvas with ZERO displays and no font: light_ui's logic is
// deliberately hardware-free, and these are the paths that were hardware-verified but
// unmeasured until this suite existed. coordinates are computed from the window's own
// fields (padding, border) rather than hardcoded, so a styling change moves the
// expectations with it instead of breaking them.
//
// RUN AS: ctest, or this binary directly. With no argument it runs everything; with a case
// name it runs just that one -- see light_draw's suite for the conventions this follows.
#include <light_ui.h>
#include <light_cli.h>
#include <module/mod_light_ui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// light_core's framework.c refers to `this_app`, which only an application defines -- so a
// test binary linking the real library has to be one. it is never started
static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
Light_Application_Define(test_light_ui, _test_app_event, _test_app_main,
                                &light_ui, &light_core);

// cli_task() is registered by light_cli's module hook in a real application; this binary
// never loads modules, so it calls the task directly to drain what an activation queued
extern uint8_t cli_task(struct light_application *app);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                printf(__VA_ARGS__); printf("\n"); \
                failures++; \
        } \
} while(0)

// 100x160 is small enough to reason about and big enough for several rows; no font is set,
// which the toolkit tolerates (labels simply do not render), because layout and input are
// what this suite is for
static struct ui_context *make_ui(void)
{
        light_draw_context_t *render = light_draw_context_create((const uint8_t *)"test", 100, 160, 16);
        struct canvas_context *canvas = light_canvas_create(render, NULL, 0);
        return light_ui_create_context(canvas);
}

static int g_pressed;
static void _on_press(struct ui_button *btn, void *ud)
{
        g_pressed = (int)(uintptr_t)ud;
}

// a hand-placed root window: light_ui_relayout() is deliberately NOT called, so the rect
// chosen here survives and every expectation below can be derived from it
static struct ui_window *make_window(struct ui_context *ui, int16_t x0, int16_t y0,
                                int16_t x1, int16_t y1)
{
        return light_ui_window_create(ui, NULL,
                        (struct ui_rect) { x0, y0, x1, y1 }, NULL);
}

static int16_t inset_of(const struct ui_window *win)
{
        return (int16_t)(win->padding + (win->border ? 1 : 0));
}

// --- stack layout ---

static void test_stack_divides_rows_evenly(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        struct ui_button *b1 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        struct ui_button *b2 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        struct ui_button *b3 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        light_ui_window_layout_stack(win, 2);

        int16_t ins = inset_of(win);
        // rows span the full content width
        CHECK(b1->widget.rect.x0 == 10 + ins && b1->widget.rect.x1 == 89 - ins,
                        "row does not span the content width: %d..%d",
                        b1->widget.rect.x0, b1->widget.rect.x1);
        // equal heights, off by at most the remainder the last row absorbs
        int16_t h1 = b1->widget.rect.y1 - b1->widget.rect.y0 + 1;
        int16_t h2 = b2->widget.rect.y1 - b2->widget.rect.y0 + 1;
        CHECK(h1 == h2, "equal rows differ: %d vs %d", h1, h2);
        // the last row reaches the content bottom exactly (the remainder rule)
        CHECK(b3->widget.rect.y1 == 149 - ins,
                        "last row stops at %d, content bottom is %d",
                        b3->widget.rect.y1, 149 - ins);
        // rows are ordered and separated by the gap
        CHECK(b2->widget.rect.y0 == b1->widget.rect.y1 + 1 + 2, "gap not applied");
}

static void test_stack_pins_min_height_and_overflows(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_window_set_scroll(win, UI_SCROLL_VERTICAL);
        struct ui_button *b[5];
        for(int i = 0; i < 5; i++) {
                b[i] = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
                light_ui_widget_set_min_size(&b[i]->widget, 0, 50);
        }
        light_ui_window_layout_stack(win, 0);

        // every row pinned at its minimum, even though equal division would be far smaller
        for(int i = 0; i < 5; i++) {
                int16_t h = b[i]->widget.rect.y1 - b[i]->widget.rect.y0 + 1;
                CHECK(h == 50, "row %d height %d, expected the 50px minimum", i, h);
        }
        // and the content extent records the overflow the scroll moves through
        CHECK(win->content_h == 250, "content_h %d, expected 250", win->content_h);
        CHECK(b[4]->widget.rect.y1 > 149, "content does not overflow the frame");
}

static void test_stack_caps_max_height(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        struct ui_button *b1 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        struct ui_button *b2 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        light_ui_widget_set_max_size(&b1->widget, 0, 30);
        light_ui_widget_set_max_size(&b2->widget, 0, 30);
        light_ui_window_layout_stack(win, 0);

        int16_t h1 = b1->widget.rect.y1 - b1->widget.rect.y0 + 1;
        int16_t h2 = b2->widget.rect.y1 - b2->widget.rect.y0 + 1;
        CHECK(h1 == 30, "first row %d, expected the 30px cap", h1);
        // the cap also overrides the last row's absorb-the-remainder rule
        CHECK(h2 == 30, "last row %d, expected the 30px cap", h2);
}

static void test_flush_corners_on_non_scroll_last_row(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_window_set_corner_radius(win, 12);
        struct ui_button *b1 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        struct ui_button *b2 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        light_ui_window_layout_stack(win, 2);

        int16_t ins = inset_of(win);
        int16_t flush_r = (int16_t)(12 - ins);
        CHECK(b1->corner_radius == 0, "first row grew corners");
        CHECK(b2->corner_radius == (uint8_t)flush_r,
                        "last row radius %d, expected %d", b2->corner_radius, flush_r);
        CHECK(b2->corners == LIGHT_DRAW_CORNER_BOTTOM, "last row corners not BOTTOM");
        // flush means flush: the row's bottom edge sits at the same inset the container's
        // arc centres use, not at the deeper corner-cleared bottom
        CHECK(b2->widget.rect.y1 == 149 - ins,
                        "flush row bottom %d, expected %d", b2->widget.rect.y1, 149 - ins);
        // and the strip below it joins the hit area
        CHECK(b2->widget.hit_slop_y1 > 0, "flush row got no hit slop");
}

static void test_scroll_end_cap_rounds_last_row(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_window_set_corner_radius(win, 12);
        light_ui_window_set_scroll(win, UI_SCROLL_VERTICAL);
        struct ui_button *b[4];
        for(int i = 0; i < 4; i++) {
                b[i] = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
                light_ui_widget_set_min_size(&b[i]->widget, 0, 60);
        }
        light_ui_window_layout_stack(win, 0);

        int16_t cap_r = (int16_t)(12 - inset_of(win));
        // permanently, at layout time -- not granted and revoked by scroll position
        CHECK(b[3]->corner_radius == (uint8_t)cap_r,
                        "end cap radius %d, expected %d", b[3]->corner_radius, cap_r);
        CHECK(b[3]->corners == LIGHT_DRAW_CORNER_BOTTOM, "end cap corners not BOTTOM");
        CHECK(b[0]->corner_radius == 0, "a middle row grew corners");
        light_ui_window_scroll_by(win, 0, 40);
        CHECK(b[3]->corner_radius == (uint8_t)cap_r, "end cap lost by scrolling");
}

// --- scrolling ---

static void test_scroll_clamps_at_both_ends(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_window_set_scroll(win, UI_SCROLL_VERTICAL);
        for(int i = 0; i < 5; i++) {
                struct ui_button *b = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
                light_ui_widget_set_min_size(&b->widget, 0, 50);
        }
        light_ui_window_layout_stack(win, 0);

        // top stop: scrolling up from 0 moves nothing and says so
        CHECK(!light_ui_window_scroll_by(win, 0, -10), "scrolled above the top stop");
        CHECK(win->scroll_y == 0, "offset moved above 0");

        // bottom stop: an over-ask lands exactly on the clamp (square window, so the stop
        // is the plain viewport bottom: content 250 against a viewport of 134)
        CHECK(light_ui_window_scroll_by(win, 0, 999), "could not scroll down");
        int16_t ins = inset_of(win);
        int32_t viewport_h = (149 - ins) - (10 + ins) + 1;
        CHECK(win->scroll_y == win->content_h - viewport_h,
                        "bottom stop at %d, expected %d",
                        win->scroll_y, (int)(win->content_h - viewport_h));
        CHECK(!light_ui_window_scroll_by(win, 0, 5), "scrolled past the bottom stop");
}

static void test_scroll_stop_is_the_flush_edge(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_window_set_corner_radius(win, 12);
        light_ui_window_set_scroll(win, UI_SCROLL_VERTICAL);
        struct ui_button *last = NULL;
        for(int i = 0; i < 4; i++) {
                last = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
                light_ui_widget_set_min_size(&last->widget, 0, 60);
        }
        light_ui_window_layout_stack(win, 0);
        light_ui_window_scroll_by(win, 0, 999);

        // at the bottom stop the last row rests against the frame -- the same bottom edge
        // the non-scroll flush case uses -- not at the corner-cleared viewport bottom
        CHECK(last->widget.rect.y1 == 149 - inset_of(win),
                        "stop leaves the last row at %d, flush edge is %d",
                        last->widget.rect.y1, 149 - inset_of(win));
}

static void test_scroll_shifts_children_uniformly(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_window_set_scroll(win, UI_SCROLL_VERTICAL);
        struct ui_button *b[5];
        for(int i = 0; i < 5; i++) {
                b[i] = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
                light_ui_widget_set_min_size(&b[i]->widget, 0, 50);
        }
        light_ui_window_layout_stack(win, 0);

        struct ui_rect before[5];
        for(int i = 0; i < 5; i++)
                before[i] = b[i]->widget.rect;
        CHECK(light_ui_window_scroll_by(win, 0, 20), "scroll refused");
        for(int i = 0; i < 5; i++) {
                CHECK(b[i]->widget.rect.y0 == before[i].y0 - 20
                                && b[i]->widget.rect.y1 == before[i].y1 - 20,
                                "row %d not shifted by the scroll", i);
                CHECK(b[i]->widget.rect.x0 == before[i].x0, "row %d moved sideways", i);
        }
}

// --- focus ---

static void test_focus_cycles_and_wraps(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        struct ui_button *b1 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        struct ui_button *b2 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        struct ui_button *b3 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        light_ui_window_layout_stack(win, 0);

        // the first focusable created took focus, so cycling starts somewhere useful
        CHECK(ui->focused == &b1->widget, "first button did not take initial focus");
        light_ui_input_focus_next(ui);
        CHECK(ui->focused == &b2->widget, "next did not advance");
        light_ui_input_focus_next(ui);
        light_ui_input_focus_next(ui);
        CHECK(ui->focused == &b1->widget, "cycle did not wrap to the first");
        light_ui_input_focus_prev(ui);
        CHECK(ui->focused == &b3->widget, "prev did not wrap to the last");
}

static void test_focus_skips_hidden_and_disabled(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        struct ui_button *b1 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        struct ui_button *b2 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        struct ui_button *b3 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        light_ui_window_layout_stack(win, 0);

        light_ui_widget_set_visible(&b2->widget, false);
        light_ui_set_focus(ui, &b1->widget);
        light_ui_input_focus_next(ui);
        CHECK(ui->focused == &b3->widget, "hidden widget not skipped");

        light_ui_widget_set_enabled(&b3->widget, false);
        light_ui_set_focus(ui, &b1->widget);
        light_ui_input_focus_next(ui);
        CHECK(ui->focused == &b1->widget, "disabled widget not skipped (only b1 focusable)");
}

static void test_focus_scrolls_target_into_view(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_window_set_scroll(win, UI_SCROLL_VERTICAL);
        struct ui_button *b[5];
        for(int i = 0; i < 5; i++) {
                b[i] = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
                light_ui_widget_set_min_size(&b[i]->widget, 0, 50);
        }
        light_ui_window_layout_stack(win, 0);

        CHECK(win->scroll_y == 0, "list did not start at the top");
        for(int i = 0; i < 4; i++)
                light_ui_input_focus_next(ui);
        CHECK(ui->focused == &b[4]->widget, "focus did not reach the last row");
        CHECK(win->scroll_y > 0, "focusing an off-screen row did not scroll");
        int16_t ins = inset_of(win);
        CHECK(b[4]->widget.rect.y1 <= 149 - ins,
                        "focused row still below the viewport (%d)", b[4]->widget.rect.y1);
        CHECK(b[4]->widget.rect.y0 >= 10 + ins,
                        "focused row scrolled past the top (%d)", b[4]->widget.rect.y0);
}

// --- hit testing ---

static void test_press_focuses_and_activates(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, _on_press, (void *)1);
        struct ui_button *b2 = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, _on_press, (void *)2);
        light_ui_window_layout_stack(win, 0);

        g_pressed = 0;
        int16_t cy = (int16_t)((b2->widget.rect.y0 + b2->widget.rect.y1) / 2);
        CHECK(light_ui_input_press_at(ui, 50, (uint16_t)cy), "press on a button missed");
        CHECK(g_pressed == 2, "wrong button activated (%d)", g_pressed);
        CHECK(ui->focused == &b2->widget, "press did not move focus");

        g_pressed = 0;
        CHECK(!light_ui_input_press_at(ui, 5, 5), "press outside the window hit something");
        CHECK(g_pressed == 0, "outside press activated a button");
}

static void test_press_ignores_rows_outside_viewport(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_window_set_scroll(win, UI_SCROLL_VERTICAL);
        for(int i = 0; i < 5; i++) {
                struct ui_button *b = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, _on_press, (void *)(uintptr_t)(i + 1));
                light_ui_widget_set_min_size(&b->widget, 0, 50);
        }
        light_ui_window_layout_stack(win, 0);
        light_ui_window_scroll_by(win, 0, 999);

        //   rows scrolled above the viewport pass over the window's top band (border and
        // padding); a press there must not reach them -- what cannot be seen must not respond
        g_pressed = 0;
        CHECK(!light_ui_input_press_at(ui, 50, 11), "press in the top band hit a scrolled-out row");
        CHECK(g_pressed == 0, "scrolled-out row activated");
}

// --- the touch tracker ---

static void test_touch_tap_activates_on_release(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        struct ui_button *b = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, _on_press, (void *)7);
        light_ui_window_layout_stack(win, 0);

        g_pressed = 0;
        uint16_t cy = (uint16_t)((b->widget.rect.y0 + b->widget.rect.y1) / 2);
        CHECK(light_ui_input_touch(ui, 50, cy, true) == UI_TOUCH_PENDING, "down not pending");
        CHECK(g_pressed == 0, "down-edge activated -- taps deliver on release");
        CHECK(light_ui_input_touch(ui, 50, cy, false) == UI_TOUCH_TAP, "release not a tap");
        CHECK(g_pressed == 7, "tap did not activate the button under it");
}

static void test_touch_drag_scrolls_and_is_not_a_tap(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_window_set_scroll(win, UI_SCROLL_VERTICAL);
        for(int i = 0; i < 5; i++) {
                struct ui_button *b = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, _on_press, (void *)(uintptr_t)(i + 1));
                light_ui_widget_set_min_size(&b->widget, 0, 50);
        }
        light_ui_window_layout_stack(win, 0);

        g_pressed = 0;
        CHECK(light_ui_input_touch(ui, 50, 100, true) == UI_TOUCH_PENDING, "down not pending");
        // beyond the slop, over a scrollable window: the drag engages and the content
        // catches up with the finger's full movement since the touch began
        CHECK(light_ui_input_touch(ui, 50, 60, true) == UI_TOUCH_DRAG, "movement did not engage a drag");
        CHECK(win->scroll_y == 40, "drag did not catch the content up (scroll_y %d)", win->scroll_y);
        // and each further sample moves it by the step
        CHECK(light_ui_input_touch(ui, 50, 50, true) == UI_TOUCH_DRAG, "drag did not continue");
        CHECK(win->scroll_y == 50, "drag step wrong (scroll_y %d)", win->scroll_y);
        // release ends the drag; it is not a tap, and nothing activates
        CHECK(light_ui_input_touch(ui, 50, 50, false) == UI_TOUCH_DRAG_END, "release not DRAG_END");
        CHECK(g_pressed == 0, "a drag activated a button");
        CHECK(win->scroll_y == 50, "release moved the scroll");
}

static void test_touch_travel_without_target_is_neither(void)
{
        struct ui_context *ui = make_ui();
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, _on_press, (void *)1);
        light_ui_window_layout_stack(win, 0);

        //   a touch that travels with nothing to scroll commits to neither: not a drag
        // (nothing to move), not a tap (the finger clearly travelled) -- the release is left
        // for the gesture pipeline to classify as a swipe
        g_pressed = 0;
        CHECK(light_ui_input_touch(ui, 50, 100, true) == UI_TOUCH_PENDING, "down not pending");
        CHECK(light_ui_input_touch(ui, 50, 60, true) == UI_TOUCH_PENDING,
                        "travel with no scroll target engaged a drag");
        CHECK(light_ui_input_touch(ui, 50, 60, false) == UI_TOUCH_NONE,
                        "a travelled release was not left to the gesture pipeline");
        CHECK(g_pressed == 0, "a travelled release activated a button");
}

// --- widget-attached commands ---

static int g_command_ran;
static struct light_cli_invocation_result _cmd_ping(struct light_cli_invocation *invoke)
{
        g_command_ran++;
        return Result_Success;
}

static void test_widget_command_queues_for_cli_task(void)
{
        light_cli_init();
        struct light_command *root = light_cli_create_command(NULL,
                        (const uint8_t *)"testroot", (const uint8_t *)"test root", NULL);
        light_cli_create_command(root, (const uint8_t *)"ping",
                        (const uint8_t *)"sets a flag", _cmd_ping);

        struct ui_context *ui = make_ui();
        light_ui_set_command_root(ui, root);
        struct ui_window *win = make_window(ui, 10, 10, 89, 149);
        struct ui_button *b = light_ui_button_create(ui, &win->widget, (struct ui_rect){0,0,0,0}, NULL, NULL, NULL);
        light_ui_button_set_command(b, (const uint8_t *)"ping");
        light_ui_window_layout_stack(win, 0);

        g_command_ran = 0;
        uint16_t cy = (uint16_t)((b->widget.rect.y0 + b->widget.rect.y1) / 2);
        CHECK(light_ui_input_press_at(ui, 50, cy), "press missed the command button");
        // queued, not run: the handler must not have fired inside the activation
        CHECK(g_command_ran == 0, "command ran inline instead of being queued");
        // one cli_task tick drains and dispatches exactly the queued line
        cli_task(NULL);
        CHECK(g_command_ran == 1, "cli_task did not dispatch the queued command (%d)", g_command_ran);
        cli_task(NULL);
        CHECK(g_command_ran == 1, "the command ran twice");
}

// --- declarative build and pages ---

static void *g_bound;
Light_UI_Button_Define(_desc_btn_a, "A", _on_press, (void *)11, Light_UI_Bind(g_bound));
Light_UI_Button_Define(_desc_btn_b, "B", _on_press, (void *)12);
Light_UI_Window_Define(_desc_root, "T",
        Light_UI_Stack(2),
        Light_UI_Children(&_desc_btn_a, &_desc_btn_b));

static void test_build_binds_and_orders(void)
{
        struct ui_context *ui = make_ui();
        struct ui_widget *root = light_ui_build(ui, NULL, &_desc_root);
        CHECK(root != NULL && root->type == UI_WIDGET_WINDOW, "root not built as a window");
        // building a root relayouts against the canvas
        CHECK(root->rect.x1 == 99 && root->rect.y1 == 159,
                        "root not sized to the canvas: (%d,%d)", root->rect.x1, root->rect.y1);
        // sibling order is source order
        struct ui_widget *c1 = root->first_child;
        CHECK(c1 && c1->type == UI_WIDGET_BUTTON, "first child missing");
        CHECK(c1->next_sibling && c1->next_sibling->next_sibling == NULL, "child count wrong");
        // the bind delivered the first button's address
        CHECK(g_bound == (void *)c1, "Light_UI_Bind bound the wrong widget");
}

Light_UI_Button_Define(_desc_p2_btn, "back", NULL, NULL);
Light_UI_Window_Define(_desc_p1_win, "P1", Light_UI_Stack(0), Light_UI_Children(&_desc_btn_b));
Light_UI_Window_Define(_desc_p2_win, "P2", Light_UI_Stack(0), Light_UI_Children(&_desc_p2_btn));
Light_UI_Page_Define(_page_one, NULL, _desc_p1_win);
Light_UI_Page_Define(_page_two, &_page_one, _desc_p2_win);

static void test_pages_navigate_and_return(void)
{
        struct ui_context *ui = make_ui();
        light_ui_navigate(ui, &_page_one);
        CHECK(light_ui_current_page(ui) == &_page_one, "navigation did not land on page one");
        // back from a top-level page has nowhere to go and says so
        CHECK(!light_ui_navigate_back(ui), "back from a top-level page succeeded");

        light_ui_navigate(ui, &_page_two);
        CHECK(light_ui_current_page(ui) == &_page_two, "navigation did not land on page two");
        CHECK(light_ui_navigate_back(ui), "back from a child page failed");
        CHECK(light_ui_current_page(ui) == &_page_one, "back did not follow the parent");

        // the return override wins over the structural parent, for exactly one transfer
        light_ui_navigate_returning(ui, &_page_two, &_page_two);
        CHECK(light_ui_navigate_back(ui), "back with an override failed");
        CHECK(light_ui_current_page(ui) == &_page_two, "back ignored the return override");
        // the override was cleared by that navigation: back now follows the parent again
        CHECK(light_ui_navigate_back(ui), "second back failed");
        CHECK(light_ui_current_page(ui) == &_page_one, "override leaked into a later back");
}

// enumerated rather than discovered -- see light_draw's suite for why. KEEP IN SYNC with
// the CMakeLists.txt list; `--list` prints the names the binary actually has
static const struct {
        const char *name;
        void (*fn)(void);
} test_cases[] = {
        { "stack_divides_rows_evenly",           test_stack_divides_rows_evenly },
        { "stack_pins_min_height_and_overflows", test_stack_pins_min_height_and_overflows },
        { "stack_caps_max_height",               test_stack_caps_max_height },
        { "flush_corners_on_non_scroll_last_row", test_flush_corners_on_non_scroll_last_row },
        { "scroll_end_cap_rounds_last_row",      test_scroll_end_cap_rounds_last_row },
        { "scroll_clamps_at_both_ends",          test_scroll_clamps_at_both_ends },
        { "scroll_stop_is_the_flush_edge",       test_scroll_stop_is_the_flush_edge },
        { "scroll_shifts_children_uniformly",    test_scroll_shifts_children_uniformly },
        { "focus_cycles_and_wraps",              test_focus_cycles_and_wraps },
        { "focus_skips_hidden_and_disabled",     test_focus_skips_hidden_and_disabled },
        { "focus_scrolls_target_into_view",      test_focus_scrolls_target_into_view },
        { "press_focuses_and_activates",         test_press_focuses_and_activates },
        { "press_ignores_rows_outside_viewport", test_press_ignores_rows_outside_viewport },
        { "touch_tap_activates_on_release",      test_touch_tap_activates_on_release },
        { "touch_drag_scrolls_and_is_not_a_tap", test_touch_drag_scrolls_and_is_not_a_tap },
        { "touch_travel_without_target_is_neither", test_touch_travel_without_target_is_neither },
        { "widget_command_queues_for_cli_task",  test_widget_command_queues_for_cli_task },
        { "build_binds_and_orders",              test_build_binds_and_orders },
        { "pages_navigate_and_return",           test_pages_navigate_and_return },
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
