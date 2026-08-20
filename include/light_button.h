#ifndef _LIGHT_BUTTON_H
#define _LIGHT_BUTTON_H

#include <light.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#define ID_BUTTON_DEVICE_ROOT                   "light_button:device_root"
#define ID_BUTTON_DEVICE                        "light_button:device"

#define LIGHT_BUTTON_MAX_DEVICES                16

// default debounce window, in ms. long enough to swallow the contact bounce of the cheap
// tactile switches these boards use (typically under 5ms, but specified as high as 10),
// short enough that a deliberate press still feels immediate
#define LIGHT_BUTTON_DEBOUNCE_MS_DEFAULT        20

// events reported by light_button_take_event(). edges only -- a held button produces one
// PRESS, not a stream of them; the held state is available as dev->state for anything that
// needs the level rather than the transition
#define BUTTON_EVENT_NONE                       0
#define BUTTON_EVENT_PRESS                      1
#define BUTTON_EVENT_RELEASE                    2

struct button_device;
struct button_driver
{
        const uint8_t *name;
        struct button_driver_context *(*spawn_context)();
        //   frees whatever spawn_context() allocated. Called when the device holding that
        // context is released, so a context outlives exactly the device it was spawned for.
        // OPTIONAL: a driver whose context is not heap-allocated leaves this NULL and the
        // release path skips it
        void (*destroy_context)(struct button_driver_context *ctx);
        void (*init_device)(struct button_device *);
        void (*reset)(struct button_device *);
        // reads the button's RAW, undebounced level: true when pressed. debouncing is
        // light_button's job, not the driver's -- a driver only has to answer "is the
        // contact closed right now", and every driver would otherwise reimplement the same
        // timer logic (the same reasoning that moved the async update state machine out of
        // light_display's drivers and into light_display itself)
        bool (*read)(struct button_device *);
};
struct button_driver_context
{
        const struct button_driver *driver;
        const void *state;
};

struct button_device {
        struct light_object header;
        uint8_t device_id;
        // debounced level -- what callers should treat as the button's actual state
        bool state;
        struct button_driver_context *driver_ctx;

        // --- debounce, driven from light_button_command_poll() ---
        // most recent raw level seen, and when it last differed from the one before it. a
        // raw change starts a fresh debounce_ms window; `state` only follows once the raw
        // level has held unchanged for that whole window, so bounce (which by definition
        // keeps changing) never gets through
        bool raw_last;
        uint32_t raw_change_ms;
        uint16_t debounce_ms;
        // BUTTON_EVENT_NONE when nothing is waiting to be collected
        uint8_t event_pending;
};
struct button_device_root {
        struct light_object header;
        struct button_device *device[LIGHT_BUTTON_MAX_DEVICES];
};

#define to_button_device_root(ptr) container_of(ptr, struct button_device_root, header)
#define to_button_device(ptr) container_of(ptr, struct button_device, header)

extern void light_button_init();

extern struct button_device_root *light_button_device_get_root();
extern struct button_device *light_button_create_device(struct button_driver *driver,
                                                uint8_t *format, ...);
extern struct button_device *light_button_create_device_va(struct button_driver *driver,
                                                uint8_t *format, va_list args);
// lower-level entry point, same rationale as light_touch_init_device(): a driver's own
// state (e.g. which GPIO to read) must be attached to driver_ctx before the device is added
// to the object tree, since that add synchronously triggers init_device() -- too late to
// set driver-private state first if using the one-shot create_device() above
extern struct button_device *light_button_init_device(
                struct button_device *dev,
                struct button_driver_context *driver_ctx,
                uint8_t *format, ...);
extern struct button_device *light_button_init_device_va(
                struct button_device *dev,
                struct button_driver_context *driver_ctx,
                uint8_t *format, va_list args);
extern void light_button_command_init(struct button_device *dev);
extern void light_button_command_reset(struct button_device *dev);
// samples the raw level and advances the debounce state machine, latching an event if the
// debounced state changed. returns true when it did. safe (and expected) to call far more
// often than the button actually changes
extern bool light_button_command_poll(struct button_device *dev);

// collects the pending event and clears it, so each edge is reported exactly once however
// often this is called. returns false and leaves *type_out untouched when nothing is
// pending.
//
// only one event is held at a time: if a second edge lands before the first is collected,
// the first is discarded rather than queued. that is a deliberate match for
// light_touch_take_gesture()'s contract -- poll often enough that it doesn't matter, which
// for a periodic-task-driven app it will be. note the discarded-edge case for a button is
// benign in a way it isn't for a gesture: press/release alternate, so dropping a pair
// leaves `state` correct regardless
extern bool light_button_take_event(struct button_device *dev, uint8_t *type_out);
// overrides the debounce window (see debounce_ms above). a driver-level preference, not
// device state, so it persists across light_button_command_reset()
extern void light_button_set_debounce(struct button_device *dev, uint16_t debounce_ms);

// --- built-in GPIO driver ---
// a plain push-button wired to a GPIO isn't a vendor part the way a CST816T is, so it lives
// here rather than in a light_button_<chip> repo of its own. active_low is the usual wiring
// (button to ground, internal pull-up enabled); pass false for a button that pulls high,
// which configures a pull-down instead
extern struct button_driver *light_button_driver_gpio();
extern struct button_device *light_button_gpio_create_device(uint8_t *name, uint8_t pin, bool active_low);

#endif
