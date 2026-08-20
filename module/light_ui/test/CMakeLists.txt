
# HOST ONLY -- see the guard in the module's own CMakeLists. these tests drive the widget
# toolkit's layout, scrolling, focus, hit-testing and touch tracking against a canvas with no
# displays, so the binary has to execute on the machine doing the building.
add_executable(test_light_ui ${CMAKE_CURRENT_LIST_DIR}/test_light_ui.c)

# light_ui is an INTERFACE library carrying its own sources and its dependencies (canvas,
# display, draw, cli, core), so linking it is all the test needs
target_link_libraries(test_light_ui PRIVATE light_ui)

# one CTest case per test function -- a failure then names itself in the ctest summary.
# KEEP IN SYNC with test_cases[] in test_light_ui.c; `test_light_ui --list` prints the names
# the binary actually has, and an unknown name here exits non-zero rather than passing quietly
set(LIGHT_UI_TEST_CASES
        stack_divides_rows_evenly
        stack_pins_min_height_and_overflows
        stack_caps_max_height
        flush_corners_on_non_scroll_last_row
        scroll_end_cap_rounds_last_row
        scroll_clamps_at_both_ends
        scroll_stop_is_the_flush_edge
        scroll_shifts_children_uniformly
        focus_cycles_and_wraps
        focus_skips_hidden_and_disabled
        focus_scrolls_target_into_view
        press_focuses_and_activates
        press_ignores_rows_outside_viewport
        touch_tap_activates_on_release
        touch_drag_scrolls_and_is_not_a_tap
        touch_travel_without_target_is_neither
        widget_command_queues_for_cli_task
        build_binds_and_orders
        pages_navigate_and_return
)
foreach(_case ${LIGHT_UI_TEST_CASES})
        add_test(NAME light_ui.${_case} COMMAND test_light_ui ${_case})
endforeach()
