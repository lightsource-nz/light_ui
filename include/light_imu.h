#ifndef _LIGHT_IMU_H
#define _LIGHT_IMU_H

#include <light.h>
#include <light_ioport.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#define ID_IMU_DEVICE_ROOT                      "light_imu:device_root"
#define ID_IMU_DEVICE                           "light_imu:device"

#define LIGHT_IMU_MAX_DEVICES                   4

// axis indices into accel_mg[]/gyro_mdps[]. those arrays are in the DEVICE frame, not the
// chip's: +X points right across the display, +Y up it, +Z out of it toward the viewer.
// a driver reports whatever its chip's own axes read, and light_imu rotates them into the
// device frame using the board's axis map (see light_imu_set_axis_map())
#define IMU_AXIS_X                              0
#define IMU_AXIS_Y                              1
#define IMU_AXIS_Z                              2
#define IMU_AXIS_COUNT                          3

// how a chip's own axes are mounted relative to the device frame above. source[i] names the
// CHIP axis that supplies device axis i, and sign[i] negates it.
//
// this has to exist for the orientation codes below to mean anything at all: "portrait" and
// "face up" describe the DISPLAY, and a chip soldered down rotated (or on the underside)
// reports axes that have no fixed relationship to it. without the map, orientation is only
// correct on a board whose IMU happens to be mounted square, which is luck rather than design
struct imu_axis_map {
        uint8_t source[IMU_AXIS_COUNT];
        int8_t sign[IMU_AXIS_COUNT];
};
// the map for a chip already aligned with the device frame, and the default for a device
// whose board hasn't set one
#define IMU_AXIS_MAP_IDENTITY \
        ((struct imu_axis_map) { \
                .source = { IMU_AXIS_X, IMU_AXIS_Y, IMU_AXIS_Z }, \
                .sign = { 1, 1, 1 } \
        })

// which way the device is being held, derived from the accelerometer (see
// light_imu_take_orientation()). "portrait" means the board's +Y axis points up
#define IMU_ORIENT_UNKNOWN                      0
#define IMU_ORIENT_PORTRAIT                     1
#define IMU_ORIENT_PORTRAIT_FLIP                2
#define IMU_ORIENT_LANDSCAPE_L                  3
#define IMU_ORIENT_LANDSCAPE_R                  4
#define IMU_ORIENT_FACE_UP                      5
#define IMU_ORIENT_FACE_DOWN                    6

// how long a new orientation must hold before it is adopted, and by how much its axis must
// beat the incumbent. both are needed: without the margin a board held near 45 degrees
// flaps between two orientations every sample, and without the hold time a single knock
// re-orients the UI
#define LIGHT_IMU_ORIENT_HOLD_MS_DEFAULT        250
#define LIGHT_IMU_ORIENT_MARGIN_MG_DEFAULT      200

struct imu_device;
struct imu_driver
{
        const uint8_t *name;
        struct imu_driver_context *(*spawn_context)();
        //   frees whatever spawn_context() allocated. Called when the device holding that
        // context is released, so a context outlives exactly the device it was spawned for.
        // OPTIONAL: a driver whose context is not heap-allocated leaves this NULL and the
        // release path skips it
        void (*destroy_context)(struct imu_driver_context *ctx);
        void (*init_device)(struct imu_device *);
        void (*reset)(struct imu_device *);
        // samples the sensor, writing engineering units into dev->accel_mg/gyro_mdps in the
        // CHIP's own axes (light_imu rotates them into the device frame afterwards). returns
        // true if a new sample was captured, false if there was nothing new to report
        bool (*poll)(struct imu_device *);

        // how often this chip can actually produce a new sample, in ms. light_imu throttles
        // polling to it, so no driver has to time its own transport access.
        //
        // data rather than a function because the logic around it is identical in every
        // driver and only the constant ever differs -- the same call light_display made with
        // async_timeout_ms. 0 means "poll whenever asked", for a driver whose read is free
        uint16_t sample_interval_ms;
};
struct imu_driver_context
{
        const struct imu_driver *driver;
        const void *state;
};

struct imu_device {
        struct light_object header;
        uint8_t device_id;

        // last-sampled state, in ENGINEERING UNITS rather than raw counts: milli-g and
        // milli-degrees per second. integer throughout, so this stays usable on an FPU-less
        // RP2040 as readily as on the RP2350 it was written against -- and it means the
        // conversion (which needs the chip's full-scale setting) happens once, in the
        // driver that knows it, rather than in every caller.
        //
        // in the DEVICE frame: drivers write chip-frame values here and light_imu rotates
        // them in place through axis_map on the way out of the poll
        int32_t accel_mg[IMU_AXIS_COUNT];
        int32_t gyro_mdps[IMU_AXIS_COUNT];
        struct imu_axis_map axis_map;
        // milli-degrees Celsius; drivers that don't report die temperature leave this at 0
        int32_t temperature_mc;
        struct imu_driver_context *driver_ctx;

        // polling throttle -- see imu_driver.sample_interval_ms. seeded from the driver at
        // creation and overridable per device, e.g. to trade responsiveness for power
        uint32_t last_poll_ms;
        uint16_t poll_interval_ms;

        // --- orientation tracking, driven from light_imu_command_poll() ---
        // the settled orientation, and the candidate currently being timed out
        uint8_t orientation;
        uint8_t orientation_candidate;
        uint32_t orientation_candidate_ms;
        // IMU_ORIENT_UNKNOWN when nothing is waiting to be collected
        uint8_t orientation_pending;
        uint16_t orientation_hold_ms;
        uint16_t orientation_margin_mg;
};
struct imu_device_root {
        struct light_object header;
        struct imu_device *device[LIGHT_IMU_MAX_DEVICES];
};

#define to_imu_device_root(ptr) container_of(ptr, struct imu_device_root, header)
#define to_imu_device(ptr) container_of(ptr, struct imu_device, header)

extern void light_imu_init();

extern struct imu_device_root *light_imu_device_get_root();
extern struct imu_device *light_imu_create_device(struct imu_driver *driver, uint8_t *format, ...);
extern struct imu_device *light_imu_create_device_va(struct imu_driver *driver,
                                                uint8_t *format, va_list args);
// lower-level entry point, same rationale as light_touch_init_device(): a driver's own state
// (its io_context, interrupt pin) must be attached to driver_ctx before the device is added
// to the object tree, since that add synchronously triggers init_device()
extern struct imu_device *light_imu_init_device(
                struct imu_device *dev,
                struct imu_driver_context *driver_ctx,
                uint8_t *format, ...);
extern struct imu_device *light_imu_init_device_va(
                struct imu_device *dev,
                struct imu_driver_context *driver_ctx,
                uint8_t *format, va_list args);
extern void light_imu_command_init(struct imu_device *dev);
extern void light_imu_command_reset(struct imu_device *dev);
// polls the device for a new sample and advances orientation tracking. returns true when a
// new sample was captured.
//
// safe (and expected) to call far more often than the sensor produces data: the driver is
// only actually asked once per poll_interval_ms, and every call in between returns false
// without touching the transport at all
extern bool light_imu_command_poll(struct imu_device *dev);
// overrides the polling throttle seeded from the driver (see imu_driver.sample_interval_ms)
extern void light_imu_set_poll_interval(struct imu_device *dev, uint16_t interval_ms);

// collects a pending orientation CHANGE and clears it, so each change is reported exactly
// once however often this is called. returns false and leaves *out untouched when the
// orientation hasn't changed since it was last collected.
//
// callers that want the current orientation rather than notification of a change should
// just read dev->orientation, which is always the settled value
extern bool light_imu_take_orientation(struct imu_device *dev, uint8_t *out);
// overrides the orientation thresholds (see the defaults above). driver-level preferences
// rather than device state, so they persist across light_imu_command_reset()
extern void light_imu_set_orientation_thresholds(struct imu_device *dev,
                                                uint16_t hold_ms, uint16_t margin_mg);
// declares how this board mounts the chip (see struct imu_axis_map). set it once, from the
// board's own hardware setup, before anything reads a sample -- it is a property of the
// PCB, so a driver cannot know it and an application should not have to compensate for it
extern void light_imu_set_axis_map(struct imu_device *dev, struct imu_axis_map map);

// converts a raw signed 16-bit sensor count into engineering units given the full-scale
// range it was sampled at (e.g. 8000 for a +/-8g accelerometer reporting milli-g).
//
// two details matter here, which is why every driver calls this rather than open-coding it.
// the int64 intermediate is load-bearing: at a +/-2048 dps range 32767 * 2048000 is 6.7e10
// and overflows int32 outright. and the divide is a real divide, not a >>15 -- right-shifting
// a negative value is implementation-defined in C, and where it is defined it floors, which
// biases every negative reading a count low rather than truncating symmetrically about zero
static inline int32_t light_imu_scale_sample(int16_t raw, uint32_t full_scale_units)
{
        return (int32_t)(((int64_t)raw * (int64_t)full_scale_units) / 32768);
}

#endif
