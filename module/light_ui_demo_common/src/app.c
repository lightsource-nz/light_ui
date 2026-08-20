#include <light_ui_demo.h>
#include <light_canvas.h>
#include <light_cli.h>
#include <light_platform.h>

#include <stdint.h>
#include <string.h>

// shared light_ui demo: one framed window filling the canvas, with a stack of buttons in
// it. each button toggles its own label between "Name" and "Name *" when activated, which
// is enough to show a press landing on the right widget without needing screen space for a
// status line the 128x64 rigs don't have to spare.
//
// deliberately identical on both bring-up rigs, so the only thing that differs between a
// two-button board and a touch board is which input path drives the same widget tree

struct display_device *_display[LIGHT_UI_DEMO_DISPLAY_COUNT];
struct ui_context *_ui;
struct backlight_device *_backlight_main;
struct audio_device *_audio_main;

// when input was last seen, and whether the backlight has already been dimmed for idleness.
// the flag matters: without it every tick past the threshold would restart the fade, which
// would hold the brightness at its starting value forever
static uint32_t last_activity_ms;
static bool backlight_dimmed;

// set by light_ui_demo_request_shutdown(), read once per tick by light_ui_demo_main(). no
// atomics needed: both ends run on the main task loop -- a command handler runs inside
// cli_task(), which is a periodic task like the demo's own
static bool shutdown_requested;

void light_ui_demo_request_shutdown(void)
{
        shutdown_requested = true;
}

//   the demo's command tree, shared by every app the same way the widget tree is. Reached two
// ways: LIGHT_BOOT_COMMAND, baked into the image by CMake and run once at application launch
// (which is what makes the brightness a board starts at a preset setting rather than a #define
// somebody has to edit a file to change) -- and interactively on boards with a console, typed
// at the USB CDC prompt and fed through _poll_console() below. Lines are typed without the
// root's name: "backlight 128", "shutdown".
//   apps hang board-specific subcommands off cmd_light_ui_demo, declared in light_ui_demo.h.
static struct light_cli_invocation_result do_cmd_light_ui_demo(struct light_cli_invocation *invoke)
{
        // the bare root command does nothing on its own; it exists to hang subcommands off
        return Result_Success;
}
static struct light_cli_invocation_result do_cmd_light_ui_demo_backlight(struct light_cli_invocation *invoke)
{
        const uint8_t *value = light_cli_invocation_get_arg_value(invoke, 0);

        if(!value) {
                light_error("backlight: expected a level in 0..%d", LIGHT_BACKLIGHT_LEVEL_MAX);
                return Result_Error;
        }
        //   parsed by hand rather than with strtol: the input is at most four digits, and this
        // reports a bad one precisely rather than silently yielding zero the way atoi() would
        uint32_t level = 0;
        for(const uint8_t *p = value; *p; p++) {
                if(*p < '0' || *p > '9') {
                        light_error("backlight: '%s' is not a number", value);
                        return Result_Error;
                }
                level = (level * 10) + (uint32_t)(*p - '0');
                if(level > LIGHT_BACKLIGHT_LEVEL_MAX) {
                        light_error("backlight: level %s exceeds the maximum of %d",
                                        value, LIGHT_BACKLIGHT_LEVEL_MAX);
                        return Result_Error;
                }
        }
        if(!_backlight_main) {
                light_error("backlight: no backlight device");
                return Result_Error;
        }
        light_info("backlight: setting level to %d", level);
        light_backlight_set_level(_backlight_main, (uint16_t) level);

        return Result_Success;
}
//   asks the demo's periodic task to return LF_STATUS_SHUTDOWN, which ends the scheduler loop
// and starts the orderly unload. A request rather than anything immediate, because this
// handler runs inside cli_task() and the tail of the current scheduler pass should still
// happen; the demo task answers on its next tick, which is also what guarantees the "winding
// down" line below is queued while the log drain is still running to print it. What the board
// does once the framework has wound down is each app's own business, in its main() after
// light_framework_run() returns -- the touch169 app darkens the panel and drops into BOOTSEL
static struct light_cli_invocation_result do_cmd_light_ui_demo_shutdown(struct light_cli_invocation *invoke)
{
        light_info("shutdown: winding down at the console's request");
        light_ui_demo_request_shutdown();
        return Result_Success;
}
//   interactive discoverability: "help" lists the subcommands and what they take. Against the
// root command, so it stays correct as commands are added rather than being a hand-kept list
static struct light_cli_invocation_result do_cmd_light_ui_demo_help(struct light_cli_invocation *invoke)
{
        light_cli_print_command_help(&cmd_light_ui_demo);
        return Result_Success;
}
//   the button rig's two keys, available from the console: "focus next|prev" and "press".
// This is what lets focus-driven behaviour (cycling, and the scroll-into-view it triggers on
// an overflowing list) be exercised on a board whose only physical input is a touch panel --
// and scripted from a host, which no arrangement of fingers can be
static struct light_cli_invocation_result do_cmd_light_ui_demo_focus(struct light_cli_invocation *invoke)
{
        const uint8_t *dir = light_cli_invocation_get_arg_value(invoke, 0);
        if(!_ui) {
                light_error("focus: no ui context yet");
                return Result_Error;
        }
        if(dir && !strcmp((const char *)dir, "prev"))
                light_ui_input_focus_prev(_ui);
        else if(dir && !strcmp((const char *)dir, "next"))
                light_ui_input_focus_next(_ui);
        else {
                light_error("focus: expected 'next' or 'prev'");
                return Result_Error;
        }
        light_ui_demo_note_activity();
        return Result_Success;
}
static struct light_cli_invocation_result do_cmd_light_ui_demo_press(struct light_cli_invocation *invoke)
{
        if(!_ui) {
                light_error("press: no ui context yet");
                return Result_Error;
        }
        light_ui_demo_note_activity();
        light_ui_input_activate(_ui);
        return Result_Success;
}
//   the root name has to match the first token of LIGHT_BOOT_COMMAND: process_command_line()
// treats argv[0] as the root command, exactly as a shell command line does
Light_Command_Define(cmd_light_ui_demo, &root_command, "light_ui_demo",
                        "commands for the shared light_ui demo", do_cmd_light_ui_demo, 0, 0);
Light_Command_Define(cmd_light_ui_demo_backlight, &cmd_light_ui_demo, "backlight",
                        "sets the panel backlight level", do_cmd_light_ui_demo_backlight, 1, 1);
Light_Command_Define(cmd_light_ui_demo_shutdown, &cmd_light_ui_demo, "shutdown",
                        "winds down the framework and shuts the device down", do_cmd_light_ui_demo_shutdown, 0, 0);
Light_Command_Define(cmd_light_ui_demo_help, &cmd_light_ui_demo, "help",
                        "lists the commands this console accepts", do_cmd_light_ui_demo_help, 0, 0);
Light_Command_Define(cmd_light_ui_demo_focus, &cmd_light_ui_demo, "focus",
                        "moves the focus highlight: focus <next|prev>", do_cmd_light_ui_demo_focus, 1, 1);
Light_Command_Define(cmd_light_ui_demo_press, &cmd_light_ui_demo, "press",
                        "activates the focused widget", do_cmd_light_ui_demo_press, 0, 0);

#if LIGHT_PLATFORM_USB_ON_CORE1
//   the console feeder: pops at most ONE completed line per tick from the core 1 USB worker
// (which reads, echoes and line-edits at its end -- core 0 must never touch the CDC endpoints
// itself) and queues it for cli_task() to dispatch on a later tick. One per tick is the
// intended pace, not a shortcut: cli_task() drains one line per tick and is registered ahead
// of this task, so the queue between them never holds more than a single line -- see
// cli_task()'s own comment for why that ordering is load-bearing.
//   compiled only where the platform's core 1 USB worker exists to read lines at all; on a
// board without a console (po13) this is nothing, and the command tree above still serves the
// baked boot command
static void _poll_console(void)
{
        uint8_t line[LIGHT_STREAM_MAX_MSG_LENGTH];
        if(!light_core_port_console_take_line(line, sizeof(line)))
                return;
        // typing at the console is handling the device, the same as touching it
        light_ui_demo_note_activity();
        light_cli_queue_line(&cmd_light_ui_demo, line);
}
#else
static void _poll_console(void) {}
#endif

void light_ui_demo_note_activity(void)
{
        last_activity_ms = light_platform_get_time_since_init();
        if(!_backlight_main || !backlight_dimmed)
                return;
        backlight_dimmed = false;
        light_backlight_fade_to(_backlight_main, LIGHT_UI_DEMO_BACKLIGHT_FULL, LIGHT_UI_DEMO_FADE_UP_MS);
}

static void _service_idle_backlight(uint32_t now)
{
        if(!_backlight_main || backlight_dimmed)
                return;
        if(now - last_activity_ms < LIGHT_UI_DEMO_IDLE_MS)
                return;
        backlight_dimmed = true;
        light_backlight_fade_to(_backlight_main, LIGHT_UI_DEMO_BACKLIGHT_DIM, LIGHT_UI_DEMO_FADE_DOWN_MS);
}

#define LIGHT_UI_DEMO_BUTTON_COUNT              3

// named once and used in three places -- the descriptor below, and both label arrays -- so a
// renamed button cannot end up disagreeing with the label it toggles back to
#define DEMO_LABEL_0                            "Alpha"
#define DEMO_LABEL_1                            "Beta"
#define DEMO_LABEL_2                            "Gamma"

static const uint8_t *const _label_off[LIGHT_UI_DEMO_BUTTON_COUNT] = {
        (const uint8_t *)DEMO_LABEL_0, (const uint8_t *)DEMO_LABEL_1, (const uint8_t *)DEMO_LABEL_2
};
// parenthesised so the cast plainly applies to the concatenated literal. it would anyway --
// adjacent string literals are joined in translation phase 6, before the cast is parsed -- but
// unparenthesised it reads like a cast of the first literal alone, next to a stray second one
static const uint8_t *const _label_on[LIGHT_UI_DEMO_BUTTON_COUNT] = {
        (const uint8_t *)(DEMO_LABEL_0 " *"), (const uint8_t *)(DEMO_LABEL_1 " *"),
        (const uint8_t *)(DEMO_LABEL_2 " *")
};
static bool _toggled[LIGHT_UI_DEMO_BUTTON_COUNT];

static struct light_draw_context *render;
static struct canvas_context *canvas;

static void _on_press(struct ui_button *btn, void *user_data)
{
        uintptr_t index = (uintptr_t)user_data;
        _toggled[index] = !_toggled[index];
        light_ui_button_set_label(btn,
                        _toggled[index] ? _label_on[index] : _label_off[index]);
        light_info("button %d toggled %s", (int)index, _toggled[index] ? "on" : "off");
        if(_audio_main)
                light_audio_tone(_audio_main, LIGHT_UI_DEMO_CLICK_HZ, LIGHT_UI_DEMO_CLICK_MS);
}

// a short decaying chirp, synthesised rather than embedded: it exercises conversion, DMA and
// rate pacing with nothing to build, embed or keep in flash, which is the whole point of a
// bring-up sound. a square wave rather than a sine because the maths stays integer and a
// piezo cannot tell the difference anyway
static void _play_startup_chirp(void)
{
        if(!_audio_main)
                return;

        uint32_t count = (LIGHT_UI_DEMO_CHIRP_RATE * LIGHT_UI_DEMO_CHIRP_MS) / 1000;
        int16_t *pcm = light_alloc(count * sizeof(int16_t));
        if(!pcm) {
                light_warn("no memory for the startup chirp (%d samples)", (int)count);
                return;
        }

        // sweeps upward, with a linear decay so it ends at silence -- stopping at full
        // amplitude would leave a step, and a step is a click.
        //
        // amplitude is a config knob (LIGHT_UI_DEMO_CHIRP_AMPLITUDE) rather than a constant
        // because the right value depends entirely on what is on the end of the pin. Raising
        // it to full scale was tried on the touch board's piezo and changed nothing audible,
        // so the default sits below maximum where it leaves headroom for a board with an
        // amplifier
        uint32_t phase = 0;
        for(uint32_t i = 0; i < count; i++) {
                uint32_t hz = 1200 + (2400 * i) / count;
                phase += hz;
                int32_t amp = LIGHT_UI_DEMO_CHIRP_AMPLITUDE
                                - (int32_t)((LIGHT_UI_DEMO_CHIRP_AMPLITUDE * (int64_t)i) / count);
                bool high = ((phase / LIGHT_UI_DEMO_CHIRP_RATE) & 1) != 0;
                pcm[i] = (int16_t)(high ? amp : -amp);
        }

        struct audio_format fmt = {
                .sample_rate = LIGHT_UI_DEMO_CHIRP_RATE,
                .encoding = LIGHT_AUDIO_PCM_S16,
                .channels = 1
        };
        if(!light_audio_play_pcm(_audio_main, pcm, count, fmt))
                light_warn("startup chirp was refused by the audio device","");
        // light_audio_play_pcm() converts into its own buffer for S16, so this one has done
        // its job by the time the call returns
        light_free(pcm);
}

// the whole interface, as data. the tree's shape is the source's shape: three buttons, in the
// order they appear on screen, inside one rounded window that stacks them.
//
// no rects anywhere -- Light_UI_Stack() gives every child an equal-height row in the window's
// content area, which is the entire reason light_ui_window_layout_stack() exists. the window
// gets no rect either: it is the root, and light_ui_relayout() sizes the root to the canvas,
// which is what makes this identical source work on a 64x128 OLED and a 240x280 panel.
//
// the user_data index is what ties a press back to _toggled[]/_label_on[]; _on_press receives
// the button itself, so nothing here needs to be bound to a variable (the old code kept a
// _button[] array that was written and never read)
Light_UI_Button_Define(_btn_alpha, DEMO_LABEL_0, _on_press, (void *)0);
Light_UI_Button_Define(_btn_beta,  DEMO_LABEL_1, _on_press, (void *)1);
Light_UI_Button_Define(_btn_gamma, DEMO_LABEL_2, _on_press, (void *)2);

#if LIGHT_UI_DEMO_PAGES

//   the navigation example. Two pages: the main one, and a second reached from its last row
// and returned from with a swipe. The pages reference each other -- the child names its
// parent, the parent's handler names the child -- so the child is declared before the handler
// that navigates to it
Light_UI_Page_Declare(_page_detail);

static void _on_open_detail(struct ui_button *btn, void *user_data)
{
        if(_audio_main)
                light_audio_tone(_audio_main, LIGHT_UI_DEMO_CLICK_HZ, LIGHT_UI_DEMO_CLICK_MS);
        //   last statement in the handler, and deliberately: navigating destroys the tree this
        // button belongs to, so `btn` is released memory the moment it returns
        light_ui_navigate(btn->widget.ui, &_page_detail);
}
static void _on_detail_back(struct ui_button *btn, void *user_data)
{
        if(_audio_main)
                light_audio_tone(_audio_main, LIGHT_UI_DEMO_CLICK_HZ, LIGHT_UI_DEMO_CLICK_MS);
        // the same route the swipe takes, so the on-screen control and the gesture cannot
        // disagree about where "back" is
        light_ui_navigate_back(btn->widget.ui);
}

Light_UI_Button_Define(_btn_more, "More >", _on_open_detail, NULL);

//   the scrolling example: more rows than the window can show, each pinned at a minimum
// height, inside a window marked UI_SCROLL_VERTICAL. Nothing here scrolls explicitly --
// focusing an off-screen item (focus cycling, or tapping a half-visible row) is what moves
// the list, via light_ui_set_focus()'s scroll-into-view
Light_UI_Page_Declare(_page_list);

static void _on_open_list(struct ui_button *btn, void *user_data)
{
        if(_audio_main)
                light_audio_tone(_audio_main, LIGHT_UI_DEMO_CLICK_HZ, LIGHT_UI_DEMO_CLICK_MS);
        light_ui_navigate(btn->widget.ui, &_page_list);
}
static void _on_list_item(struct ui_button *btn, void *user_data)
{
        light_info("list item %d pressed", (int)(uintptr_t)user_data);
        if(_audio_main)
                light_audio_tone(_audio_main, LIGHT_UI_DEMO_CLICK_HZ, LIGHT_UI_DEMO_CLICK_MS);
}

Light_UI_Button_Define(_btn_list, "List >", _on_open_list, NULL);

#define LIST_ITEM(n) \
        Light_UI_Button_Define(_btn_item_##n, "Item " #n, _on_list_item, (void *)(n), \
                        Light_UI_MinSize(0, LIGHT_UI_DEMO_LIST_MIN_ROW))
LIST_ITEM(1); LIST_ITEM(2); LIST_ITEM(3); LIST_ITEM(4);
LIST_ITEM(5); LIST_ITEM(6); LIST_ITEM(7);
// the back row is a list item like any other, and deliberately LAST: reaching it means
// scrolling the whole list, so navigating out doubles as the end-to-end check
Light_UI_Button_Define(_btn_list_back, "< Back", _on_detail_back, NULL,
                Light_UI_MinSize(0, LIGHT_UI_DEMO_LIST_MIN_ROW));

Light_UI_Window_Define(_demo_window, LIGHT_UI_DEMO_TITLE,
        Light_UI_Rounded(LIGHT_UI_DEMO_CORNER_RADIUS),
        Light_UI_Stack(LIGHT_UI_DEMO_ROW_GAP),
        Light_UI_Children(&_btn_alpha, &_btn_beta, &_btn_gamma, &_btn_more, &_btn_list));

Light_UI_Label_Define(_lbl_detail, "swipe right to go back");
Light_UI_Button_Define(_btn_back, "< Back", _on_detail_back, NULL);
Light_UI_Window_Define(_detail_window, "More",
        Light_UI_Rounded(LIGHT_UI_DEMO_CORNER_RADIUS),
        Light_UI_Stack(LIGHT_UI_DEMO_ROW_GAP),
        Light_UI_Children(&_lbl_detail, &_btn_back));

Light_UI_Window_Define(_list_window, "List",
        Light_UI_Rounded(LIGHT_UI_DEMO_CORNER_RADIUS),
        Light_UI_Stack(LIGHT_UI_DEMO_ROW_GAP),
        Light_UI_Scroll(UI_SCROLL_VERTICAL),
        Light_UI_Children(&_btn_item_1, &_btn_item_2, &_btn_item_3, &_btn_item_4,
                        &_btn_item_5, &_btn_item_6, &_btn_item_7, &_btn_list_back));

// the main page is top-level, so back from it has nowhere to go and does nothing
Light_UI_Page_Define(_page_main, NULL, _demo_window);
Light_UI_Page_Define(_page_detail, &_page_main, _detail_window);
Light_UI_Page_Define(_page_list, &_page_main, _list_window);

#else

Light_UI_Window_Define(_demo_window, LIGHT_UI_DEMO_TITLE,
        Light_UI_Rounded(LIGHT_UI_DEMO_CORNER_RADIUS),
        Light_UI_Stack(LIGHT_UI_DEMO_ROW_GAP),
        Light_UI_Children(&_btn_alpha, &_btn_beta, &_btn_gamma));

#endif

void light_ui_demo_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
        case LF_EVENT_MODULE_LOAD:;
                render = light_draw_context_create("light_ui_demo_render",
                                LIGHT_UI_DEMO_RENDER_WIDTH, LIGHT_UI_DEMO_RENDER_HEIGHT, LIGHT_UI_DEMO_RENDER_BPP);
                light_draw_context_set_rotation(render, LIGHT_UI_DEMO_RENDER_ROTATION);
                // must happen before any widget is created: light_ui reads char_width/
                // char_height off the context to lay out and truncate labels, and
                // light_draw_draw_text() is a silent no-op with no font set
                light_draw_context_set_font(render, __light_ui_demo_font());

                light_debug("passing control to board hardware setup function","");
                __light_ui_demo_hardware_init();
                for(uint8_t i = 0; i < LIGHT_UI_DEMO_DISPLAY_COUNT; i++)
                        light_display_set_render_context(_display[i], render);

                // frame pacing, double buffering and region flushing live in the canvas;
                // light_ui contributes only the widget tree and what changed in it
                canvas = light_canvas_create(render, _display, LIGHT_UI_DEMO_DISPLAY_COUNT);
                light_canvas_enable_double_buffer(canvas);
                light_canvas_set_frame_rate(canvas, LIGHT_UI_DEMO_FRAME_RATE);

                _ui = light_ui_create_context(canvas);
                // the whole widget tree, from the descriptor above. building a root sizes it
                // to the canvas, so there is no rect to compute here
#if LIGHT_UI_DEMO_PAGES
                //   entered through the page system rather than built directly, so the context
                // knows which page it is showing. light_ui_build() on its own leaves ui->page
                // NULL, and navigate_back() would then have nothing to reason from
                light_ui_navigate(_ui, &_page_main);
#else
                light_ui_build(_ui, NULL, &_demo_window);
#endif
                // after the tree exists, not before: setting the inset re-lays-out, and with
                // no root yet there would be nothing to lay out
                light_ui_set_safe_inset(_ui, LIGHT_UI_DEMO_SAFE_INSET);
                // nothing on the panel matches the freshly built tree yet, so the first
                // frame has to push the whole canvas rather than just what changed
                light_ui_invalidate(_ui);
                light_info("ui pipeline setup complete","");
                // last, so a board where audio misbehaves has already got its display up --
                // a bring-up sound that hangs before the first frame would look like a dead
                // panel rather than an audio fault
                _play_startup_chirp();
        break;
        // TODO implement unregister for event hooks
        case LF_EVENT_MODULE_UNLOAD:
        break;
        }
}

uint8_t light_ui_demo_main(struct light_application *app)
{
        //   checked first, so a shutdown requested on the previous tick ends the loop before
        // another frame is rendered. Returning the signal -- rather than the requester calling
        // exit() or similar -- is what lets light_framework_run() do its orderly unload and
        // final log flush
        if(shutdown_requested) {
                light_info("shutdown requested -- stopping the task loop");
                return LF_STATUS_SHUTDOWN;
        }

        // the console before the board's own inputs, so a command typed while a finger is on
        // the glass still lands this tick; a no-op on boards compiled without a console
        _poll_console();

        // every tick, not once per frame: the input modules' own periodic tasks run
        // independently of this app's frame rate and hold only one event at a time, so
        // collecting at frame rate could drop one
        __light_ui_demo_input_poll();

        // after the input poll, so a tick that saw activity resets the timer before it is
        // tested rather than dimming for one tick and immediately waking again
        _service_idle_backlight(light_platform_get_time_since_init());

        // also every tick -- the canvas's own pacing decides when a frame actually happens,
        // and light_ui_render() is a no-op on the ticks in between (and on any tick where
        // nothing in the tree changed)
        light_ui_render(_ui);

        return LF_STATUS_RUN;
}
