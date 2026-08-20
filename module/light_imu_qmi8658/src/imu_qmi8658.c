#include <light_imu_qmi8658.h>

#include "light_imu_qmi8658_internal.h"

#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
#include <hardware/gpio.h>
#endif

struct qmi8658_state {
        struct io_context *io_ctx;
        // INT1 -- configured but not read; the data-ready bit arrives inside the sample
        // frame for free instead (see light_imu_qmi8658.h). kept so switching to
        // interrupt-driven sampling later needs no signature change
        uint8_t pin_int1;
        // full-scale ranges currently programmed into the chip, in the units the scaling
        // needs: milli-g and milli-dps at full deflection. held here rather than assumed
        // from the defaults, so a future set_range() call stays consistent with the
        // conversion without a second source of truth
        uint32_t accel_fs_mg;
        uint32_t gyro_fs_mdps;
};

static struct imu_driver_context *_qmi8658_spawn_context();
static void _qmi8658_destroy_context(struct imu_driver_context *ctx);
static void _qmi8658_init(struct imu_device *dev);
static void _qmi8658_reset(struct imu_device *dev);
static bool _qmi8658_poll(struct imu_device *dev);

static struct imu_driver _driver_qmi8658 = {
        .name = "imu.driver:qmi8658",
        .spawn_context = _qmi8658_spawn_context,
        .destroy_context = _qmi8658_destroy_context,
        .init_device = _qmi8658_init,
        .reset = _qmi8658_reset,
        .poll = _qmi8658_poll,
        // matched to the ~94.5Hz ODR configured below, rounded down so a ready sample is
        // never left waiting a whole extra interval. light_imu does the throttling
        .sample_interval_ms = QMI8658_POLL_INTERVAL_MS
};

struct imu_driver *light_imu_driver_qmi8658()
{
        return &_driver_qmi8658;
}

static struct imu_driver_context *_qmi8658_spawn_context()
{
        struct imu_driver_context *ctx = light_alloc(sizeof(struct imu_driver_context));
        ctx->driver = light_imu_driver_qmi8658();
        ctx->state = light_alloc(sizeof(struct qmi8658_state));
        struct qmi8658_state *state = (struct qmi8658_state *) ctx->state;
        // light_alloc() isn't zeroed, same as every other driver state in this codebase.
        // the ranges especially must not start as garbage -- they divide every sample
        state->io_ctx = NULL;
        state->pin_int1 = LIGHT_IOPORT_PIN_NONE;
        state->accel_fs_mg = (uint32_t)QMI8658_ACCEL_FS_G_DEFAULT * 1000;
        state->gyro_fs_mdps = (uint32_t)QMI8658_GYRO_FS_DPS_DEFAULT * 1000;
        return ctx;
}

//   the counterpart to _qmi8658_spawn_context(), called from the device release path
// when the device this context was spawned for is freed. Frees in the reverse of
// the order allocated: the state first, then the context that points at it
static void _qmi8658_destroy_context(struct imu_driver_context *ctx)
{
        light_free((void *)ctx->state);
        light_free(ctx);
}

static void _qmi8658_gpio_int_setup(uint8_t pin_int1)
{
        if(pin_int1 == LIGHT_IOPORT_PIN_NONE)
                return;
#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
        gpio_init(pin_int1);
        gpio_set_dir(pin_int1, false);
        gpio_pull_up(pin_int1);
#endif
}

static bool _qmi8658_write_reg(struct qmi8658_state *state, uint8_t reg, uint8_t value)
{
        return light_ioport_write_register(state->io_ctx, reg, &value, 1);
}

static void _qmi8658_configure(struct imu_device *dev)
{
        struct qmi8658_state *state = (struct qmi8658_state *) dev->driver_ctx->state;

        // address auto-increment is what makes the twelve-byte sample burst in poll() work
        // at all -- without it every read would return the same register
        _qmi8658_write_reg(state, QMI8658_REG_CTRL1, QMI8658_CTRL1_ADDR_AUTO_INC);
        _qmi8658_write_reg(state, QMI8658_REG_CTRL2, QMI8658_ACCEL_FS_8G | QMI8658_ODR_NORMAL);
        _qmi8658_write_reg(state, QMI8658_REG_CTRL3, QMI8658_GYRO_FS_512DPS | QMI8658_ODR_NORMAL);
        // enabled last: the ranges have to be in place before the sensors start producing
        // samples against them, or the first few reads are scaled by the wrong constant
        _qmi8658_write_reg(state, QMI8658_REG_CTRL7,
                        QMI8658_CTRL7_ACCEL_EN | QMI8658_CTRL7_GYRO_EN);

        state->accel_fs_mg = (uint32_t)QMI8658_ACCEL_FS_G_DEFAULT * 1000;
        state->gyro_fs_mdps = (uint32_t)QMI8658_GYRO_FS_DPS_DEFAULT * 1000;
}

static void _qmi8658_init(struct imu_device *dev)
{
        struct qmi8658_state *state = (struct qmi8658_state *) dev->driver_ctx->state;
        _qmi8658_gpio_int_setup(state->pin_int1);

        uint8_t chip_id = 0;
        // log-and-continue, not hard-fail: this register map is cross-referenced from
        // open-source drivers rather than a primary datasheet, so if the ID read is wrong
        // the useful thing is to let poll() run anyway and see what it produces -- exactly
        // the call light_touch_cst816t makes, for the same reason
        if(!light_ioport_read_register(state->io_ctx, QMI8658_REG_WHO_AM_I, &chip_id, 1)) {
                light_warn("failed to read chip ID for device '%s'", dev->header.id);
        } else if(chip_id != QMI8658_CHIP_ID) {
                light_warn("unexpected chip ID for device '%s': got 0x%x, expected 0x%x",
                                dev->header.id, chip_id, QMI8658_CHIP_ID);
        } else {
                light_info("qmi8658 chip ID confirmed for device '%s': 0x%x", dev->header.id, chip_id);
        }

        _qmi8658_configure(dev);
}
static void _qmi8658_reset(struct imu_device *dev)
{
        struct qmi8658_state *state = (struct qmi8658_state *) dev->driver_ctx->state;
        // this part shares the I2C bus (and therefore the reset line, if any) with the touch
        // controller, so reset here means re-applying our own configuration rather than
        // pulsing a shared line out from under a neighbour
        (void)state;
        _qmi8658_configure(dev);
}

// little-endian int16 out of a byte pair, which is the layout the sample registers use
static int16_t _le16(const uint8_t *p)
{
        return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static bool _qmi8658_poll(struct imu_device *dev)
{
        struct qmi8658_state *state = (struct qmi8658_state *) dev->driver_ctx->state;

        // status, temperature and all six axes in ONE transaction (see QMI8658_FRAME_BASE).
        // this used to be a status read followed by a separate twelve-byte sample burst;
        // since the registers are contiguous and auto-increment is enabled, the whole frame
        // costs what the status read alone used to. light_imu throttles how often we get
        // here, so this runs about once per sample rather than once per scheduler tick
        uint8_t frame[QMI8658_FRAME_BYTES];
        if(!light_ioport_read_register(state->io_ctx, QMI8658_FRAME_BASE,
                                        frame, QMI8658_FRAME_BYTES))
                return false;

        // the data we just read is stale if the sensor hasn't produced a new sample -- the
        // status byte came from the same transaction, so it describes exactly this frame
        if(!(frame[QMI8658_FRAME_STATUS0] &
                        (QMI8658_STATUS0_ACCEL_READY | QMI8658_STATUS0_GYRO_READY)))
                return false;

        dev->temperature_mc = ((int32_t)_le16(&frame[QMI8658_FRAME_TEMP]) * 1000)
                        / QMI8658_TEMP_COUNTS_PER_DEGREE;

        for(uint8_t axis = 0; axis < IMU_AXIS_COUNT; axis++) {
                dev->accel_mg[axis] = light_imu_scale_sample(
                        _le16(&frame[QMI8658_FRAME_ACCEL + axis * 2]), state->accel_fs_mg);
                dev->gyro_mdps[axis] = light_imu_scale_sample(
                        _le16(&frame[QMI8658_FRAME_GYRO + axis * 2]), state->gyro_fs_mdps);
        }

        return true;
}

struct imu_device *light_imu_qmi8658_create_device(
        uint8_t *name, struct io_context *io, uint8_t pin_int1)
{
        // io_ctx/pin_int1 must be attached to the driver state before the device is
        // registered: adding it to the object tree (via light_imu_init_device()) immediately
        // triggers init_device(), which reads the chip ID and writes the control registers
        // over exactly this io_context -- same rationale/pattern as every other driver here
        struct imu_device *dev = light_object_alloc(sizeof(struct imu_device));
        struct imu_driver_context *driver_ctx = _qmi8658_spawn_context();
        struct qmi8658_state *state = (struct qmi8658_state *) driver_ctx->state;
        state->io_ctx = io;
        state->pin_int1 = pin_int1;

        return light_imu_init_device(dev, driver_ctx, "%s", name);
}
