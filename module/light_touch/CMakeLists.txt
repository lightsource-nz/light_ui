
add_library(light_touch)

target_include_directories(
        light_touch PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_touch PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/touch.c
)

# light_ioport is an aggregate submodule, reused here for its I2C transport --
# it has no display/pixel coupling (struct io_context/i2c_state are fully generic), so
# light_touch depends on it the same way light_display does
target_link_libraries(
        light_touch PUBLIC
        light_ioport
        light_core
)
