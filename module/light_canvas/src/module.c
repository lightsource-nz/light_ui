#include <light_canvas.h>
#include <module/mod_light_display.h>

#include "light_canvas_internal.h"

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_canvas, _module_event,
                                &light_draw,
                                &light_display,
                                &light_core);

static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
                case LF_EVENT_MODULE_LOAD:
                light_canvas_init();
                // deliberately no periodic task: a frame has to be paced against the
                // application's own clock and drawn by the application, so it drives
                // light_canvas_frame_begin() rather than the scheduler. light_display's own
                // task still drives the transfers this queues
                break;
                // TODO implement unregister for event hooks
                case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}
