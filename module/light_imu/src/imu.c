#include <light_imu.h>
#include <light_platform.h>

#include "light_imu_internal.h"

static void _device_root_child_add(struct light_object *obj, struct light_object *child)
{
        struct imu_device_root *root = to_imu_device_root(obj);
        struct imu_device *dev = to_imu_device(child);
        root->device[dev->device_id] = dev;
}
static void _device_release(struct light_object *obj)
{
        struct imu_device *dev = to_imu_device(obj);
        //   the driver context was spawned for this device alone, so it goes with it. Without
        // this the device is reclaimed and its context -- plus whatever driver state hangs
        // off it -- is not, a leak that only becomes visible once teardown is exercised
        if(dev->driver_ctx && dev->driver_ctx->driver->destroy_context)
                dev->driver_ctx->driver->destroy_context(dev->driver_ctx);
        light_free(dev);
}
static void _device_add(struct light_object *obj, struct light_object *parent) {
        struct imu_device *dev = to_imu_device(obj);
        light_debug("name=%s", dev->header.id);
        light_imu_command_init(dev);
}
// singleton container object for imu_device objects
static struct lobj_type ltype_imu_device_root = (struct lobj_type) {
        .id = ID_IMU_DEVICE_ROOT,
        .release = NULL,
        .evt_child_add = _device_root_child_add
};
static struct lobj_type ltype_imu_device = (struct lobj_type) {
        .id = ID_IMU_DEVICE,
        .release = _device_release,
        .evt_add = _device_add
};
static struct imu_device_root device_root;

static volatile uint16_t next_device_id;

void light_imu_init()
{
        next_device_id = 0;
        light_object_init_static(&device_root.header, &ltype_imu_device_root);
        light_object_add(&device_root.header, NULL, "root_device");
}
struct imu_device_root *light_imu_device_get_root()
{
        return &device_root;
}
struct imu_device *light_imu_create_device(struct imu_driver *driver, uint8_t *format, ...)
{
        va_list vargs;

        va_start(vargs, format);
        struct imu_device *dev = light_imu_create_device_va(driver, format, vargs);
        va_end(vargs);
        return dev;
}
struct imu_device *light_imu_create_device_va(struct imu_driver *driver,
                                                uint8_t *format, va_list args)
{
        struct imu_device *dev = light_object_alloc(sizeof(struct imu_device));
        struct imu_driver_context *driver_ctx = driver->spawn_context();

        return light_imu_init_device_va(dev, driver_ctx, format, args);
}
struct imu_device *light_imu_init_device(
                struct imu_device *dev,
                struct imu_driver_context *driver_ctx,
                uint8_t *format, ...)
{
        va_list vargs;

        va_start(vargs, format);
        struct imu_device *out = light_imu_init_device_va(dev, driver_ctx, format, vargs);
        va_end(vargs);
        return out;
}
struct imu_device *light_imu_init_device_va(
                struct imu_device *dev,
                struct imu_driver_context *driver_ctx,
                uint8_t *format, va_list args)
{
        light_trace("(driver=%s)", driver_ctx->driver->name);
        // TODO: this should be an ASSERT statement
        if(next_device_id >= LIGHT_IMU_MAX_DEVICES) {
                light_error("could not create new device: max devices reached (%d)", next_device_id);
                return NULL;
        }
        uint8_t device_id = next_device_id++;
        light_object_init(&dev->header, &ltype_imu_device);
        dev->device_id = device_id;
        dev->driver_ctx = driver_ctx;
        // light_object_alloc() doesn't zero -- an uninitialised orientation_candidate would
        // make the first poll compare against garbage and could adopt an orientation the
        // board was never in
        for(uint8_t i = 0; i < IMU_AXIS_COUNT; i++) {
                dev->accel_mg[i] = 0;
                dev->gyro_mdps[i] = 0;
        }
        dev->temperature_mc = 0;
        dev->axis_map = IMU_AXIS_MAP_IDENTITY;
        dev->last_poll_ms = 0;
        dev->poll_interval_ms = driver_ctx->driver->sample_interval_ms;
        dev->orientation = IMU_ORIENT_UNKNOWN;
        dev->orientation_candidate = IMU_ORIENT_UNKNOWN;
        dev->orientation_candidate_ms = 0;
        dev->orientation_pending = IMU_ORIENT_UNKNOWN;
        dev->orientation_hold_ms = LIGHT_IMU_ORIENT_HOLD_MS_DEFAULT;
        dev->orientation_margin_mg = LIGHT_IMU_ORIENT_MARGIN_MG_DEFAULT;

        light_object_add_va(&dev->header, &device_root.header, format, args);
        return dev;
}
void light_imu_command_init(struct imu_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->init_device(dev);
}
void light_imu_command_reset(struct imu_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->reset(dev);
}

// classifies which way the board is being held from the gravity vector alone. the dominant
// axis wins, so the board only has to be roughly in an orientation rather than exactly.
//
// this deliberately ignores the gyro: integrating angular rate would drift, and gravity is
// an absolute reference that never does. the cost is that it says nothing useful during
// freefall or sustained linear acceleration, which for "which way is up" is the right trade
static uint8_t _classify_orientation(const struct imu_device *dev, int32_t margin_mg)
{
        int32_t x = dev->accel_mg[IMU_AXIS_X];
        int32_t y = dev->accel_mg[IMU_AXIS_Y];
        int32_t z = dev->accel_mg[IMU_AXIS_Z];
        int32_t ax = x < 0 ? -x : x;
        int32_t ay = y < 0 ? -y : y;
        int32_t az = z < 0 ? -z : z;

        // the dominant axis must beat BOTH others by the margin, not merely exceed them --
        // near a 45 degree tilt two axes read almost equally, and picking whichever is
        // momentarily larger is exactly the flapping this is here to prevent
        if(ax >= ay + margin_mg && ax >= az + margin_mg)
                return x > 0 ? IMU_ORIENT_LANDSCAPE_R : IMU_ORIENT_LANDSCAPE_L;
        if(ay >= ax + margin_mg && ay >= az + margin_mg)
                return y > 0 ? IMU_ORIENT_PORTRAIT : IMU_ORIENT_PORTRAIT_FLIP;
        if(az >= ax + margin_mg && az >= ay + margin_mg)
                return z > 0 ? IMU_ORIENT_FACE_UP : IMU_ORIENT_FACE_DOWN;

        // no axis clearly dominant: hold whatever was already settled rather than reporting
        // UNKNOWN, so a board tilted through an ambiguous angle doesn't blink out
        return dev->orientation;
}
// adopts a candidate orientation only once it has held for orientation_hold_ms. edge-driven
// off the candidate changing, so it costs nothing on the many polls where nothing moved
static void _track_orientation(struct imu_device *dev, uint32_t now)
{
        uint8_t observed = _classify_orientation(dev, dev->orientation_margin_mg);

        if(observed != dev->orientation_candidate) {
                dev->orientation_candidate = observed;
                dev->orientation_candidate_ms = now;
                return;
        }
        if(observed == dev->orientation)
                return;
        if(now - dev->orientation_candidate_ms < dev->orientation_hold_ms)
                return;

        dev->orientation = observed;
        dev->orientation_pending = observed;
        light_debug("device '%s': orientation now %d", dev->header.id, observed);
}

// rotates a freshly-sampled chip-frame reading into the device frame, in place. done here
// rather than in each driver because the mounting is a property of the BOARD, which a chip
// driver has no way to know -- and done before orientation is classified, since the
// orientation codes are defined in the device frame
static void _apply_axis_map(struct imu_device *dev)
{
        int32_t accel[IMU_AXIS_COUNT], gyro[IMU_AXIS_COUNT];

        for(uint8_t i = 0; i < IMU_AXIS_COUNT; i++) {
                accel[i] = dev->accel_mg[i];
                gyro[i] = dev->gyro_mdps[i];
        }
        for(uint8_t i = 0; i < IMU_AXIS_COUNT; i++) {
                uint8_t src = dev->axis_map.source[i];
                // a map naming an out-of-range source would index past the copies above --
                // fall back to the straight-through axis rather than read rubbish
                if(src >= IMU_AXIS_COUNT)
                        src = i;
                dev->accel_mg[i] = accel[src] * dev->axis_map.sign[i];
                dev->gyro_mdps[i] = gyro[src] * dev->axis_map.sign[i];
        }
}

bool light_imu_command_poll(struct imu_device *dev)
{
        uint32_t now = light_platform_get_time_since_init();

        // don't go near the transport more often than the sensor can produce data. this is
        // the core's job rather than each driver's: a sensor runs at its own output rate
        // while the module task polls every scheduler tick, so the great majority of calls
        // would otherwise spend a bus read discovering there is nothing new -- and on a bus
        // shared with other devices, that costs every one of them latency
        if(dev->poll_interval_ms && now - dev->last_poll_ms < dev->poll_interval_ms)
                return false;
        dev->last_poll_ms = now;

        bool got_sample = dev->driver_ctx->driver->poll(dev);
        if(got_sample) {
                _apply_axis_map(dev);
                _track_orientation(dev, now);
        }
        return got_sample;
}
void light_imu_set_poll_interval(struct imu_device *dev, uint16_t interval_ms)
{
        dev->poll_interval_ms = interval_ms;
}
bool light_imu_take_orientation(struct imu_device *dev, uint8_t *out)
{
        if(dev->orientation_pending == IMU_ORIENT_UNKNOWN)
                return false;
        if(out)
                *out = dev->orientation_pending;
        dev->orientation_pending = IMU_ORIENT_UNKNOWN;
        return true;
}
void light_imu_set_orientation_thresholds(struct imu_device *dev,
                                        uint16_t hold_ms, uint16_t margin_mg)
{
        dev->orientation_hold_ms = hold_ms;
        dev->orientation_margin_mg = margin_mg;
}
void light_imu_set_axis_map(struct imu_device *dev, struct imu_axis_map map)
{
        dev->axis_map = map;
        // the settled orientation was classified in the old frame, so it means nothing now.
        // clearing it rather than leaving it makes the next poll re-derive from scratch
        dev->orientation = IMU_ORIENT_UNKNOWN;
        dev->orientation_candidate = IMU_ORIENT_UNKNOWN;
}
void light_imu_poll_devices(void)
{
        for(uint16_t i = 0; i < next_device_id; i++) {
                struct imu_device *dev = device_root.device[i];
                if(!dev)
                        continue;
                // routed through the command entry point rather than straight to the driver,
                // so orientation tracking sees every sample regardless of what drove the poll
                light_imu_command_poll(dev);
        }
}
