
add_library(light_ui_hw_ws_touch169 INTERFACE)

target_include_directories(
        light_ui_hw_ws_touch169 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_ui_hw_ws_touch169 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/hardware.c
)

target_link_libraries(
        light_ui_hw_ws_touch169 INTERFACE
        light_audio
        light_backlight
        light_display
        light_display_st7789
        light_touch
        light_touch_cst816t
        light_imu
        light_imu_qmi8658
        light_core
)
