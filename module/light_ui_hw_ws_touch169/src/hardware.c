#include <light_ui_hw_ws_touch169.h>
#include <light_audio.h>
#include <light_backlight.h>
#include <light_display_st7789.h>
#include <light_imu_qmi8658.h>
#include <light_touch_cst816t.h>

// board: Waveshare RP2350-Touch-LCD-1.69 -- ST7789 panel (240x280, 16bpp) plus a CST816T
// capacitive touch controller. moved here verbatim from screentest_ws_touch169's app.c so
// the UI demo app can build against the same board without copying it

// no direct hardware includes any more: the backlight was the last thing this file drove as
// a raw GPIO, and light_backlight owns that pin now

struct display_device *light_ui_hw_ws_touch169_display(void)
{
        struct io_context *io = light_ioport_setup_io_spi_4p(
                PORT_SPI_1,
                ST_DISPLAY_PIN_RESET, ST_DISPLAY_PIN_CS, ST_DISPLAY_PIN_DC,
                ST_DISPLAY_PIN_SCK, ST_DISPLAY_PIN_MOSI);
        // before the device is created, so even the initialisation sequence runs at the
        // faster clock -- if the panel cannot take it, it fails visibly from the first
        // frame rather than only once something animates
        light_ioport_set_spi_clock(io, ST_DISPLAY_SPI_HZ);
        struct display_device *disp = light_display_st7789_create_device(
                "screentest_display_main", ST_DISPLAY_WIDTH, ST_DISPLAY_HEIGHT, io);

        //   row_offset 20 is CONFIRMED, not the community guess it started as. The panel's
        // visible area is GDDRAM rows 20..299, so writing the framebuffer at offset 20 puts
        // row 0 of the buffer at row 0 of the glass and covers it exactly.
        //
        //   measured rather than assumed, by setting it to 0 and looking: the image moved UP
        // by 20, the top 20 rows fell off the glass, and rows 260..279 showed a blank band --
        // unwritten GDDRAM 280..299. Restoring 20 removed the band. Touch corroborates the
        // alignment independently: tapping the extreme top of the visible glass reports y=1
        // and the extreme bottom reports y=278, against a 280-row buffer.
        //
        //   worth stating plainly because the earlier note here recorded the opposite
        // impression -- that offsets had "zero visible effect" -- which was true only of a
        // test confounded by a render context still hardcoded to the OLED rigs' 64x128 1bpp
        // geometry. Anyone re-testing this on the strength of that note would be repeating a
        // measurement that has since been made properly.
        light_display_st7789_set_offset(disp, 0, 20);
        // creating the device already cleared the panel -- but that ran with the offset
        // still at its default of 0, so it blanked GDDRAM rows 0..279 while every update
        // from here on writes rows 20..299. that leaves rows 280..299 holding whatever
        // powered up in them. a full-frame update happens to paint over that band every
        // frame, which is why it stayed hidden until updates became region-limited and
        // stopped touching it. clear again now that the offset is right
        light_display_command_clear(disp, 0);

        light_info("display pipeline setup complete","");
        return disp;
}

struct touch_device *light_ui_hw_ws_touch169_touch(void)
{
        // shared I2C1 bus (also used by IMU/RTC, not yet implemented) -- setup_io_i2c
        // re-inits the same peripheral each time it's called, harmless as long as the
        // params (scl/sda) agree, which they will once IMU/RTC support lands
        struct io_context *io = light_ioport_setup_io_i2c(
                PORT_I2C_1, ST_TOUCH_PIN_RST, CST816T_I2C_ADDR,
                ST_TOUCH_PIN_SCL, ST_TOUCH_PIN_SDA);
        struct touch_device *touch = light_touch_cst816t_create_device(
                "screentest_touch_main", ST_TOUCH_X_MAX, ST_TOUCH_Y_MAX, io, ST_TOUCH_PIN_INT);

        light_info("touch pipeline setup complete","");
        return touch;
}

struct backlight_device *light_ui_hw_ws_touch169_backlight(void)
{
        // the backlight is a plain pin rather than anything the ST7789 knows about, which is
        // why it was driven straight from here as an on/off GPIO before light_backlight
        // existed. active high: this board's enable line sources into the LED driver
        return light_backlight_pwm_create_device(
                "screentest_backlight_main", ST_DISPLAY_PIN_BL, false);
}

struct audio_device *light_ui_hw_ws_touch169_audio(void)
{
#ifdef ST_AUDIO_PIN_BUZZER
        return light_audio_pwm_create_device("screentest_audio_main", ST_AUDIO_PIN_BUZZER);
#else
        // the pin is not known yet -- see ST_AUDIO_PIN_BUZZER. returning NULL rather than
        // guessing keeps PWM off a pin that might be wired to something else entirely
        light_warn("no buzzer pin configured for this board; audio disabled","");
        return NULL;
#endif
}

struct imu_device *light_ui_hw_ws_touch169_imu(void)
{
        // its own io_context despite sharing the bus with the touch controller: an
        // io_context carries the target's I2C address, and these two answer to different
        // ones. setup_io_i2c re-inits the same peripheral, which is harmless as long as the
        // scl/sda parameters agree -- and they do, being the same two pins.
        //
        // no reset line: the IMU has none of its own on this board, and pulsing the touch
        // controller's out from under it would be actively wrong, hence PIN_NONE
        struct io_context *io = light_ioport_setup_io_i2c(
                PORT_I2C_1, LIGHT_IOPORT_PIN_NONE, QMI8658_I2C_ADDR,
                ST_TOUCH_PIN_SCL, ST_TOUCH_PIN_SDA);
        struct imu_device *imu = light_imu_qmi8658_create_device(
                "screentest_imu_main", io, ST_IMU_PIN_INT1);

        // declare the mounting before anything reads a sample, so orientation is classified
        // in the device frame from the very first poll rather than settling wrong and then
        // being corrected
        light_imu_set_axis_map(imu, ST_IMU_AXIS_MAP);

        light_info("imu pipeline setup complete","");
        return imu;
}
