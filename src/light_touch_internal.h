#ifndef _LIGHT_TOUCH_INTERNAL_H
#define _LIGHT_TOUCH_INTERNAL_H

// called once per scheduler tick by light_touch's own periodic task (module.c) to poll
// every live device for a new touch sample. not meant to be called by application code,
// hence kept out of the public header
extern void light_touch_poll_devices(void);

#endif
