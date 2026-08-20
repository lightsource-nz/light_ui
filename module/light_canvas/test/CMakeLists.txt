
# HOST ONLY -- see the guard in the module's own CMakeLists. these tests exercise the canvas's
# region arithmetic and frame pacing against zero displays, so the binary has to execute on
# the machine doing the building.
add_executable(test_light_canvas ${CMAKE_CURRENT_LIST_DIR}/test_light_canvas.c)

target_link_libraries(test_light_canvas PRIVATE light_canvas)

# one CTest case per test function -- KEEP IN SYNC with test_cases[] in test_light_canvas.c;
# `test_light_canvas --list` prints the names the binary actually has
set(LIGHT_CANVAS_TEST_CASES
        invalidate_normalises_and_clips
        overlapping_regions_merge
        one_addition_bridges_two_regions
        region_overflow_collapses_to_full_canvas
        carry_is_what_the_caller_invalidated
        reset_regions_forgets_everything
        clip_follows_a_rotated_canvas
        frame_open_guard
        frame_pacing_defers_the_next_frame
        double_buffer_swaps_and_suspends
)
foreach(_case ${LIGHT_CANVAS_TEST_CASES})
        add_test(NAME light_canvas.${_case} COMMAND test_light_canvas ${_case})
endforeach()
