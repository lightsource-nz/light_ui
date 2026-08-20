#ifndef _LIGHT_TOUCH_CST816T_H
#define _LIGHT_TOUCH_CST816T_H

#include <light_touch.h>

#include <stdint.h>

// register map cross-referenced from two independent open-source drivers (koendv/cst816t
// and Espressif's esp_lcd_touch_cst816s), which agree on this core layout -- not from a
// primary datasheet (the Waveshare CST816S PDF didn't extract cleanly), so treat this as
// informed-but-unverified until confirmed on real hardware, same as every other new
// peripheral in this project
#define CST816T_REG_CHIP_ID             0xA7
#define CST816T_CHIP_ID                 0xB5    // 0xB4/0xB6 are the sibling S/D variants

// the controller classifies gestures itself and reports the result here. this register
// sits immediately before the touch data block, so a single burst starting at it picks up
// both -- cheaper than a second transaction, and it guarantees the gesture and the
// coordinates come from the same frame rather than straddling two.
//
// burst layout from CST816T_REG_GESTURE: GestureID, FingerNum (point count), XposH (low
// nibble = X high bits), XposL, YposH (low nibble = Y high bits), YposL -- 6 bytes total
#define CST816T_REG_GESTURE             0x01
#define CST816T_REG_TOUCH_DATA          0x02
#define CST816T_TOUCH_DATA_LEN          6

// native gesture codes, from the same two open-source drivers as the rest of this map and
// carrying the same caveat -- unverified against a primary datasheet. the click and
// long-press codes have no light_touch equivalent and are deliberately not mapped.
//
// NOTE the two vertical names below do NOT match observed behaviour: on hardware, the code
// named SWIPE_UP is reported for a downward swipe and vice versa, while the horizontal
// pair is correct. the names are kept as the reference drivers have them so this map still
// cross-references against them; the correction is applied where they're translated, in
// _cst816t_read_gesture()
#define CST816T_GESTURE_NONE            0x00
#define CST816T_GESTURE_SWIPE_UP        0x01
#define CST816T_GESTURE_SWIPE_DOWN      0x02
#define CST816T_GESTURE_SWIPE_LEFT      0x03
#define CST816T_GESTURE_SWIPE_RIGHT     0x04
#define CST816T_GESTURE_SINGLE_CLICK    0x05
#define CST816T_GESTURE_DOUBLE_CLICK    0x0B
#define CST816T_GESTURE_LONG_PRESS      0x0C

// this controller only responds to I2C for a short window after asserting its
// (active-low) INT line -- default I2C slave address, per koendv/cst816t
#define CST816T_I2C_ADDR                0x15

extern struct touch_driver *light_touch_driver_cst816t();

// pin_int is the controller's own interrupt line (TP_INT), read directly as a GPIO --
// there's no equivalent in light_ioport's io_context (displays don't have
// interrupt pins), so it's tracked here in the driver's own state instead
extern struct touch_device *light_touch_cst816t_create_device(
        uint8_t *name, uint16_t x_max, uint16_t y_max, struct io_context *io, uint8_t pin_int);

#endif
