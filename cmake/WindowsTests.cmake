find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(FFMPEG_CLI ffmpeg REQUIRED)
add_executable(windows-storage-test tests/unit/test_windows_storage.c)
target_link_libraries(windows-storage-test PRIVATE papo_sessions)
papo_add_test(NAME windows-storage COMMAND windows-storage-test)
papo_add_test(NAME windows-terminal COMMAND ${Python3_EXECUTABLE}
              ${PROJECT_SOURCE_DIR}/tests/terminal/test_windows.py $<TARGET_FILE:papo-tui>)
papo_set_tests_properties(windows-terminal PROPERTIES TIMEOUT 180)
add_executable(windows-input-probe tests/probes/windows_input_probe.c
                                   src/platform/windows/terminal.c)
target_link_libraries(windows-input-probe PRIVATE papo_send_editor)
target_compile_options(windows-input-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
papo_add_test(
    NAME windows-input COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/terminal/test_windows_input.py $<TARGET_FILE:windows-input-probe>)
papo_set_tests_properties(windows-input PROPERTIES TIMEOUT 30)
