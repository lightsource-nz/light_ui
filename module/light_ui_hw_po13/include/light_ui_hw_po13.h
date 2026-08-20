#ifndef _LIGHT_UI_HW_PO13_H
#define _LIGHT_UI_HW_PO13_H

#include <light_display.h>
#include <light_display_po13.h>

// board wiring and device construction for the Pico-OLED-1.3 rig, factored out of the demo
// application that used to own it so more than one app can be built against the same board
// without restating (or drifting from) its pinout. the panel's own pins live in
// light_display_po13.h (PO13_PIN_*) -- only what this RIG adds on top of a bare board is
// here.
//
// the render geometry this board wants (64x128, 1bpp, LIGHT_DRAW_ROTATE_90) is already
// screentest_common's screentest.h default, so there is deliberately no ST_RENDER_*
// override here

// second SH1107 panel on spi0, present on the two-display circle-demo rig
#define ST_DISPLAY_1_PIN_DC             16
#define ST_DISPLAY_1_PIN_CS             17
#define ST_DISPLAY_1_PIN_SCK            18
#define ST_DISPLAY_1_PIN_TX             19
#define ST_DISPLAY_1_PIN_RESET          20

// the Pico-OLED-1.3's two onboard user keys, both wired to ground through the switch with
// no external pull -- i.e. active low, relying on the RP2040's internal pull-up.
//
// NOTE: KEY1 collides with ST_DISPLAY_1_PIN_CS above. an application can have the second
// SH1107 panel or the second key, not both, which is why the UI demo app runs single-
// display. TO BE CONFIRMED against the board wiki/schematic on first bring-up, the same way
// the ST7789 rig's pins were -- if these are wrong, this is the only place to correct them
#define ST_BUTTON_PIN_KEY0              15
#define ST_BUTTON_PIN_KEY1              17

// main panel: the Pico-OLED-1.3 itself, on its own board-default pins over spi1
extern struct display_device *light_ui_hw_po13_display_main(void);
// secondary panel: a bare SH1107 of the same geometry on spi0. separate from the main
// panel's constructor so an app that wants one display doesn't get the second one wired up
// (and doesn't lose GP17 to it -- see the key-pin note above)
extern struct display_device *light_ui_hw_po13_display_sec(void);

#endif
