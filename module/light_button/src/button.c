#include <light_button.h>
#include <light_platform.h>

#include "light_button_internal.h"

static void _device_root_child_add(struct light_object *obj, struct light_object *child)
{
        struct button_device_root *root = to_button_device_root(obj);
        struct button_device *dev = to_button_device(child);
        root->device[dev->device_id] = dev;
}
static void _device_release(struct light_object *obj)
{
        struct button_device *dev = to_button_device(obj);
        //   the driver context was spawned for this device alone, so it goes with it. Without
        // this the device is reclaimed and its context -- plus whatever driver state hangs
        // off it -- is not, a leak that only becomes visible once teardown is exercised
        if(dev->driver_ctx && dev->driver_ctx->driver->destroy_context)
                dev->driver_ctx->driver->destroy_context(dev->driver_ctx);
        light_free(dev);
}
static void _device_add(struct light_object *obj, struct light_object *parent) {
        struct button_device *dev = to_button_device(obj);
        light_debug("name=%s", dev->header.id);
        light_button_command_init(dev);
}
// singleton container object for button_device objects
static struct lobj_type ltype_button_device_root = (struct lobj_type) {
        .id = ID_BUTTON_DEVICE_ROOT,
        .release = NULL,
        .evt_child_add = _device_root_child_add
};
static struct lobj_type ltype_button_device = (struct lobj_type) {
        .id = ID_BUTTON_DEVICE,
        .release = _device_release,
        .evt_add = _device_add
};
static struct button_device_root device_root;

static volatile uint16_t next_device_id;

void light_button_init()
{
        next_device_id = 0;
        light_object_init_static(&device_root.header, &ltype_button_device_root);
        light_object_add(&device_root.header, NULL, "root_device");
}
struct button_device_root *light_button_device_get_root()
{
        return &device_root;
}
struct button_device *light_button_create_device(struct button_driver *driver, uint8_t *format, ...)
{
        va_list vargs;

        va_start(vargs, format);
        struct button_device *dev = light_button_create_device_va(driver, format, vargs);
        va_end(vargs);
        return dev;
}
struct button_device *light_button_create_device_va(struct button_driver *driver,
                                                uint8_t *format, va_list args)
{
        struct button_device *dev = light_object_alloc(sizeof(struct button_device));
        struct button_driver_context *driver_ctx = driver->spawn_context();

        return light_button_init_device_va(dev, driver_ctx, format, args);
}
struct button_device *light_button_init_device(
                struct button_device *dev,
                struct button_driver_context *driver_ctx,
                uint8_t *format, ...)
{
        va_list vargs;

        va_start(vargs, format);
        struct button_device *out = light_button_init_device_va(dev, driver_ctx, format, vargs);
        va_end(vargs);
        return out;
}
struct button_device *light_button_init_device_va(
                struct button_device *dev,
                struct button_driver_context *driver_ctx,
                uint8_t *format, va_list args)
{
        light_trace("(driver=%s)", driver_ctx->driver->name);
        // TODO: this should be an ASSERT statement
        if(next_device_id >= LIGHT_BUTTON_MAX_DEVICES) {
                light_error("could not create new device: max devices reached (%d)", next_device_id);
                return NULL;
        }
        uint8_t device_id = next_device_id++;
        light_object_init(&dev->header, &ltype_button_device);
        dev->device_id = device_id;
        dev->driver_ctx = driver_ctx;
        // light_object_alloc() doesn't zero. an uninitialised raw_last of `true` would make
        // the very first poll of an unpressed button look like a release edge and latch a
        // spurious BUTTON_EVENT_RELEASE before anything had ever been pressed
        dev->state = false;
        dev->raw_last = false;
        dev->raw_change_ms = 0;
        dev->event_pending = BUTTON_EVENT_NONE;
        dev->debounce_ms = LIGHT_BUTTON_DEBOUNCE_MS_DEFAULT;

        light_object_add_va(&dev->header, &device_root.header, format, args);
        return dev;
}
void light_button_command_init(struct button_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->init_device(dev);
}
void light_button_command_reset(struct button_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->reset(dev);
}
bool light_button_command_poll(struct button_device *dev)
{
        bool raw = dev->driver_ctx->driver->read(dev);
        uint32_t now = light_platform_get_time_since_init();

        // any raw change restarts the window, which is what makes this reject bounce rather
        // than merely delay it: a contact that keeps chattering never accumulates
        // debounce_ms of stability, so `state` doesn't move until it settles
        if(raw != dev->raw_last) {
                dev->raw_last = raw;
                dev->raw_change_ms = now;
                return false;
        }
        if(raw == dev->state)
                return false;
        if(now - dev->raw_change_ms < dev->debounce_ms)
                return false;

        dev->state = raw;
        dev->event_pending = raw ? BUTTON_EVENT_PRESS : BUTTON_EVENT_RELEASE;
        light_debug("button '%s': %s", dev->header.id, raw ? "press" : "release");
        return true;
}
bool light_button_take_event(struct button_device *dev, uint8_t *type_out)
{
        if(dev->event_pending == BUTTON_EVENT_NONE)
                return false;
        if(type_out)
                *type_out = dev->event_pending;
        dev->event_pending = BUTTON_EVENT_NONE;
        return true;
}
void light_button_set_debounce(struct button_device *dev, uint16_t debounce_ms)
{
        dev->debounce_ms = debounce_ms;
}
void light_button_poll_devices(void)
{
        for(uint16_t i = 0; i < next_device_id; i++) {
                struct button_device *dev = device_root.device[i];
                if(!dev)
                        continue;
                light_button_command_poll(dev);
        }
}
