#include <light_touch_cst816t.h>

#include "light_touch_cst816t_internal.h"

#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
#include <hardware/gpio.h>
#endif

struct cst816t_state {
        struct io_context *io_ctx;
        // TP_INT -- read directly as a GPIO, not part of light_ioport's
        // io_context (see light_touch_cst816t.h)
        uint8_t pin_int;
        // tracks the previous sample's touch_active so poll() can log on the down-edge
        // only, not every tick a touch stays held -- light_alloc() isn't zeroed, so this
        // must be set explicitly in _spawn_context(), same as every other driver state
        // field in this codebase
        bool was_active;
        // most recent non-zero GestureID seen during the current touch. latched rather
        // than read once at release, because the engine reports its result in whichever
        // frame it recognises the gesture -- not necessarily the frame that reports the
        // finger lifting. cleared on each new touch-down so a stale code can never be
        // attributed to the following touch
        uint8_t last_gesture;
        // when the controller last answered with a report, and how many polls have since
        // gone without one. together these infer the finger lifting -- see
        // _cst816t_infer_release()
        uint32_t last_report_ms;
        uint32_t idle_polls;
        // consecutive reads the controller has not answered, capped where the backoff caps.
        // paces the retry interval -- see _cst816t_read_interval_ms()
        uint8_t unanswered;
        // when the current run of unanswered reads began, and when the controller was last
        // reset to break one. Both drive the recovery in _cst816t_poll()
        uint32_t unanswered_since_ms;
        uint32_t last_recover_ms;
        // when a read was last ATTEMPTED, answered or not. paces the cadence below, and is
        // deliberately separate from last_report_ms: a sleeping controller is attempted often
        // but reports nothing, and conflating the two would retry it as fast as the loop spins
        uint32_t last_attempt_ms;
};

// the controller reports at ~83 Hz while a finger is down (measured on the
// RP2350-Touch-LCD-1.69: INT asserted 1-3 ms, idle ~10 ms, a ~12 ms cycle, worst observed
// gap 23 ms). Five times the nominal cadence, and still far below the ~150-250 ms between
// the two taps of a human double-tap -- which is the number that matters at the top end,
// since a release declared late swallows the second tap
#define CST816T_RELEASE_TIMEOUT_MS 60
// ...but elapsed time alone would also "expire" a touch that is still very much held, if
// the poll loop were stalled long enough by a busy frame. Requiring some polls to have
// actually looked distinguishes "nothing is arriving" from "nobody was asking". At the
// measured ~900 polls/sec this is ~9 ms of healthy running, a floor rather than a threshold
#define CST816T_RELEASE_MIN_POLLS 8
//   how often to read the controller when INT has not asked us to. The interrupt is only a
// hint: at 1-3 ms asserted out of every 12 ms, a poll loop that has slowed to ~60 Hz under
// rendering load samples straight past most assertions, and a touch whose pulse is missed
// used to be dropped outright -- the controller was never read, so the tap simply never
// happened. Reading on our own schedule removes the need to catch the pulse at all.
//   matched to the controller's own ~83 Hz report rate: faster only re-reads the same frame
// while sharing a bus with the IMU, slower starts adding latency a finger can feel
#define CST816T_POLL_INTERVAL_MS 10
//   ...but only while the controller is actually answering. Retrying a silent controller at
// that rate means a timed-out, aborted I2C transfer every 10 ms, and aborted transfers are a
// good way to wedge a bus that this chip shares with the IMU. Measured before this backoff
// existed: the panel went deaf for 9.5 s, 7.6 s and once 20.2 s at a stretch, with the main
// loop, display and audio all running perfectly throughout -- the UI simply stopped
// responding while every task returned on time.
//   doubling per consecutive failure up to this ceiling. INT still forces an immediate read,
// so backing off costs nothing when the controller wakes: it announces itself and is read on
// that very poll, rather than waiting out the interval
#define CST816T_POLL_BACKOFF_MAX_MS 160
//   after this many unanswered reads the cadence stops entirely and INT becomes the only way
// in. The controller is asleep, and sleeping is the correct thing for it to be doing on an
// untouched panel -- it wakes on touch and announces itself on the interrupt line, which is
// precisely the case the interrupt exists for.
//   measured why this matters: polling straight through a standby left the panel deaf 66 s
// out of 110, in a ~1.2 s awake / ~2.4 s silent cycle, because every retry was a timed-out
// aborted transfer against a chip that had nothing to say. The cadence earns its keep while
// a finger is actually down and a report is genuinely expected every ~12 ms; against a
// sleeping controller it is pure interference
#define CST816T_QUIET_AFTER_FAILS 4
// how long the controller may stay silent before its reset line is pulsed, and how long to
// leave it alone afterwards so it can boot. Comfortably longer than the ~1.6 s standby blocks
// seen when the panel recovers on its own, so ordinary quiet is never mistaken for a wedge
#define CST816T_RECOVER_AFTER_MS 2000
#define CST816T_RECOVER_COOLDOWN_MS 1000
static uint32_t _cst816t_read_interval_ms(uint8_t unanswered)
{
        uint32_t interval = (uint32_t)CST816T_POLL_INTERVAL_MS << unanswered;
        return interval > CST816T_POLL_BACKOFF_MAX_MS
                        ? CST816T_POLL_BACKOFF_MAX_MS : interval;
}

static struct touch_driver_context *_cst816t_spawn_context();
static void _cst816t_destroy_context(struct touch_driver_context *ctx);
static void _cst816t_init(struct touch_device *dev);
static void _cst816t_reset(struct touch_device *dev);
static bool _cst816t_poll(struct touch_device *dev);
static bool _cst816t_read_gesture(struct touch_device *dev, uint8_t *type_out);

static struct touch_driver _driver_cst816t = {
        .name = "touch.driver:cst816t",
        .spawn_context = _cst816t_spawn_context,
        .destroy_context = _cst816t_destroy_context,
        .init_device = _cst816t_init,
        .reset = _cst816t_reset,
        .poll = _cst816t_poll,
        .read_gesture = _cst816t_read_gesture
};

struct touch_driver *light_touch_driver_cst816t()
{
        return &_driver_cst816t;
}

static struct touch_driver_context *_cst816t_spawn_context()
{
        struct touch_driver_context *ctx = light_alloc(sizeof(struct touch_driver_context));
        ctx->driver = light_touch_driver_cst816t();
        ctx->state = light_alloc(sizeof(struct cst816t_state));
        struct cst816t_state *state = (struct cst816t_state *) ctx->state;
        state->was_active = false;
        state->last_gesture = CST816T_GESTURE_NONE;
        state->last_report_ms = light_platform_get_time_since_init();
        state->last_attempt_ms = state->last_report_ms;
        state->idle_polls = 0;
        state->unanswered = 0;
        state->unanswered_since_ms = 0;
        state->last_recover_ms = 0;
        return ctx;
}

//   the counterpart to _cst816t_spawn_context(), called from the device release path
// when the device this context was spawned for is freed. Frees in the reverse of
// the order allocated: the state first, then the context that points at it
static void _cst816t_destroy_context(struct touch_driver_context *ctx)
{
        light_free((void *)ctx->state);
        light_free(ctx);
}

static void _cst816t_gpio_int_setup(uint8_t pin_int)
{
#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
        gpio_init(pin_int);
        gpio_set_dir(pin_int, false);
        gpio_pull_up(pin_int);
#endif
}
//   active-low: the controller pulls this line low when it has new touch data ready. A
// PULSE, not a level held for the duration of a touch -- measured at 1-3 ms asserted, ~10 ms
// idle, a ~12 ms cycle while a finger is down.
//
//   an earlier note here also claimed the controller "only responds to I2C reads for a short
// window after" asserting. That is FALSE, and worth stating plainly because it is the kind of
// claim that shapes a driver around a constraint it does not have. Measured directly: 360
// reads taken with INT idle succeeded, in unbroken multi-second runs, and reported the
// correct state throughout -- fingers=1 for the whole of a hold, 0 once released. Reads do
// fail in long contiguous blocks of 0.7-1.6 s, but that is the controller's auto-sleep, which
// it enters after a spell without touches and leaves when touched again; it is a mode, not a
// per-read timing window. So this line is a useful HINT that fresh data is waiting, and
// nothing more -- see _cst816t_poll(), which no longer depends on catching it
static bool _cst816t_int_asserted(uint8_t pin_int)
{
#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
        return !gpio_get(pin_int);
#else
        return false;
#endif
}

static void _cst816t_init(struct touch_device *dev)
{
        struct cst816t_state *state = (struct cst816t_state *) dev->driver_ctx->state;
        _cst816t_gpio_int_setup(state->pin_int);
        _cst816t_reset(dev);

        uint8_t chip_id = 0;
        // log-and-continue, not hard-fail: the register map is cross-referenced from two
        // open-source drivers, not a primary datasheet -- if this turns out wrong on real
        // hardware, a mismatched chip ID shouldn't prevent poll() from still being tried
        if(!light_ioport_read_register(state->io_ctx, CST816T_REG_CHIP_ID, &chip_id, 1)) {
                light_warn("failed to read chip ID for device '%s'", dev->header.id);
        } else if(chip_id != CST816T_CHIP_ID) {
                light_warn("unexpected chip ID for device '%s': got 0x%x, expected 0x%x",
                                dev->header.id, chip_id, CST816T_CHIP_ID);
        } else {
                light_info("cst816t chip ID confirmed for device '%s': 0x%x", dev->header.id, chip_id);
        }

        //   NOT attempting to disable the controller's auto-sleep here, having tried it:
        // writing 1 to 0xFE (DisAutoSleep, per the same open-source cross-reference the rest
        // of this map comes from) is ACKNOWLEDGED by the chip and changes nothing -- it still
        // went to standby on the same ~1.2 s awake / ~2.4 s asleep cycle. So on this part that
        // register either is not DisAutoSleep or does not mean what the cross-reference says.
        //   left out rather than left in as a hopeful no-op, because a write that lands and
        // does nothing is worse than no write: it reads like a solved problem. Sleeping is
        // correct behaviour for an untouched panel anyway; what matters is not interfering
        // with it, which _cst816t_poll() handles by going quiet and waiting on the interrupt
}
static void _cst816t_reset(struct touch_device *dev)
{
        struct cst816t_state *state = (struct cst816t_state *) dev->driver_ctx->state;
        light_ioport_signal_reset(state->io_ctx);
}
//   INT is a data-ready PULSE, not a level held for the duration of a touch, so the line
// says nothing about whether a finger is still present -- it is idle between every pair of
// reports on a touch that is very much ongoing. The controller does send a finger_num == 0
// frame when the finger lifts, but that frame is one ~2 ms window among many and is missed
// often: over one instrumented session only five arrived, and touch_active was once observed
// stuck true for 9.7 SECONDS after a release, across 8864 polls.
//
//   that stuck flag is not cosmetic. Callers detect a press on the down-edge of touch_active,
// so while it is wrongly true every subsequent tap produces no edge and is silently dropped --
// the user taps, nothing happens, they tap again and it works. Gesture tracking finalises on
// the same falling edge, so lost releases eat swipes too. It showed up worst right after a
// page transition, whose full-framebuffer copy stalls the poll loop exactly while the
// navigating tap is being released.
//
//   so the release is inferred rather than waited for: no report for long enough, while the
// loop was demonstrably still looking, means the finger is gone. Deliberately NOT clearing on
// the first idle poll, which would end every touch between its own 10 ms reports.
//
//   now a BACKSTOP rather than the main path. Since poll() reads on its own cadence instead
// of only inside the interrupt window, an ordinary release is read directly as finger_num == 0
// within an interval of it happening. What is left for this to catch is the controller going
// quiet while a finger is still believed down -- sleeping mid-touch, or a bus that stops
// answering -- which no amount of reading will report
static void _cst816t_infer_release(struct touch_device *dev, struct cst816t_state *state)
{
        if(!dev->touch_active)
                return;
        if(state->idle_polls < CST816T_RELEASE_MIN_POLLS)
                return;
        if(light_platform_get_time_since_init() - state->last_report_ms
                        < CST816T_RELEASE_TIMEOUT_MS)
                return;

        dev->touch_active = false;
        // kept in step with touch_active, so the next real touch is still seen as a
        // down-edge here and clears the latched gesture as it would have anyway
        state->was_active = false;
}

static bool _cst816t_poll(struct touch_device *dev)
{
        struct cst816t_state *state = (struct cst816t_state *) dev->driver_ctx->state;
        uint32_t now = light_platform_get_time_since_init();

        //   INT still gets first refusal, because when it IS asserted there is definitely a
        // fresh frame waiting and reading it immediately is the lowest-latency path. But it is
        // no longer the only way in: missing the pulse now costs at most one interval instead
        // of losing the touch entirely
        //   once it has gone quiet, INT is the only thing that gets us in. Until then the
        // cadence backs off but keeps trying, which is what covers a report whose pulse was
        // sampled past while a finger is down
        bool int_asserted = _cst816t_int_asserted(state->pin_int);
        bool quiet = state->unanswered >= CST816T_QUIET_AFTER_FAILS;
        if(!int_asserted && (quiet
                        || now - state->last_attempt_ms
                                < _cst816t_read_interval_ms(state->unanswered))) {
                if(state->idle_polls < UINT32_MAX)
                        state->idle_polls++;
                _cst816t_infer_release(dev, state);
                return false;
        }
        state->last_attempt_ms = now;

        // one burst covering the gesture register and the touch data together -- see
        // CST816T_REG_GESTURE for the layout and why they're read as a single frame
        uint8_t data[CST816T_TOUCH_DATA_LEN];
        if(!light_ioport_read_register(state->io_ctx, CST816T_REG_GESTURE, data, CST816T_TOUCH_DATA_LEN)) {
                // backs off the retry rate; INT still forces a read regardless
                if(state->unanswered < CST816T_QUIET_AFTER_FAILS)
                        state->unanswered++;
                if(!state->unanswered_since_ms)
                        state->unanswered_since_ms = now;

                //   a controller that has been silent this long is not between reports, it is
                // in a standby it will not leave on its own -- measured going deaf for up to
                // 16 s while being tapped the whole time, INT never asserting. Pulsing its
                // reset line is the one way back that does not depend on it answering
                // anything, and costs a touch that was not being reported anyway.
                //   the cooldown matters as much as the trigger: the controller needs time to
                // come up, during which reads still fail, and resetting it again on every one
                // of those would hold it permanently in reset -- turning a recoverable stall
                // into a dead panel
                //   gated on INT, which is what separates a wedge from a nap. A controller
                // that is merely asleep says nothing and asserts nothing, and resetting it for
                // that is both pointless and destructive -- an earlier version of this did
                // exactly that, firing 23 times in 110 s against a chip whose only crime was
                // an untouched panel. One that is asserting INT has data and wants to be read,
                // so if it will still not answer, something is genuinely stuck
                if(int_asserted
                                && now - state->unanswered_since_ms > CST816T_RECOVER_AFTER_MS
                                && now - state->last_recover_ms > CST816T_RECOVER_COOLDOWN_MS) {
                        state->last_recover_ms = now;
                        light_warn("touch controller asserting INT but not answering for %u ms;"
                                " resetting it",
                                (unsigned)(now - state->unanswered_since_ms));
                        _cst816t_reset(dev);
                }

                //   not an error, and deliberately not logged: the controller sleeps after a
                // spell without touches and answers nothing at all until the next one wakes
                // it, so this is the ordinary resting state of an untouched panel. Measured at
                // ~1 ms to fail, which is what makes retrying on a cadence affordable
                if(state->idle_polls < UINT32_MAX)
                        state->idle_polls++;
                _cst816t_infer_release(dev, state);
                return false;
        }

        // the controller answered, so whatever it says is current -- including finger_num == 0
        // on release, which is now read directly on the next interval rather than having to be
        // caught in the one frame the controller happened to announce it in
        state->unanswered = 0;
        state->unanswered_since_ms = 0;
        state->idle_polls = 0;
        state->last_report_ms = now;

        uint8_t gesture_id = data[0];
        uint8_t finger_num = data[1];
        dev->touch_active = finger_num > 0;
        if(dev->touch_active) {
                dev->x = ((uint16_t)(data[2] & 0x0F) << 8) | data[3];
                dev->y = ((uint16_t)(data[4] & 0x0F) << 8) | data[5];
        }

        if(dev->touch_active && !state->was_active) {
                // discard whatever the engine reported for the previous touch before
                // latching anything for this one
                state->last_gesture = CST816T_GESTURE_NONE;
                light_info("touch down: device '%s', x=%d, y=%d", dev->header.id, dev->x, dev->y);
        }
        if(gesture_id != CST816T_GESTURE_NONE)
                state->last_gesture = gesture_id;

        state->was_active = dev->touch_active;
        return true;
}
static bool _cst816t_read_gesture(struct touch_device *dev, uint8_t *type_out)
{
        struct cst816t_state *state = (struct cst816t_state *) dev->driver_ctx->state;

        switch(state->last_gesture) {
        // the vertical codes are mapped to their OPPOSITE, which is what the hardware
        // actually does: the code the reference drivers call "swipe up" is reported for a
        // swipe toward increasing y -- downward in the very coordinate space this same
        // controller reports touches in. confirmed on hardware, and confirmed to affect
        // only the vertical axis: left/right need no such flip.
        //
        // whether those upstream names are simply wrong, or the gesture engine uses a
        // different vertical convention than its coordinate output, can't be settled
        // without a primary datasheet. the constants keep the reference drivers' names so
        // this still cross-references cleanly against them, and the correction lives here
        case CST816T_GESTURE_SWIPE_UP:
                *type_out = TOUCH_GESTURE_SWIPE_DOWN;
                return true;
        case CST816T_GESTURE_SWIPE_DOWN:
                *type_out = TOUCH_GESTURE_SWIPE_UP;
                return true;
        case CST816T_GESTURE_SWIPE_LEFT:
                *type_out = TOUCH_GESTURE_SWIPE_LEFT;
                return true;
        case CST816T_GESTURE_SWIPE_RIGHT:
                *type_out = TOUCH_GESTURE_SWIPE_RIGHT;
                return true;
        default:
                // nothing latched, or one of the click/long-press codes light_touch has no
                // equivalent for. declining lets it fall back to its own classification,
                // which correctly finds no swipe in a stationary touch
                return false;
        }
}

struct touch_device *light_touch_cst816t_create_device(
        uint8_t *name, uint16_t x_max, uint16_t y_max, struct io_context *io, uint8_t pin_int)
{
        // io_ctx/pin_int must be attached to the driver state before the device is
        // registered: adding it to the object tree (via light_touch_init_device())
        // immediately triggers init_device(), which reads both -- same rationale/pattern
        // as every display driver's create_device() (see e.g. light_display_sh1106's)
        struct touch_device *dev = light_object_alloc(sizeof(struct touch_device));
        struct touch_driver_context *driver_ctx = _cst816t_spawn_context();
        struct cst816t_state *state = (struct cst816t_state *) driver_ctx->state;
        state->io_ctx = io;
        state->pin_int = pin_int;

        return light_touch_init_device(dev, driver_ctx, x_max, y_max, "%s", name);
}
