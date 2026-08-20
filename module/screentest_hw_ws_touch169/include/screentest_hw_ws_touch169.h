#ifndef _SCREENTEST_HW_WS_TOUCH169_H
#define _SCREENTEST_HW_WS_TOUCH169_H

#include <light_backlight.h>
#include <light_display.h>
#include <light_imu.h>
#include <light_touch.h>

// board wiring and device construction for the Waveshare RP2350-Touch-LCD-1.69, factored
// out of the demo application that used to own it so more than one app can be built
// against the same board without restating (or drifting from) its pinout

// ST7789 panel, SPI 4-wire on real spi1 hardware -- unlike crossfire's PIO-emulated SPI,
// this board has nothing else contending for spi1. all confirmed from the board's
// schematic + wiki pin table during bring-up
#define ST_DISPLAY_PIN_DC               8
#define ST_DISPLAY_PIN_CS               9
#define ST_DISPLAY_PIN_SCK              10
#define ST_DISPLAY_PIN_MOSI             11
#define ST_DISPLAY_PIN_RESET            13
#define ST_DISPLAY_PIN_BL               25

// tried width/height swapped (280x240) as an experiment to explain a noise strip seen
// during bring-up -- disproved, not confirmed: it made the noise strip wider and fragmented
// the test circle into a horizontally-repeating row of smaller circles (a column-address-
// wraparound signature -- 280 exceeds this panel's real native column capacity). back to
// the product spec's 240x280, which the evidence now says was correct all along
#define ST_DISPLAY_WIDTH                240
#define ST_DISPLAY_HEIGHT               280

// the glass has rounded corners, so the panel does not show its whole pixel grid: pixels in
// the corners are addressable and get drawn, they are simply never visible. nothing in the
// software can detect that, so it has to be declared -- content placed there just silently
// disappears, which is how the UI demo's title lost its first letters to the top-left corner.
//
// the radius is uniform on all four corners, which is what lets the interface rotate freely:
// anything expressed per-edge in logical coordinates would land on the wrong edges as soon as
// the board turned.
//
// MEASURED, not estimated. this was 20 for a while, guessed from the panel's physical size
// (240x280 px over 27.9x32.6 mm is about 8.6 px/mm, and a corner of this style looked like
// 2-3 mm). that guess was less than half the real value, and it was invisible in code -- a
// frame drawn to it simply had its corners swallowed by the glass.
//
// measured with screentest_calib169, which sweeps a rounded rectangle's radius at a fixed 2px
// inset and prints the current value on screen. a rect drawn at inset d with radius r is
// fully inside glass of radius R exactly when r >= R - d, so the radius at which the corners
// first close is R - 2. that transition sat between 36 and 40, and the upper end is taken
// here: too large only pulls the frame slightly off the curve, while too small clips it again
#define ST_DISPLAY_CORNER_RADIUS        42

// this panel's SPI clock, raised from light_ioport's 10MHz default. at 10MHz a full-screen
// push of 240x280x16bpp is 134400 bytes = 107ms, a 9.3fps ceiling -- enough for the static
// UI but not for animating a rotation, which needs every frame to be a full push. 40MHz
// brings that to 27ms (37fps).
//
// set per-io_context rather than by raising light_ioport's global SPI_BAUDRATE, because that
// global is shared with the SH1107 OLED rigs, which are on hardware that currently cannot be
// flashed -- re-clocking a display nobody can look at is not a change worth making.
//
// TO BE CONFIRMED ON HARDWARE: too fast shows up as corrupt pixels rather than a clean
// failure, so if the panel speckles or tears, step this back down. 40MHz is a common working
// figure for ST7789 on short traces, not a datasheet guarantee for this board
#define ST_DISPLAY_SPI_HZ               (40 * 1000 * 1000)

// CST816T touch controller -- shared I2C1 bus (also used by IMU/RTC, not yet implemented),
// confirmed from the board's schematic + wiki pin table during bring-up
#define ST_TOUCH_PIN_SDA                6
#define ST_TOUCH_PIN_SCL                7
#define ST_TOUCH_PIN_INT                21
#define ST_TOUCH_PIN_RST                22
#define ST_TOUCH_X_MAX                  ST_DISPLAY_WIDTH
#define ST_TOUCH_Y_MAX                  ST_DISPLAY_HEIGHT

// QMI8658C 6-axis IMU -- same shared I2C1 bus as the touch controller, at its own address.
// INT1/INT2 confirmed from the board's schematic + wiki pin table; INT1 is passed to the
// driver but not yet used to gate sampling (see light_imu_qmi8658.h)
#define ST_IMU_PIN_INT1                 23
#define ST_IMU_PIN_INT2                 24

// how this board mounts the QMI8658C, as the axis map light_imu rotates every sample
// through (see struct imu_axis_map). a board-MOUNTING fact rather than a driver one, so it
// lives here where a single edit corrects it -- the same treatment the display's row offset
// needed. declaring it once means BOTH orientation reporting and tilt steering come out
// right, instead of each compensating separately.
//
// CONFIRMED ON HARDWARE, using the convention that an accelerometer axis pointing UP reads
// +1g. three observations fix all three axes:
//   - lowering the right edge drove the circle DOWN, so the chip's +Y points right
//   - lowering the bottom edge drove it RIGHT, so the chip's +X points up the screen
//   - face-up was reported as face-down, so the chip's +Z points INTO the screen
// the X/Y transposition and the Z inversion corroborate each other: transposing two axes
// alone would flip handedness, which no physical mounting can do, and negating the third
// restores it. so this is a real rotation, not two independent guesses that happen to fit
#define ST_IMU_AXIS_MAP \
        ((struct imu_axis_map) { \
                .source = { IMU_AXIS_Y, IMU_AXIS_X, IMU_AXIS_Z }, \
                .sign = { 1, 1, -1 } \
        })

// maps a device orientation onto the light_draw rotation that keeps the interface upright. lives
// here rather than in light_ui because it depends on the PANEL's native orientation: this
// one is natively portrait (240x280 with LIGHT_DRAW_ROTATE_0), so portrait needs no rotation at
// all. a landscape panel would need every entry shifted by 90 degrees.
//
// FACE_UP/FACE_DOWN deliberately have no entry -- a board lying flat has no upright
// direction, and picking one would make the UI snap around whenever it was set down. the
// caller holds the current rotation for those (see the UI app).
//
// the two landscape entries were transposed on the first attempt. the derivation, since it
// is easy to get backwards and not worth doing a third time:
//
// turn the board CLOCKWISE. screen-right now points world-DOWN, and an accelerometer axis
// pointing down reads -1g, so device X goes negative -- which light_imu classifies as
// LANDSCAPE_L. for the interface to read upright in that position its logical top edge has
// to sit along the panel's LEFT edge, and LIGHT_DRAW_ROTATE_270 is the transform that puts it
// there (phys_x = y, so logical y=0 maps to phys_x=0). hence L -> 270, and R -> 90.
//
// portrait needs no rotation because this panel is natively portrait; a landscape panel
// would shift every entry by 90 degrees
#define ST_IMU_ROTATION_PORTRAIT        LIGHT_DRAW_ROTATE_0
#define ST_IMU_ROTATION_PORTRAIT_FLIP   LIGHT_DRAW_ROTATE_180
#define ST_IMU_ROTATION_LANDSCAPE_L     LIGHT_DRAW_ROTATE_270
#define ST_IMU_ROTATION_LANDSCAPE_R     LIGHT_DRAW_ROTATE_90

// the passive piezo buzzer, driven as a PWM output (see light_audio). read off the board
// schematic rather than guessed -- leaving this undefined is the safe default, and
// screentest_hw_ws_touch169_audio() returns NULL when it is, on the same terms as a board
// with no backlight.
//
// its PWM slice is deliberately not the backlight's. slices are shared between pin pairs, so
// two devices landing on one would fight over wrap and clkdiv, and the symptom -- the
// backlight flickering in time with audio -- points nowhere near the cause. pin 2 and the
// backlight's pin 25 are four slices apart; light_audio's driver logs whichever it gets, so a
// future clash shows up at init rather than as a mystery
#define ST_AUDIO_PIN_BUZZER             2

extern struct display_device *screentest_hw_ws_touch169_display(void);
extern struct touch_device *screentest_hw_ws_touch169_touch(void);
extern struct imu_device *screentest_hw_ws_touch169_imu(void);
extern struct backlight_device *screentest_hw_ws_touch169_backlight(void);
// NULL until ST_AUDIO_PIN_BUZZER is defined above
extern struct audio_device *screentest_hw_ws_touch169_audio(void);

#endif
