#include <light_ui.h>
#include <module/mod_light_display.h>

#include "light_ui_internal.h"

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
// no light_touch/light_button dependency by design -- see the input section of light_ui.h.
// rend and light_display ARE dependencies: light_ui draws through one and pushes through
// the other, and needs both loaded before any context can be created
Light_Module_Define(light_ui, _module_event,
                                &rend,
                                &light_display,
                                &light_core);

static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
                case LF_EVENT_MODULE_LOAD:
                light_ui_init();
                // deliberately no periodic task, unlike light_touch/light_button. rendering
                // has to be frame-rate gated against the application's own animation clock,
                // so the application drives light_ui_render() rather than the scheduler
                break;
                // TODO implement unregister for event hooks
                case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}
