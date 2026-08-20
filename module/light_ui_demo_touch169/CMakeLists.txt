
add_executable(light_ui_demo_touch169)

target_include_directories(
        light_ui_demo_touch169 PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

# 240x280 px over 27.9x32.6 mm -- derived from the panel's 1.69" diagonal at this pixel
# count, so crush resolves point size against the physical dimensions it actually has
crush_add_font_target(light_ui_demo_touch169_font
        FONT ${FONT_CRUSHER_PATH}/test_resource/fonts/TypeLightSans.ttf
        DISPLAY_NAME ws_touch169 DISPLAY_WIDTH 240 DISPLAY_HEIGHT 280 DISPLAY_DIMENSION 27.9x32.6
        RENDER_NAME light_ui_demo_touch169 POINT_SIZE 14 PIXEL_SIZE 16
)

target_sources(
        light_ui_demo_touch169 PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src/app.c
)

target_link_libraries(
        light_ui_demo_touch169 PUBLIC
        light_ui_demo_common
        light_ui_hw_ws_touch169
        light_ui_demo_touch169_font
        light_ui
        light_touch
        light_imu
        light_audio
        light_backlight
        light_draw
        # dispatches both the build-time command baked in by LIGHT_BOOT_COMMAND and the lines
        # typed at the USB CDC console at runtime (the command tree and console feeder live in
        # light_ui_demo_common; this app's part is loading &light_cli so cli_task runs)
        light_cli
        light_core
)

# see screentest_ws_touch169's CMakeLists for why the multicore worker is disabled on this
# board: the core1 message-stream worker races TinyUSB's device stack now that stdio is USB
# CDC, so the message queue is drained from the main loop instead
target_compile_definitions(
        light_ui_demo_touch169 PUBLIC
        #   still 0: light_core's own core 1 worker stays disabled, because the drain now
        # happens inside the platform's core 1 USB worker instead (see LIGHT_PLATFORM_USB_ON_CORE1
        # below). Setting this to 1 as well would launch a SECOND core 1 worker
        LIGHT_PLATFORM_HAS_MULTICORE_WORKER=0
        #   put TinyUSB and the message-queue drain on core 1 TOGETHER. The comment above about
        # the core1 worker racing TinyUSB was true of the old arrangement, where the worker ran
        # on core 1 and TinyUSB on core 0; the race is between cores, not with core 1 as such,
        # so co-locating both on core 1 removes it. What this buys is the CDC busy-wait in
        # stdio_usb_out_chars() no longer being paid by the task loop that renders the UI.
        LIGHT_PLATFORM_USB_ON_CORE1=1
        #   the two SDK knobs that arrangement depends on. Without them the SDK pumps tud_task()
        # from core 0 -- via a low priority IRQ, or an alarm on the default pool, which lives on
        # core 0 -- and calls tusb_init() there too, which is what decides the core USBCTRL_IRQ
        # is enabled on. Either one alone reintroduces exactly the cross-core access being removed
        PICO_STDIO_USB_ENABLE_TINYUSB_INIT=0
        PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=0
        #   the last thing on core 0 that still wrote to stdio was the SDK's own panic(), which
        # prints from whichever core died. This routes it through light_core_port_panic(), which
        # formats on the dying core and hands the printing to core 1. DOES_NOT_RETURN=1 is what
        # makes the format string and its arguments arrive intact (see pico/platform/panic.h)
        PICO_PANIC_FUNCTION=light_core_port_panic
        PICO_PANIC_FUNCTION_DOES_NOT_RETURN=1
        #   set to prevent long console lines from truncating
        CFG_TUD_CDC_TX_BUFSIZE=512
        #   how long stdio_usb_out_chars() busy-waits for the host to drain the CDC FIFO
        # before giving up on a write. The SDK default is 500000 (500 ms) PER WRITE. This was
        # 1 ms when the message queue was drained from the main loop, where a slow host stalled
        # rendering, touch and audio (measured: 13.2 s against a capture script that reopened
        # its output file per line). Under LIGHT_PLATFORM_USB_ON_CORE1 (above) every stdio
        # write happens on the core 1 worker, so the timeout now bounds only log-drain latency
        # -- the UI never pays it.
        #   and 1 ms is BELOW the transport's own cadence: TinyUSB's CDC moves at most one
        # 64-byte packet per USB frame (1 ms) per flush cycle, so any burst that fills the
        # 512-byte FIFO saw its writes time out in lockstep with the frame clock. Measured: the
        # shutdown path's module-unload logging arrived with every line after the sixth cut at
        # exactly 64 bytes, identically on every run. 5 ms sits comfortably above the per-packet
        # cadence while still bounding the stalled-host case: a wedged reader costs at most
        # 5 ms per message on the log drain, and the 64-slot message queue in front of it is
        # what actually protects core 0
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=5000
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK)
        pico_add_extra_outputs(light_ui_demo_touch169)
        # this board has no SWD pads exposed and UART console access has been unreliable
        # over an external debug probe -- USB CDC gives a serial console over the same cable
        # already used to flash the board
        pico_enable_stdio_usb(light_ui_demo_touch169 1)
        pico_enable_stdio_uart(light_ui_demo_touch169 0)
endif()
