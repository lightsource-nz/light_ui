#include <light_ui_hw_po13.h>
#include <light_display_sh1107.h>

// board: Waveshare Pico-OLED-1.3 ("po13"), optionally with a second bare SH1107 panel of
// the same geometry wired to spi0. moved here verbatim from screentest_po13's app.c so the
// UI demo app can build against the same board without copying it

struct display_device *light_ui_hw_po13_display_main(void)
{
        struct io_context *io = light_display_po13_setup_io_spi_4p(PORT_SPI_1);
        return light_display_po13_create_device("screentest_display_main", io);
}

struct display_device *light_ui_hw_po13_display_sec(void)
{
        struct io_context *io = light_ioport_setup_io_spi_4p(
                        PORT_SPI_0,
                        ST_DISPLAY_1_PIN_RESET,
                        ST_DISPLAY_1_PIN_CS,
                        ST_DISPLAY_1_PIN_DC,
                        ST_DISPLAY_1_PIN_SCK,
                        ST_DISPLAY_1_PIN_TX);
        return light_display_sh1107_create_device("screentest_display_sec",
                        PO13_WIDTH, PO13_HEIGHT, PO13_BPP, io);
}
