#include <light_ui_demo.h>
#include <light_backlight.h>
#include <light_imu.h>
#include <light_touch.h>
#include <module/mod_light_audio.h>
#include <module/mod_light_backlight.h>
#include <module/mod_light_imu.h>
#include <module/mod_light_touch.h>
#include <module/mod_light_display.h>
#include <module/mod_light_ui.h>
#include <module/mod_light_cli.h>
#include <TypeLightSans_ttf_16px_font.h>

// reset_usb_boot(), for the terminal state main() enters once the framework has wound down
#include <pico/bootrom.h>

// app: light_ui_demo_touch169
// the shared light_ui demo on the RP2350-Touch-LCD-1.69, driven by tapping buttons
// directly. no push-buttons and no light_button dependency -- this is the other half of the
// pair, proving the same widget tree works from either input path

// named per-app rather than after the shared demo: this define lives in each app precisely
// so it can name its own dependencies, and the name it gives the application is what
// light_module_get_name() reports in the log, so it should say which binary is running
Light_Application_Define(light_ui_demo_touch169, light_ui_demo_event, light_ui_demo_main,
                                &light_draw,
                                &light_display,
                                &light_ui,
                                &light_touch,
                                &light_imu,
                                &light_audio,
                                &light_backlight,
                                &light_cli,
                                &light_core);

static struct touch_device *_touch_main;
// this app's own, not shared through light_ui_demo.h: the shared demo body never touches
// the IMU, only this board's orientation wiring does
static struct imu_device *_imu_main;

void main(int argc, char **argv)
{
        light_framework_init();
        //   (0, NULL), not (argc, argv): this is a bare-metal entry point and the runtime never
        // sets those, so they hold whatever happened to be in the argument registers. Passing
        // that on was harmless while nothing read it, but light_cli parses it -- and garbage
        // argc is indistinguishable from a real command line. Zero means "no command line",
        // which is what lets the baked boot command take over
        light_framework_run(0, NULL);

        //   only reachable through the shutdown command: the framework has unloaded its
        // modules and flushed its final logging (the core 1 USB worker drains before it
        // finishes), so all that is left is to darken the panel and pick a terminal state.
        //
        //   BOOTSEL, not a halt, and deliberately the same choice as light_core_port_abort():
        // this chip has no power-off, and a halted board no longer serves the USB stack the
        // 1200-baud reflash depends on -- on a board with no exposed SWD pads that means a
        // hand on the physical BOOT button every time. In BOOTSEL the panel is dark, the
        // firmware is stopped, and the next flash needs nothing pressed.
        if(_backlight_main)
                light_backlight_set_level(_backlight_main, 0);
        reset_usb_boot(0, 0);
}

const light_draw_font_t *__light_ui_demo_font(void)
{
        // 16px here, where the po13 rig needs 8px: this panel is 240x280, so a 12x19 glyph
        // still leaves twenty characters per row and four comfortable button rows
        return &TypeLightSans_ttf_16px_font;
}

void __light_ui_demo_hardware_init(void)
{
        _display[0] = screentest_hw_ws_touch169_display();
        _touch_main = screentest_hw_ws_touch169_touch();
        _imu_main = screentest_hw_ws_touch169_imu();
        _backlight_main = screentest_hw_ws_touch169_backlight();
        // NULL until the board header names the buzzer's pin, which every use in the shared
        // demo already guards for
        _audio_main = screentest_hw_ws_touch169_audio();
}

// keeps the interface upright as the board is turned. light_ui knows nothing about IMUs --
// it takes a rotation -- so the translation lives here, and the board header owns the actual
// orientation-to-rotation table because it depends on the panel's native orientation
static void _poll_orientation(void)
{
        uint8_t orientation;
        if(!_imu_main || !light_imu_take_orientation(_imu_main, &orientation))
                return;

        // a settled orientation report means the board was picked up and turned, which is
        // someone handling it -- so it counts as activity even for the flat orientations
        // below that deliberately leave the rotation alone
        light_ui_demo_note_activity();

        uint8_t rotation;
        switch(orientation) {
        case IMU_ORIENT_PORTRAIT:       rotation = ST_IMU_ROTATION_PORTRAIT;      break;
        case IMU_ORIENT_PORTRAIT_FLIP:  rotation = ST_IMU_ROTATION_PORTRAIT_FLIP; break;
        case IMU_ORIENT_LANDSCAPE_L:    rotation = ST_IMU_ROTATION_LANDSCAPE_L;   break;
        case IMU_ORIENT_LANDSCAPE_R:    rotation = ST_IMU_ROTATION_LANDSCAPE_R;   break;
        default:
                // FACE_UP/FACE_DOWN: the board is flat and has no upright direction, so
                // hold whatever rotation it had rather than snapping to a default every
                // time it is set down
                light_info("orientation %d (flat) -- holding rotation", orientation);
                return;
        }
        // logged because the orientation-to-rotation table is the part most likely to need
        // correcting on a new board, and guessing from how the screen looks is slower than
        // reading which value produced which rotation
        light_info("orientation %d -> rotation %d", orientation, rotation);
        light_ui_set_rotation(_ui, rotation);
}

void __light_ui_demo_input_poll(void)
{
        _poll_orientation();

        if(!_touch_main)
                return;

        //   light_touch's own periodic task keeps touch_active/x/y current, so this reads the
        // state it maintains rather than polling the controller a second time -- the
        // CST816T only answers for a short window after asserting its interrupt line, so a
        // redundant poll would mostly just return nothing anyway.
        //
        //   the panel's own coordinates go straight through: light_ui_input_touch() takes
        // them in the display's physical frame and untransforms them itself, which is what
        // keeps touches landing on the right widget once the UI has been rotated.
        //
        //   the tracker runs the whole tap-versus-drag interaction: a tap activates on
        // release (the old down-edge press had to go -- with scrollable content it fired on
        // every drag's first contact), and a drag over a scrollable window scrolls it to
        // follow the finger. this app's part is one rule: a drag that scrolled has SPENT the
        // finger's movement, so the touch is claimed before its release can classify as a
        // swipe and navigate as well
        uint8_t touch_state = light_ui_input_touch(_ui,
                        _touch_main->x, _touch_main->y, _touch_main->touch_active);
        if(touch_state != UI_TOUCH_NONE) {
                // any touch involvement is activity -- tapping a blank part of a dimmed
                // screen is still someone asking for it, and a long drag keeps the panel
                // awake for as long as the finger is down
                light_ui_demo_note_activity();
        }
        if(touch_state == UI_TOUCH_DRAG)
                light_touch_suppress_gesture(_touch_main);

        //   swipe right returns to the previous page. light_ui knows nothing about gestures --
        // the mapping from this board's touch controller onto navigation is an application
        // fact, the same as the tap wiring above and the IMU-to-rotation wiring.
        //
        //   taken unconditionally rather than only when a page is showing, so a gesture is
        // never left queued to fire later. navigate_back() answers false at the top of the
        // tree, which is exactly the case where the swipe should mean nothing
        //   the gesture's ENDPOINTS are what matter, not its type. light_touch classifies in
        // the panel's frame, which is fixed to the glass, while the swipe was made relative to
        // the interface, which rotates with the board -- so TOUCH_GESTURE_SWIPE_RIGHT means
        // "right" only at LIGHT_DRAW_ROTATE_0, and taking it at face value made this work in portrait
        // and act on the wrong axis in landscape. light_ui_swipe_direction() untransforms both
        // ends through the same path a tap takes and answers in the frame the user is using
        struct touch_gesture gesture;
        if(light_touch_take_gesture(_touch_main, &gesture)) {
                light_ui_demo_note_activity();
                uint8_t dir = light_ui_swipe_direction(_ui, gesture.start_x, gesture.start_y,
                                                gesture.end_x, gesture.end_y);
                if(dir == UI_SWIPE_RIGHT && light_ui_navigate_back(_ui))
                        light_debug("swipe: returned to the previous page");
        }
}
