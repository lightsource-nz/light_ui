#ifndef _LIGHT_UI_DEMO_CONFIG_H
#define _LIGHT_UI_DEMO_CONFIG_H

// board config for the Pico-OLED-1.3 UI rig.
//
// deliberately empty: light_ui_demo_common's defaults (64x128, 1bpp, LIGHT_DRAW_ROTATE_90, one display)
// are exactly this board's geometry, and it has no backlight to dim. the file still exists
// rather than being optional, because every app providing one makes the set of them
// greppable -- an app that needs no overrides is worth seeing at a glance rather than
// inferring from an absence

#endif
