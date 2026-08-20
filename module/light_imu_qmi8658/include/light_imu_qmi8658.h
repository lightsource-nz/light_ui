#ifndef _LIGHT_IMU_QMI8658_H
#define _LIGHT_IMU_QMI8658_H

#include <light_imu.h>

#include <stdint.h>

// QUECTEL QMI8658C 6-axis IMU (3-axis accelerometer + 3-axis gyroscope), I2C.
//
// PROVENANCE: the register map in this driver is cross-referenced from open-source drivers,
// NOT a primary datasheet -- the same footing light_touch_cst816t started on, where the
// vertical gesture codes turned out to be inverted from what those drivers claimed. treat
// the WHO_AM_I check in init as the first real evidence either way, and expect axis
// sign/order to need confirming against the board before anything downstream is trusted.

// 7-bit address with SA0 pulled high, which is how this part is strapped on the
// RP2350-Touch-LCD-1.69. 0x6A is the SA0-low alternative if a future board differs
#define QMI8658_I2C_ADDR                0x6B

// value WHO_AM_I is expected to return
#define QMI8658_CHIP_ID                 0x05

// the driver's defaults, exposed so an application can reason about the reported ranges.
// +/-8g leaves headroom for a knock without clipping while keeping tilt resolution fine,
// and +/-512dps is far more than a handheld board rotates at
#define QMI8658_ACCEL_FS_G_DEFAULT      8
#define QMI8658_GYRO_FS_DPS_DEFAULT     512

extern struct imu_driver *light_imu_driver_qmi8658();
// pin_int1 is accepted and configured but NOT used to gate sampling. it was originally
// deferred over the register map's provenance; it is now deferred because there is very
// little left for it to win. poll() reads status, temperature and all six axes as one
// contiguous transaction, so a data-ready check costs nothing on top of the sample it
// arrives with, and light_imu throttles polling to the sensor's output rate -- so the bus
// sees roughly one transfer per sample either way. an interrupt would trade that for a
// dependency on interrupt-enable and polarity bits from the same unverified map, against a
// configuration now confirmed working on hardware.
//
// pass LIGHT_IOPORT_PIN_NONE if the line isn't wired
extern struct imu_device *light_imu_qmi8658_create_device(
                uint8_t *name, struct io_context *io, uint8_t pin_int1);

#endif
