#ifndef _LIGHT_UI_DEMO_CONFIG_H
#define _LIGHT_UI_DEMO_CONFIG_H

// board pinout and device construction (ST_DISPLAY_*, ST_TOUCH_*, ST_IMU_*)
#include <screentest_hw_ws_touch169.h>

// ONLY what this board does differently from light_ui_demo_common's defaults. the idle backlight
// behaviour, frame rate and title are all shared, so changing one is a single edit

// this panel's real geometry rather than the small OLED rigs' 64x128 1bpp. no rotation at
// rest: the ST7789 driver already declares width/height in the panel's native orientation,
// so logical and physical coincide -- light_ui rotates from here as the board is turned
#define LIGHT_UI_DEMO_RENDER_WIDTH      ST_DISPLAY_WIDTH
#define LIGHT_UI_DEMO_RENDER_HEIGHT     ST_DISPLAY_HEIGHT
#define LIGHT_UI_DEMO_RENDER_BPP        16
#define LIGHT_UI_DEMO_RENDER_ROTATION   LIGHT_DRAW_ROTATE_0

// a roomier gap than the OLED rigs' 2px: rows here are ~60px tall, so 2px would read as the
// buttons being fused together
#define LIGHT_UI_DEMO_ROW_GAP           6

// 240x280 has room for a fourth row, and this board has the touch panel the swipe-to-return
// half of the navigation example needs
#define LIGHT_UI_DEMO_PAGES             1

// this glass has rounded corners and does not show its whole pixel grid -- see
// ST_DISPLAY_CORNER_RADIUS. the OLED rigs are square-cornered, hence the shared defaults of 0.
//
// the inset used to be the full corner radius, which kept content safe by giving up a 20px
// band on every edge and left a square frame sitting inside round glass. now the frame itself
// is rounded and drawn near the panel edge, so the inset is only a breathing margin and the
// curve is carried by the corner radius instead
#define LIGHT_UI_DEMO_SAFE_INSET        2
#define LIGHT_UI_DEMO_CORNER_RADIUS     (ST_DISPLAY_CORNER_RADIUS - LIGHT_UI_DEMO_SAFE_INSET)
// tall enough for a comfortable touch target, and chosen so the list page's eight rows need
// ~500px of a ~200px viewport -- most of the list is off-screen, which is what the scrolling
// is there to prove
#define LIGHT_UI_DEMO_LIST_MIN_ROW      56

#endif
