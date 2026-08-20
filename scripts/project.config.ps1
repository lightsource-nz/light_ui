# Per-project defaults for the light_ui group.
#
# This project owns the flashable light_ui demo applications -- they moved here from
# screen-test along with the modules they exercise -- so these are the presets and targets the
# shared scripts build, flash and debug. The library modules need nothing from this file; a
# consumer (screen-test) builds them through its own presets.
@{
        Name = 'light_ui'

        Trees = @{
                'conf-light_ui-host-debug'                = 'build-host'
                #   split by CHIP, as crossfire and screen-test are: the po13 rig has been a
                # Pico 2 in an SWD dock since its 2026-08 rebuild
                'conf-light_ui-pico2-debug'               = 'build-pico2'
                'conf-light_ui-waveshare-touch169-debug'  = 'build-waveshare-touch169'
        }

        Targets = @{
                #   the po13 rig flashes over SWD (scripts/debug.ps1 -Batch); Flash='swd'
                # records that light-flash.ps1's BOOTSEL path is not how an image reaches it
                'light_ui_demo_po13'     = @{ Preset = 'conf-light_ui-pico2-debug'; Flash = 'swd' }
                'light_ui_demo_touch169' = @{ Preset = 'conf-light_ui-waveshare-touch169-debug'; Flash = 'uf2' }
        }

        Expect = @{
                'conf-light_ui-host-debug'               = @{ LIGHT_PLATFORM = 'HOST'; LIGHT_SYSTEM = 'HOST_OS' }
                'conf-light_ui-pico2-debug'              = @{ LIGHT_PLATFORM = 'TARGET'; LIGHT_BOARD = 'pico2'; PICO_PLATFORM = 'rp2350-arm-s' }
                'conf-light_ui-waveshare-touch169-debug' = @{ LIGHT_PLATFORM = 'TARGET'; LIGHT_BOARD = 'waveshare_rp2350_touch_lcd_1.69'; PICO_PLATFORM = 'rp2350-arm-s' }
        }

        # which OpenOCD config and SVD belong to which board -- see screen-test's config for
        # why getting this pairing wrong misbehaves rather than erroring
        Debug = @{
                'conf-light_ui-pico2-debug' = @{
                        Config = 'openocd-rp2350.cfg'
                        Svd    = '../pico-sdk/src/rp2350/hardware_regs/RP2350.svd'
                }
                'conf-light_ui-waveshare-touch169-debug' = @{
                        Config = 'openocd-rp2350.cfg'
                        Svd    = '../pico-sdk/src/rp2350/hardware_regs/RP2350.svd'
                }
        }

        DefaultTarget = 'light_ui_demo_touch169'

        Test = @{
                Preset = 'conf-light_ui-host-debug'
                Ctest  = $true
        }

        #   'auto' discovery, HOST_OS explicitly -- the same shape as screen-test's, and for
        # the same reasons: test binaries sit at different depths, and the coverage build is a
        # plain Linux build. What this measures is what the group's host suite actually
        # reaches: light_audio thoroughly, plus the light_draw and framework suites that
        # register through the standalone build. NOTE what is absent rather than at 0%: the
        # widget toolkit (module/light_ui) and light_canvas compile into no test binary, so
        # they do not appear in the report at all -- their behaviour is hardware-verified but
        # unmeasured, which is the gap a future suite should close. The board drivers and hw
        # wiring are target-only and will never appear
        Coverage = @{
                Objects     = 'auto'
                IgnoreRegex = '(/lib/|/usr/|sanitizers/|_deps/|/freetype/|/jansson/)'
                CMakeArgs   = @('-DLIGHT_SYSTEM=HOST_OS', '-DLIGHT_PLATFORM=HOST')
        }
}
