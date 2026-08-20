#include <light.h>
#include <module/mod_light_imu.h>

#include "light_imu_qmi8658_internal.h"

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_imu_qmi8658, _module_event,
                                &light_imu,
                                &light_core);

static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
                case LF_EVENT_MODULE_LOAD:
                // nothing to do: devices are created by the application's hardware setup,
                // and light_imu's own periodic task does the polling
                break;
                // TODO implement unregister for event hooks
                case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}
