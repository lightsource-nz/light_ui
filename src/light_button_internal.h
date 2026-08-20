#ifndef _LIGHT_BUTTON_INTERNAL_H
#define _LIGHT_BUTTON_INTERNAL_H

// called once per scheduler tick by light_button's own periodic task (module.c) to sample
// every live device and advance its debounce state. not meant to be called by application
// code, hence kept out of the public header
extern void light_button_poll_devices(void);

#endif
