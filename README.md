# light_ui

The UI group of the light framework: the retained widget toolkit (`light_ui`), the input and
sensor drivers that feed it (`light_touch` + CST816T, `light_button`, `light_imu` + QMI8658C),
the presentation layer it draws through (`light_canvas`), audio feedback (`light_audio`), and
the demo applications that exercise all of it on real boards.

These modules were separate repositories, vendored as git submodules by every consumer and
kept in step by hand. They are one project now, resolved by path -- the same consolidation
`light_display` made for the display stack. A consumer adds the whole group with:

```cmake
light_resolve_project(LIGHT_UI light_ui MARKER module/light_ui/CMakeLists.txt)
add_subdirectory(${LIGHT_UI_PATH} light_ui_group)
```

Every library module is an INTERFACE library: it compiles nothing until a target links it, so
consumers pay only for what they use. The `MARKER` matters because the group and its
widget-toolkit module share a name -- see the note in CMakeLists.txt.

## Layout

- `module/light_ui` -- the widget toolkit: windows, buttons, labels, stack layout with min/max
  constraints, scrollable containers, focus and touch input (tap-versus-drag tracking)
- `module/light_canvas` -- frame pacing, double buffering and dirty-region flushing over
  light_display
- `module/light_touch`, `module/light_touch_cst816t` -- touch state, gesture recognition, and
  the CST816T controller driver
- `module/light_button` -- debounced GPIO keys
- `module/light_imu`, `module/light_imu_qmi8658` -- orientation tracking and the QMI8658C driver
- `module/light_audio` -- PCM and tone playback on a PWM pin
- `module/light_ui_demo_common` -- the shared demo: one widget tree, pages, an interactive
  console command tree, driven identically by touch or buttons
- `module/light_ui_demo_touch169`, `module/light_ui_demo_po13` -- the demo on the Waveshare
  RP2350-Touch-LCD-1.69 and on the Pico-OLED-1.3 button rig
- `module/light_ui_hw_ws_touch169`, `module/light_ui_hw_po13` -- those boards' wiring
  (pins, device construction), shared with screen-test's bring-up apps

## Building the demos

Standalone, this project owns the demo images. With the framework, light_display, pico-sdk and
font-crusher checked out as siblings:

```
scripts/build.ps1 -Target light_ui_demo_touch169
scripts/flash.ps1 -Target light_ui_demo_touch169     # 1200-baud BOOTSEL over USB
scripts/debug.ps1 -Target light_ui_demo_po13 -Batch  # the po13 rig flashes over SWD
scripts/test.ps1                                     # the group's host test suite
```
