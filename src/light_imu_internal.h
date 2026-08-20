#ifndef _LIGHT_IMU_INTERNAL_H
#define _LIGHT_IMU_INTERNAL_H

// called once per scheduler tick by light_imu's own periodic task (module.c) to poll every
// live device for a new sample. not meant to be called by application code, hence kept out
// of the public header
extern void light_imu_poll_devices(void);

#endif
