#ifndef _LIGHT_IMU_QMI8658_INTERNAL_H
#define _LIGHT_IMU_QMI8658_INTERNAL_H

// QMI8658C register map. see the provenance warning in light_imu_qmi8658.h -- these are
// cross-referenced from open-source drivers, not a primary datasheet

#define QMI8658_REG_WHO_AM_I            0x00
#define QMI8658_REG_REVISION            0x01
// serial interface / general settings. bit 6 enables address auto-increment on reads,
// which is what lets poll() pull all twelve sample bytes in one burst
#define QMI8658_REG_CTRL1               0x02
// accelerometer: full-scale in bits [6:4], output data rate in [3:0]
#define QMI8658_REG_CTRL2               0x03
// gyroscope: full-scale in bits [6:4], output data rate in [3:0]
#define QMI8658_REG_CTRL3               0x04
// low-pass filter enables/bandwidths for both sensors
#define QMI8658_REG_CTRL5               0x06
// enables: bit 0 accelerometer, bit 1 gyroscope
#define QMI8658_REG_CTRL7               0x08
#define QMI8658_REG_STATUS0             0x2E
#define QMI8658_REG_TEMP_L              0x33
// AX_L..GZ_H are twelve consecutive bytes: six little-endian int16s, accel then gyro
#define QMI8658_REG_AX_L                0x35
#define QMI8658_REG_GZ_H                0x40

// status, temperature and all six axes are CONTIGUOUS, so one read from STATUS0 through
// GZ_H fetches the lot in a single bus transaction -- the same cost the bare status read
// used to pay on its own, with the sample and the die temperature then free. STATUS1 and
// the three timestamp bytes fall in the middle unused; carrying five bytes we discard is
// far cheaper than a second round-trip on a bus shared with the touch controller.
//
// the offsets are derived from the register addresses rather than counted out by hand, so
// they cannot drift away from the map above
#define QMI8658_FRAME_BASE              QMI8658_REG_STATUS0
#define QMI8658_FRAME_BYTES             (QMI8658_REG_GZ_H - QMI8658_FRAME_BASE + 1)
#define QMI8658_FRAME_STATUS0           (QMI8658_REG_STATUS0 - QMI8658_FRAME_BASE)
#define QMI8658_FRAME_TEMP              (QMI8658_REG_TEMP_L - QMI8658_FRAME_BASE)
#define QMI8658_FRAME_ACCEL             (QMI8658_REG_AX_L - QMI8658_FRAME_BASE)
#define QMI8658_FRAME_GYRO              (QMI8658_FRAME_ACCEL + 6)

// die temperature is reported as a signed fixed-point value with 8 fractional bits, i.e.
// counts of 1/256 degC. same provenance caveat as the rest of this map
#define QMI8658_TEMP_COUNTS_PER_DEGREE  256

#define QMI8658_CTRL1_ADDR_AUTO_INC     (1 << 6)

#define QMI8658_CTRL7_ACCEL_EN          (1 << 0)
#define QMI8658_CTRL7_GYRO_EN           (1 << 1)

#define QMI8658_STATUS0_ACCEL_READY     (1 << 0)
#define QMI8658_STATUS0_GYRO_READY      (1 << 1)

// accelerometer full-scale selector, CTRL2 bits [6:4]
#define QMI8658_ACCEL_FS_2G             (0 << 4)
#define QMI8658_ACCEL_FS_4G             (1 << 4)
#define QMI8658_ACCEL_FS_8G             (2 << 4)
#define QMI8658_ACCEL_FS_16G            (3 << 4)

// gyroscope full-scale selector, CTRL3 bits [6:4]
#define QMI8658_GYRO_FS_16DPS           (0 << 4)
#define QMI8658_GYRO_FS_32DPS           (1 << 4)
#define QMI8658_GYRO_FS_64DPS           (2 << 4)
#define QMI8658_GYRO_FS_128DPS          (3 << 4)
#define QMI8658_GYRO_FS_256DPS          (4 << 4)
#define QMI8658_GYRO_FS_512DPS          (5 << 4)
#define QMI8658_GYRO_FS_1024DPS         (6 << 4)
#define QMI8658_GYRO_FS_2048DPS         (7 << 4)

// output data rate selector, low nibble of CTRL2/CTRL3. ~94.5Hz in the normal-mode table --
// comfortably above the 24fps the demo renders at, so a frame never waits on a sample
#define QMI8658_ODR_NORMAL              0x03

// minimum gap between bus accesses in poll(), matched to the ODR above (~10.5ms per sample,
// rounded down so a sample is never left waiting a whole extra interval)
#define QMI8658_POLL_INTERVAL_MS        10

#endif
