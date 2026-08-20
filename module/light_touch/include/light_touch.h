#ifndef _LIGHT_TOUCH_H
#define _LIGHT_TOUCH_H

#include <light.h>
#include <light_ioport.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#define ID_TOUCH_DEVICE_ROOT                    "light_touch:device_root"
#define ID_TOUCH_DEVICE                         "light_touch:device"

#define LIGHT_TOUCH_MAX_DEVICES                 8

// recognised gesture kinds. directions are expressed in the touch device's OWN coordinate
// space, which is the panel's physical orientation: SWIPE_UP means the finger travelled
// toward y=0, SWIPE_LEFT toward x=0. an app whose render context is rotated
// (light_draw_context_set_rotation()) draws in a different space and is responsible for mapping
// these onto it -- light_touch has no render context to consult
#define TOUCH_GESTURE_NONE                      0
#define TOUCH_GESTURE_SWIPE_UP                  1
#define TOUCH_GESTURE_SWIPE_DOWN                2
#define TOUCH_GESTURE_SWIPE_LEFT                3
#define TOUCH_GESTURE_SWIPE_RIGHT               4

struct touch_gesture {
        // one of TOUCH_GESTURE_* above
        uint8_t type;
        // where the finger went down, and where it lifted -- both in device coordinates
        uint16_t start_x;
        uint16_t start_y;
        uint16_t end_x;
        uint16_t end_y;
        // true when the controller's own gesture engine classified this, false when
        // light_touch did it from the sampled coordinates. informational -- the two are
        // reported identically otherwise -- but it makes it obvious whether a controller's
        // hardware recognition is actually doing anything
        bool from_hardware;
};

struct touch_device;
struct touch_driver
{
        const uint8_t *name;
        struct touch_driver_context *(*spawn_context)();
        //   frees whatever spawn_context() allocated. Called when the device holding that
        // context is released, so a context outlives exactly the device it was spawned for.
        // OPTIONAL: a driver whose context is not heap-allocated leaves this NULL and the
        // release path skips it
        void (*destroy_context)(struct touch_driver_context *ctx);
        void (*init_device)(struct touch_device *);
        void (*reset)(struct touch_device *);
        // samples the controller for a new touch state, writing into dev->touch_active/
        // x/y. returns true if a new sample was captured, false if there was nothing new
        // to report (e.g. the controller's interrupt line wasn't asserted). a plain
        // synchronous call, not an async/DMA trio like light_display's update -- an I2C
        // touch-data read is a handful of bytes, nothing like a display frame transfer
        bool (*poll)(struct touch_device *);
        // optional -- NULL when the controller has no gesture engine of its own. asked
        // once, as a touch ends, for the controller's own classification of it as a
        // TOUCH_GESTURE_* code. return false to decline, and light_touch classifies the
        // touch itself from the coordinates it sampled; that's also the right answer for a
        // gesture the controller recognises but light_touch has no equivalent for.
        //
        // a pure query: light_touch may call it without a gesture having occurred, and the
        // driver is responsible for not reporting a stale result from an earlier touch.
        // note that controllers which classify in hardware still don't report WHERE the
        // gesture happened, so its start and end points come from light_touch's own
        // tracking regardless of which path classified it
        bool (*read_gesture)(struct touch_device *, uint8_t *type_out);
};
struct touch_driver_context
{
        const struct touch_driver *driver;
        const void *state;
};

struct touch_device {
        struct light_object header;
        uint8_t device_id;
        // coordinate range the controller reports within -- analogous to display's
        // width/height, but there's no bpp/render_ctx equivalent: touch devices don't
        // own a pixel buffer
        uint16_t x_max;
        uint16_t y_max;
        // last-sampled state, updated by poll()
        bool touch_active;
        uint16_t x;
        uint16_t y;
        struct touch_driver_context *driver_ctx;

        // --- gesture recognition, driven from light_touch_command_poll() ---
        // how far the finger must travel along the dominant axis for a drag to count as a
        // swipe, in device coordinates. defaults to an eighth of the device's shorter
        // axis, so it scales with panel size; override per device if that doesn't suit
        uint16_t swipe_min_distance;
        // set while a touch is in progress, i.e. between a down and its matching release
        bool gesture_tracking;
        uint16_t gesture_start_x;
        uint16_t gesture_start_y;
        // most recent position seen while tracking -- the release sample itself usually
        // carries no coordinates, so this is what the gesture's end point comes from
        uint16_t gesture_last_x;
        uint16_t gesture_last_y;
        // type is TOUCH_GESTURE_NONE when nothing is waiting to be collected
        struct touch_gesture gesture_pending;
        //   set by light_touch_suppress_gesture() while a touch is in progress, cleared as
        // the next touch begins: the release of a suppressed touch is classified as nothing
        // at all. see the function below for why a consumer needs this
        bool gesture_suppressed;
};
struct touch_device_root {
        struct light_object header;
        struct touch_device *device[LIGHT_TOUCH_MAX_DEVICES];
};

#define to_touch_device_root(ptr) container_of(ptr, struct touch_device_root, header)
#define to_touch_device(ptr) container_of(ptr, struct touch_device, header)

extern void light_touch_init();

extern struct touch_device_root *light_touch_device_get_root();
extern struct touch_device *light_touch_create_device(struct touch_driver *driver,
                                                uint16_t x_max, uint16_t y_max, uint8_t *format, ...);
extern struct touch_device *light_touch_create_device_va(struct touch_driver *driver,
                                                uint16_t x_max, uint16_t y_max, uint8_t *format, va_list args);
// lower-level entry point, same rationale as light_display_init_device(): a driver's own
// state (e.g. its io_context) must be attached to driver_ctx before the device is added
// to the object tree, since that add synchronously triggers init_device() -- too late to
// set driver-private state first if using the one-shot create_device() above
extern struct touch_device *light_touch_init_device(
                struct touch_device *dev,
                struct touch_driver_context *driver_ctx,
                uint16_t x_max, uint16_t y_max,
                uint8_t *format, ...);
extern struct touch_device *light_touch_init_device_va(
                struct touch_device *dev,
                struct touch_driver_context *driver_ctx,
                uint16_t x_max, uint16_t y_max,
                uint8_t *format, va_list args);
extern void light_touch_command_init(struct touch_device *dev);
extern void light_touch_command_reset(struct touch_device *dev);
// polls the device for a new sample. returns true and fills x_out/y_out (if non-NULL)
// when a new touch was captured; returns false otherwise (nothing new since the last
// poll). x_out/y_out are left untouched on a false return
extern bool light_touch_command_poll(struct touch_device *dev, uint16_t *x_out, uint16_t *y_out);

// collects the pending gesture and clears it, so each recognised gesture is reported
// exactly once however often this is called. returns false and leaves *out untouched when
// nothing is pending.
//
// a gesture is recognised when the finger LIFTS, not partway through the drag -- so the
// reported start and end points are both final. that does mean nothing is reported until
// the touch ends, and that it depends on the controller reporting the release at all
// (CST816T does).
//
// only one gesture is held at a time: if a second completes before the first is collected,
// the first is discarded rather than queued. poll often enough that this doesn't matter,
// which for a periodic-task-driven app it will be
extern bool light_touch_take_gesture(struct touch_device *dev, struct touch_gesture *out);
// overrides the swipe travel threshold (see swipe_min_distance above). a driver-level
// preference, not controller state, so it persists across light_touch_command_reset()
extern void light_touch_set_swipe_min_distance(struct touch_device *dev, uint16_t distance);

//   marks the touch CURRENTLY in progress as claimed, so its release is classified as no
// gesture at all. cleared automatically when the next touch begins; a call with no touch in
// progress does nothing.
//
//   this exists because the gesture pipeline and a drag consumer read the same finger: a drag
// that scrolled a panel still ends in a release, and that release would otherwise be reported
// as a swipe -- which an application typically maps onto navigation, so every horizontal
// scroll would also navigate back. the consumer that used the movement is the only party that
// knows it was used, so suppression is its call to make, made DURING the touch rather than by
// racing to discard the gesture after it lands
extern void light_touch_suppress_gesture(struct touch_device *dev);

#endif
