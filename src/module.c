#include <light_imu.h>

#include "light_imu_internal.h"

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_imu, _module_event,
                                &light_core);

static uint8_t _module_task(struct light_application *app);
static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
                case LF_EVENT_MODULE_LOAD:
                light_imu_init();
                light_module_register_periodic_task(&light_imu, "light_imu_task", _module_task);
                break;
                // TODO implement unregister for event hooks
                case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}
static uint8_t _module_task(struct light_application *app)
{
        light_imu_poll_devices();
        return LF_STATUS_RUN;
}
