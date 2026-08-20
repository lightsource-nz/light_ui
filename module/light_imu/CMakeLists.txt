
add_library(light_imu INTERFACE)

target_include_directories(
        light_imu INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_imu INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/imu.c
)

# light_ioport is an aggregate submodule, reused here for its I2C transport -- it has no
# display/pixel coupling (struct io_context/i2c_state are fully generic), so light_imu
# depends on it the same way light_touch and light_display do
target_link_libraries(
        light_imu INTERFACE
        light_ioport
        light_core
)
