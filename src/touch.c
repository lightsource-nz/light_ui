#include <light_touch.h>

#include "light_touch_internal.h"

static void _device_root_child_add(struct light_object *obj, struct light_object *child)
{
        struct touch_device_root *root = to_touch_device_root(obj);
        struct touch_device *dev = to_touch_device(child);
        root->device[dev->device_id] = dev;
}
static void _device_release(struct light_object *obj)
{
        struct touch_device *dev = to_touch_device(obj);
        //   the driver context was spawned for this device alone, so it goes with it. Without
        // this the device is reclaimed and its context -- plus whatever driver state hangs
        // off it -- is not, a leak that only becomes visible once teardown is exercised
        if(dev->driver_ctx && dev->driver_ctx->driver->destroy_context)
                dev->driver_ctx->driver->destroy_context(dev->driver_ctx);
        light_free(dev);
}
static void _device_add(struct light_object *obj, struct light_object *parent) {
        struct touch_device *dev = to_touch_device(obj);
        light_debug("name=%s", dev->header.id);
        light_touch_command_init(dev);
}
// singleton container object for touch_device objects
static struct lobj_type ltype_touch_device_root = (struct lobj_type) {
        .id = ID_TOUCH_DEVICE_ROOT,
        .release = NULL,
        .evt_child_add = _device_root_child_add
};
static struct lobj_type ltype_touch_device = (struct lobj_type) {
        .id = ID_TOUCH_DEVICE,
        .release = _device_release,
        .evt_add = _device_add
};
static struct touch_device_root device_root;

static volatile uint16_t next_device_id;

void light_touch_init()
{
        next_device_id = 0;
        light_object_init_static(&device_root.header, &ltype_touch_device_root);
        light_object_add(&device_root.header, NULL, "root_device");
}
struct touch_device_root *light_touch_device_get_root()
{
        return &device_root;
}
struct touch_device *light_touch_create_device(struct touch_driver *driver,
                                                uint16_t x_max, uint16_t y_max, uint8_t *format, ...)
{
        struct touch_device *dev = light_object_alloc(sizeof(struct touch_device));
        struct touch_driver_context *driver_ctx = driver->spawn_context();

        va_list vargs;

        va_start(vargs, format);
        return light_touch_init_device_va(dev, driver_ctx, x_max, y_max, format, vargs);
        va_end(vargs);
}
struct touch_device *light_touch_init_device(
                struct touch_device *dev,
                struct touch_driver_context *driver_ctx,
                uint16_t x_max, uint16_t y_max, uint8_t *format, ...)
{
        va_list vargs;

        va_start(vargs, format);
        return light_touch_init_device_va(dev, driver_ctx, x_max, y_max, format, vargs);
        va_end(vargs);
}
struct touch_device *light_touch_init_device_va(
                struct touch_device *dev,
                struct touch_driver_context *driver_ctx,
                uint16_t x_max, uint16_t y_max, uint8_t *format, va_list args)
{
        light_trace("(driver=%s, x_max=%d, y_max=%d)",
                                driver_ctx->driver->name, x_max, y_max);
        // TODO: this should be an ASSERT statement
        if(next_device_id >= LIGHT_TOUCH_MAX_DEVICES) {
                light_error("could not create new device: max devices reached (%d)", next_device_id);
                return NULL;
        }
        uint8_t device_id = next_device_id++;
        light_object_init(&dev->header, &ltype_touch_device);
        dev->device_id = device_id;
        dev->x_max = x_max;
        dev->y_max = y_max;
        dev->touch_active = false;
        dev->x = 0;
        dev->y = 0;
        dev->driver_ctx = driver_ctx;
        // light_object_alloc() doesn't zero -- an uninitialised gesture_tracking would
        // make the first release look like the end of a drag from garbage coordinates
        dev->gesture_tracking = false;
        dev->gesture_start_x = 0;
        dev->gesture_start_y = 0;
        dev->gesture_last_x = 0;
        dev->gesture_last_y = 0;
        dev->gesture_pending.type = TOUCH_GESTURE_NONE;
        dev->gesture_pending.from_hardware = false;
        dev->gesture_suppressed = false;
        // scaled off the shorter axis so the default is sane on any panel size, rather
        // than a constant that suits whichever display happened to be developed against
        dev->swipe_min_distance = (x_max < y_max ? x_max : y_max) / 8;

        light_object_add_va(&dev->header, &device_root.header, format, args);
        return dev;
}
void light_touch_command_init(struct touch_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->init_device(dev);
}
void light_touch_command_reset(struct touch_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->reset(dev);
}
// classifies the drag that just ended. the dominant axis wins, so a swipe only has to be
// mostly straight -- a diagonal is reported as whichever of the two it leaned toward
// rather than being rejected, which is the forgiving behaviour a finger on glass needs
static uint8_t _classify_swipe(struct touch_device *dev, int32_t dx, int32_t dy)
{
        int32_t adx = dx < 0 ? -dx : dx;
        int32_t ady = dy < 0 ? -dy : dy;

        if(adx >= ady) {
                if(adx < (int32_t)dev->swipe_min_distance)
                        return TOUCH_GESTURE_NONE;
                return dx > 0 ? TOUCH_GESTURE_SWIPE_RIGHT : TOUCH_GESTURE_SWIPE_LEFT;
        }
        if(ady < (int32_t)dev->swipe_min_distance)
                return TOUCH_GESTURE_NONE;
        // y grows downward in device coordinates, so a positive dy is a downward swipe
        return dy > 0 ? TOUCH_GESTURE_SWIPE_DOWN : TOUCH_GESTURE_SWIPE_UP;
}
// edge-driven off dev->touch_active, so it costs nothing on the many polls where the state
// hasn't changed, and is safe to call more often than samples actually arrive
static void _track_gesture(struct touch_device *dev)
{
        if(dev->touch_active) {
                if(!dev->gesture_tracking) {
                        dev->gesture_tracking = true;
                        dev->gesture_start_x = dev->x;
                        dev->gesture_start_y = dev->y;
                        // each touch starts unclaimed; a suppression applies to exactly the
                        // touch whose movement was consumed, never to the one after it
                        dev->gesture_suppressed = false;
                }
                dev->gesture_last_x = dev->x;
                dev->gesture_last_y = dev->y;
                return;
        }
        if(!dev->gesture_tracking)
                return;
        dev->gesture_tracking = false;

        //   a claimed touch classifies as nothing: its movement was already spent by whoever
        // claimed it (a drag-scroll), and reporting it again as a swipe would make one finger
        // movement mean two things. before the driver's own engine is asked, so a hardware
        // classification cannot resurrect it either
        if(dev->gesture_suppressed)
                return;

        int32_t dx = (int32_t)dev->gesture_last_x - (int32_t)dev->gesture_start_x;
        int32_t dy = (int32_t)dev->gesture_last_y - (int32_t)dev->gesture_start_y;

        // the controller's own engine gets first refusal: it's tuned by the vendor for its
        // own sensor, and can distinguish things a coordinate pair can't. it only ever
        // supplies the CLASSIFICATION though -- the endpoints below come from our own
        // tracking either way, since no controller reports where the gesture happened
        const struct touch_driver *drv = dev->driver_ctx->driver;
        uint8_t type = TOUCH_GESTURE_NONE;
        bool from_hardware = drv->read_gesture && drv->read_gesture(dev, &type);
        if(!from_hardware)
                type = _classify_swipe(dev, dx, dy);
        else
                // cheap cross-check while the CST816T's gesture register map is still
                // unverified against a primary datasheet: a persistent disagreement here
                // means the controller's idea of a direction doesn't match ours
                light_debug("device '%s': hardware gesture %d, software would say %d",
                                dev->header.id, type, _classify_swipe(dev, dx, dy));

        if(type == TOUCH_GESTURE_NONE)
                return;

        dev->gesture_pending.type = type;
        dev->gesture_pending.start_x = dev->gesture_start_x;
        dev->gesture_pending.start_y = dev->gesture_start_y;
        dev->gesture_pending.end_x = dev->gesture_last_x;
        dev->gesture_pending.end_y = dev->gesture_last_y;
        dev->gesture_pending.from_hardware = from_hardware;
        light_debug("gesture %d on device '%s' (%s): (%d,%d) -> (%d,%d)",
                        type, dev->header.id, from_hardware ? "hardware" : "software",
                        dev->gesture_start_x, dev->gesture_start_y,
                        dev->gesture_last_x, dev->gesture_last_y);
}
bool light_touch_command_poll(struct touch_device *dev, uint16_t *x_out, uint16_t *y_out)
{
        bool got_sample = dev->driver_ctx->driver->poll(dev);
        _track_gesture(dev);
        if(got_sample && dev->touch_active) {
                if(x_out) *x_out = dev->x;
                if(y_out) *y_out = dev->y;
        }
        return got_sample;
}
bool light_touch_take_gesture(struct touch_device *dev, struct touch_gesture *out)
{
        if(dev->gesture_pending.type == TOUCH_GESTURE_NONE)
                return false;
        if(out)
                *out = dev->gesture_pending;
        dev->gesture_pending.type = TOUCH_GESTURE_NONE;
        return true;
}
void light_touch_set_swipe_min_distance(struct touch_device *dev, uint16_t distance)
{
        dev->swipe_min_distance = distance;
}
void light_touch_suppress_gesture(struct touch_device *dev)
{
        // gated on a touch being in progress so a stray call between touches cannot leak
        // forward and silently eat the NEXT gesture -- the failure would be an occasionally
        // unresponsive swipe, which is miserable to diagnose from the symptom
        if(dev->gesture_tracking)
                dev->gesture_suppressed = true;
}
void light_touch_poll_devices(void)
{
        for(uint16_t i = 0; i < next_device_id; i++) {
                struct touch_device *dev = device_root.device[i];
                if(!dev)
                        continue;
                // routed through the command entry point rather than straight to the
                // driver, so gesture tracking sees every sample regardless of which path
                // drove the poll
                light_touch_command_poll(dev, NULL, NULL);
        }
}
