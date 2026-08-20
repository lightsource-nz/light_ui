#ifndef _LIGHT_UI_DEMO_H
#define _LIGHT_UI_DEMO_H

// every app that uses this shared demo supplies a light_ui_demo_config.h naming ONLY what
// its board does differently. everything below is a default, applied where the config was
// silent.
//
// this used to work the other way round: an app shadowed the whole header by putting its own
// copy earlier on the include path. that made adding any shared setting a two-place edit
// with nothing to catch a missed one but a build failure, and it relied on the app's include
// directory happening to be listed before this module's, which nothing enforced.
//
// the header is named for the module rather than for screen-test because these apps are
// named for what they demonstrate. it cannot be light_ui.h -- that would shadow the
// library's own public header on the include path, which is the trap this arrangement exists
// to avoid rather than repeat
#include <light_ui_demo_config.h>

#include <light.h>
#include <light_audio.h>
#include <light_backlight.h>
#include <light_cli.h>
#include <light_display.h>
#include <light_ui.h>
#include <light_draw.h>

#include <stdint.h>

#ifndef LIGHT_UI_DEMO_DISPLAY_COUNT
#define LIGHT_UI_DEMO_DISPLAY_COUNT     1
#endif

// render context geometry. the defaults are the small portrait OLED rigs'
#ifndef LIGHT_UI_DEMO_RENDER_WIDTH
#define LIGHT_UI_DEMO_RENDER_WIDTH      64
#endif
#ifndef LIGHT_UI_DEMO_RENDER_HEIGHT
#define LIGHT_UI_DEMO_RENDER_HEIGHT     128
#endif
#ifndef LIGHT_UI_DEMO_RENDER_BPP
#define LIGHT_UI_DEMO_RENDER_BPP        1
#endif
#ifndef LIGHT_UI_DEMO_RENDER_ROTATION
#define LIGHT_UI_DEMO_RENDER_ROTATION   LIGHT_DRAW_ROTATE_90
#endif

#ifndef LIGHT_UI_DEMO_TITLE
#define LIGHT_UI_DEMO_TITLE             "light_ui"
#endif
// pixels kept clear on every edge, for a panel that doesn't show its whole pixel grid.
// defaults to 0 for the square-cornered OLED rigs. a board with rounded glass no longer needs
// this to equal its corner radius -- that squared off a band the width of the radius on all
// four sides. it sets LIGHT_UI_DEMO_CORNER_RADIUS instead and leaves this as a small breathing
// margin (see light_ui_set_safe_inset())
#ifndef LIGHT_UI_DEMO_SAFE_INSET
#define LIGHT_UI_DEMO_SAFE_INSET        0
#endif
// corner radius of the root window's frame, 0 for a square one. a board with rounded glass
// sets this so the frame follows the curve rather than floating in a square inside it.
//
// it is the PANEL's radius less the safe inset, not the panel's radius: insetting a rounded
// rectangle uniformly by d leaves a rounded rectangle of radius r-d, because the arc centres
// do not move. using the panel's own radius here would bow the corners further out than the
// glass does (see light_ui_window_set_corner_radius())
#ifndef LIGHT_UI_DEMO_CORNER_RADIUS
#define LIGHT_UI_DEMO_CORNER_RADIUS     0
#endif
// pixels between stacked button rows
//   whether the demo shows the page-navigation example: a fourth row on the main page that
// transfers to a second page, which returns with a swipe. Off by default because it costs a
// row, and the 64x128 OLED has 64 logical pixels of height to divide between them -- a fourth
// would leave rows barely taller than the font. A board with room turns it on
#ifndef LIGHT_UI_DEMO_PAGES
#define LIGHT_UI_DEMO_PAGES             0
#endif

#ifndef LIGHT_UI_DEMO_ROW_GAP
#define LIGHT_UI_DEMO_ROW_GAP           2
#endif
//   the minimum row height for the scrolling-list page (pages builds only). This is the knob
// that makes the list OVERFLOW: rows pinned at this height need more room than the window
// has, and the excess is what the scrolling demonstrates. Sized per board because it is a
// touch-target/legibility fact -- a value that overflows a 240x280 panel would still fit a
// taller one
#ifndef LIGHT_UI_DEMO_LIST_MIN_ROW
#define LIGHT_UI_DEMO_LIST_MIN_ROW      18
#endif
// how often the UI is offered a chance to repaint. light_ui_render() is a no-op unless
// something actually changed, so this bounds latency after an input rather than describing a
// steady redraw load
#ifndef LIGHT_UI_DEMO_FRAME_RATE
#define LIGHT_UI_DEMO_FRAME_RATE        24
#endif

// how long without input before the backlight dims, and the levels and fade times either
// side of it. dimming rather than blanking: the UI stays readable, so it reads as the device
// resting rather than switching off
#ifndef LIGHT_UI_DEMO_IDLE_MS
#define LIGHT_UI_DEMO_IDLE_MS           8000
#endif
#ifndef LIGHT_UI_DEMO_BACKLIGHT_FULL
#define LIGHT_UI_DEMO_BACKLIGHT_FULL    LIGHT_BACKLIGHT_LEVEL_MAX
#endif
#ifndef LIGHT_UI_DEMO_BACKLIGHT_DIM
#define LIGHT_UI_DEMO_BACKLIGHT_DIM     150
#endif
// waking is faster than dimming on purpose -- a slow fade down is unobtrusive, a slow fade
// up feels unresponsive to the touch that asked for it
#ifndef LIGHT_UI_DEMO_FADE_DOWN_MS
#define LIGHT_UI_DEMO_FADE_DOWN_MS      400
#endif
#ifndef LIGHT_UI_DEMO_FADE_UP_MS
#define LIGHT_UI_DEMO_FADE_UP_MS        150
#endif

// the click a button press makes. deliberately short -- feedback, not a notification -- and
// near a small piezo's resonance, where it is loudest for the same drive
#ifndef LIGHT_UI_DEMO_CLICK_HZ
#define LIGHT_UI_DEMO_CLICK_HZ          2700
#endif
#ifndef LIGHT_UI_DEMO_CLICK_MS
#define LIGHT_UI_DEMO_CLICK_MS          25
#endif
// a short PCM chirp at startup, which exists to exercise the sample path rather than to be
// heard: tone and PCM are entirely separate code paths through the driver, and a board where
// the click works but the chirp does not localises the fault to the DAC/DMA side.
//
// "rather than to be heard" is literal on the RP2350 touch board. Its piezo does not
// reproduce this at any amplitude up to and including full scale -- a piezo is a sharply
// resonant device, and a duty-modulated carrier it has to demodulate for itself delivers far
// less energy to it than the tone path's square wave at its resonance does. The path is
// confirmed by light_audio's "playback finished after N ms" line matching the sample count,
// not by ear. A board with a real speaker is where this becomes an audible test
#ifndef LIGHT_UI_DEMO_CHIRP_RATE
#define LIGHT_UI_DEMO_CHIRP_RATE        22050
#endif
#ifndef LIGHT_UI_DEMO_CHIRP_MS
#define LIGHT_UI_DEMO_CHIRP_MS          120
#endif
// peak sample value the chirp starts at, before its decay. roughly three quarters of full
// scale, which leaves a few dB of headroom -- the right default for a synthesised test signal
// on hardware whose gain is unknown, and specifically not clipping on a board that puts an
// amplifier after the pin.
//
// this was briefly set to full scale on the theory that a rail-to-rail swing would let the
// piezo on the RP2350 touch board hear the PCM path. It made no audible difference, so that
// reasoning did not survive contact with the hardware and there is nothing to be gained by
// defaulting to the maximum. See the note on LIGHT_UI_DEMO_CHIRP_RATE: on that board the
// sample path is verified by instrumentation rather than by ear
#ifndef LIGHT_UI_DEMO_CHIRP_AMPLITUDE
#define LIGHT_UI_DEMO_CHIRP_AMPLITUDE   24000
#endif

extern struct display_device *_display[LIGHT_UI_DEMO_DISPLAY_COUNT];
extern struct ui_context *_ui;
// NULL on boards with no controllable backlight, on the same terms as _touch_main in the
// circle demo -- the idle behaviour is simply skipped there
extern struct backlight_device *_backlight_main;
// NULL on boards with no buzzer, and on this board until ST_AUDIO_PIN_BUZZER is filled in.
// every use is guarded, so a silent board is a working board
extern struct audio_device *_audio_main;

// called by an app whenever it sees user input, to hold off (or undo) the idle dim. the
// shared demo owns the timer; only the app knows what counts as input on its board
extern void light_ui_demo_note_activity(void);

//   asks the demo's periodic task to return LF_STATUS_SHUTDOWN on its next tick, which is what
// ends light_framework_run()'s scheduler loop and starts the orderly unload. A request rather
// than a direct return because the natural caller is a command handler -- and a command runs
// inside cli_task(), whose return value belongs to the CLI, not to the command that asked.
// What the board does once the framework has wound down (dark panel, BOOTSEL, plain halt) is
// each app's own business, in its main() after light_framework_run() returns
extern void light_ui_demo_request_shutdown(void);

//   the shared demo's root command -- "light_ui_demo", matching the first token of
// LIGHT_BOOT_COMMAND. The shared subcommands (backlight, shutdown, help) live in the common
// app.c beside it; an app with board-specific commands hangs them off this root, and the
// interactive console (on boards that have one) and the baked boot command both dispatch
// against it
Light_Command_Declare(cmd_light_ui_demo, &root_command);

// --- provided by light_ui_demo_common, referenced by each app's Light_Application_Define ---
// the module dependency list has to name the input modules the board actually has
// (light_button, light_touch, or neither), so each app owns its own application define and
// main(); everything else about the demo is shared
extern void light_ui_demo_event(const struct light_module *module, uint8_t event, void *arg);
extern uint8_t light_ui_demo_main(struct light_application *app);

// --- provided by each app ---
// creates the board's display (into _display[]) and whatever input devices it has
extern void __light_ui_demo_hardware_init(void);
// called every tick: translate this board's input devices onto light_ui's hardware-free
// input entry points. a handful of lines either way -- see the two UI apps for the
// two-button and the touch wiring
extern void __light_ui_demo_input_poll(void);
// the font labels are rendered in. rendered at build time by crush, whose generated symbol
// name embeds the pixel size -- and the right pixel size differs per panel -- so the font
// cannot be named by shared code
extern const light_draw_font_t *__light_ui_demo_font(void);

#endif
