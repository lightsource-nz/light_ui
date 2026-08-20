#include <light_button.h>

#include "light_button_internal.h"

#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
#include <hardware/gpio.h>
#endif

struct button_gpio_state {
        uint8_t pin;
        // true when the switch pulls the pin LOW on press (button to ground, internal
        // pull-up enabled) -- the usual wiring, and what every onboard key on the boards
        // this was written against uses
        bool active_low;
};

static struct button_driver_context *_gpio_spawn_context();
static void _gpio_destroy_context(struct button_driver_context *ctx);
static void _gpio_init(struct button_device *dev);
static void _gpio_reset(struct button_device *dev);
static bool _gpio_read(struct button_device *dev);

static struct button_driver _driver_gpio = {
        .name = "button.driver:gpio",
        .spawn_context = _gpio_spawn_context,
        .destroy_context = _gpio_destroy_context,
        .init_device = _gpio_init,
        .reset = _gpio_reset,
        .read = _gpio_read
};

struct button_driver *light_button_driver_gpio()
{
        return &_driver_gpio;
}

static struct button_driver_context *_gpio_spawn_context()
{
        struct button_driver_context *ctx = light_alloc(sizeof(struct button_driver_context));
        ctx->driver = light_button_driver_gpio();
        ctx->state = light_alloc(sizeof(struct button_gpio_state));
        struct button_gpio_state *state = (struct button_gpio_state *) ctx->state;
        // light_alloc() isn't zeroed, same as every other driver state in this codebase --
        // these are overwritten by create_device() before the device is registered, but a
        // caller reaching the driver through light_button_create_device() gets these
        state->pin = 0;
        state->active_low = true;
        return ctx;
}

//   the counterpart to _gpio_spawn_context(), called from the device release path
// when the device this context was spawned for is freed. Frees in the reverse of
// the order allocated: the state first, then the context that points at it
static void _gpio_destroy_context(struct button_driver_context *ctx)
{
        light_free((void *)ctx->state);
        light_free(ctx);
}

static void _gpio_init(struct button_device *dev)
{
        struct button_gpio_state *state = (struct button_gpio_state *) dev->driver_ctx->state;
#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
        gpio_init(state->pin);
        gpio_set_dir(state->pin, false);
        // the pull has to oppose the press, or the pin floats in the released state and
        // reads as noise -- which debouncing cannot fix, since floating input isn't bounce
        if(state->active_low)
                gpio_pull_up(state->pin);
        else
                gpio_pull_down(state->pin);
#endif
        light_info("gpio button '%s' on pin %d (active %s)",
                        dev->header.id, state->pin, state->active_low ? "low" : "high");
}
static void _gpio_reset(struct button_device *dev)
{
        // a GPIO input has no device-side state to reset -- reconfiguring the pin is the
        // only thing "reset" could mean here, and doing that is exactly _gpio_init()
        _gpio_init(dev);
}
static bool _gpio_read(struct button_device *dev)
{
#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
        struct button_gpio_state *state = (struct button_gpio_state *) dev->driver_ctx->state;
        bool level = gpio_get(state->pin);
        return state->active_low ? !level : level;
#else
        // host builds have no GPIO to read. reporting "not pressed" rather than refusing to
        // build keeps light_button linkable on the host, so the UI logic layered on top of
        // it stays host-testable
        (void)dev;
        return false;
#endif
}

struct button_device *light_button_gpio_create_device(uint8_t *name, uint8_t pin, bool active_low)
{
        // pin/active_low must be attached to the driver state before the device is
        // registered: adding it to the object tree (via light_button_init_device())
        // immediately triggers init_device(), which configures the pin using both -- same
        // rationale/pattern as light_touch_cst816t_create_device()
        struct button_device *dev = light_object_alloc(sizeof(struct button_device));
        struct button_driver_context *driver_ctx = _gpio_spawn_context();
        struct button_gpio_state *state = (struct button_gpio_state *) driver_ctx->state;
        state->pin = pin;
        state->active_low = active_low;

        return light_button_init_device(dev, driver_ctx, "%s", name);
}
