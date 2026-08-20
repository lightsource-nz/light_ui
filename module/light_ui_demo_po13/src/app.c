#include <light_ui_demo.h>
#include <light_ui_hw_po13.h>
#include <light_button.h>
#include <module/mod_light_button.h>
#include <module/mod_light_display.h>
#include <module/mod_light_ui.h>
#include <TypeLightSans_ttf_8px_font.h>

// app: light_ui_demo_po13
// the shared light_ui demo on the Pico-OLED-1.3, navigated with the board's two onboard
// keys: KEY0 cycles focus through the buttons, KEY1 activates the focused one.
//
// single-display, unlike the circle demo on the same board: the secondary SH1107 panel's
// CS line is GP17, which is also KEY1 (see light_ui_hw_po13.h). the board can drive the
// second panel or read the second key, not both -- and this app is the one that needs keys
//
// its light_ui_demo_config.h is empty: light_ui_demo_common's defaults (64x128, 1bpp,
// LIGHT_DRAW_ROTATE_90, one display) are already this board's real geometry

// named per-app rather than after the shared demo: this define lives in each app precisely
// so it can name its own dependencies, and the name it gives the application is what
// light_module_get_name() reports in the log, so it should say which binary is running
Light_Application_Define(light_ui_demo_po13, light_ui_demo_event, light_ui_demo_main,
                                &light_draw,
                                &light_display,
                                &light_ui,
                                &light_button,
                                &light_core);

static struct button_device *_key_next;
static struct button_device *_key_select;

void main(int argc, char **argv)
{
        light_framework_init();
        light_framework_run(argc, argv);
}

const light_draw_font_t *__light_ui_demo_font(void)
{
        // 8px rather than the 16px face the circle-demo rigs' fonts use: at 12x19 px per
        // glyph a 128x64 logical canvas fits barely three rows of ten characters, leaving
        // no room for a window frame, a title and stacked buttons all at once
        return &TypeLightSans_ttf_8px_font;
}

void __light_ui_demo_hardware_init(void)
{
        _display[0] = light_ui_hw_po13_display_main();

        // both keys are wired to ground through the switch with no external pull, so
        // active_low with the internal pull-up is the correct configuration
        _key_next = light_button_gpio_create_device(
                        "screentest_key_next", ST_BUTTON_PIN_KEY0, true);
        _key_select = light_button_gpio_create_device(
                        "screentest_key_select", ST_BUTTON_PIN_KEY1, true);

        light_info("input pipeline setup complete","");
}

void __light_ui_demo_input_poll(void)
{
        uint8_t event;

        // light_button's own periodic task does the sampling and debouncing; this only
        // collects the resulting edges. PRESS only -- acting on RELEASE too would advance
        // the focus twice per keypress
        if(light_button_take_event(_key_next, &event) && event == BUTTON_EVENT_PRESS)
                light_ui_input_focus_next(_ui);
        if(light_button_take_event(_key_select, &event) && event == BUTTON_EVENT_PRESS)
                light_ui_input_activate(_ui);
}
