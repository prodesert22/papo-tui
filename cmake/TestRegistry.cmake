# One registry for both native platforms. Python discovery parses AST, never runs fixtures.
function(papo_add_test)
    cmake_parse_arguments(SPEC "" "NAME" "COMMAND" ${ARGN})
    set_property(GLOBAL APPEND PROPERTY PAPO_REGISTERED_TESTS "${SPEC_NAME}")
    set_property(GLOBAL PROPERTY "PAPO_COMMAND_${SPEC_NAME}" "${SPEC_COMMAND}")
endfunction()

function(papo_set_tests_properties)
    set(names "")
    set(index 0)
    while(index LESS ARGC)
        set(value "${ARGV${index}}")
        math(EXPR index "${index} + 1")
        if(value STREQUAL "PROPERTIES")
            break()
        endif()
        list(APPEND names "${value}")
    endwhile()
    while(index LESS ARGC)
        set(key "${ARGV${index}}")
        math(EXPR index "${index} + 1")
        set(value "${ARGV${index}}")
        math(EXPR index "${index} + 1")
        foreach(name IN LISTS names)
            set_property(GLOBAL PROPERTY "PAPO_${name}_${key}" "${value}")
        endforeach()
    endwhile()
endfunction()

function(papo_finalize_tests)
    set(paths "${PROJECT_SOURCE_DIR}/tests/support" "${PROJECT_SOURCE_DIR}/tests/terminal"
              "${PROJECT_SOURCE_DIR}/tests/integration" "${PROJECT_SOURCE_DIR}")
    if(WIN32)
        list(JOIN paths "\\;" pythonpath)
    else()
        list(JOIN paths ":" pythonpath)
    endif()
    get_property(names GLOBAL PROPERTY PAPO_REGISTERED_TESTS)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/support/run_case.py")
    if(FFMPEG_CLI AND NOT WIN32)
        add_test(
            NAME test-fonts-prepare
            COMMAND
                "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/dist/resources.py" --manifest
                "${PROJECT_SOURCE_DIR}/tests/fixtures/resources.json" --output
                "${PROJECT_SOURCE_DIR}/build/resources/test-fonts")
        set_tests_properties(test-fonts-prepare PROPERTIES FIXTURES_SETUP test-fonts LABELS
                                                           "setup;media" TIMEOUT 120)
        add_test(
            NAME media-assets-prepare
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/support/media_assets.py"
                    --directory "${PROJECT_BINARY_DIR}/test-assets" --ffmpeg "${FFMPEG_CLI}")
        set_tests_properties(
            media-assets-prepare
            PROPERTIES FIXTURES_SETUP
                       media-assets
                       FIXTURES_REQUIRED
                       test-fonts
                       LABELS
                       "setup;media"
                       TIMEOUT
                       60)
    endif()
    foreach(name IN LISTS names)
        get_property(command GLOBAL PROPERTY "PAPO_COMMAND_${name}")
        set(script "")
        foreach(part IN LISTS command)
            if(part MATCHES "/tests/.*\\.py$")
                set(script "${part}")
            endif()
        endforeach()
        set(cases "")
        if(script)
            set_property(
                DIRECTORY
                APPEND
                PROPERTY CMAKE_CONFIGURE_DEPENDS "${script}")
            execute_process(
                COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/support/run_case.py"
                        --discover "${script}"
                RESULT_VARIABLE status
                OUTPUT_VARIABLE discovered
                ERROR_VARIABLE errors
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "Cannot discover ${script}: ${errors}")
            endif()
            if(discovered)
                string(REPLACE "\n" ";" cases "${discovered}")
            endif()
        endif()
        if(NOT cases)
            set(cases "__script__")
        endif()
        foreach(case IN LISTS cases)
            set(invocation ${command})
            set(id "${name}")
            if(NOT case STREQUAL "__script__")
                set(id "${name}/${case}")
                list(FIND invocation "${script}" script_index)
                list(REMOVE_AT invocation ${script_index})
                list(
                    INSERT
                    invocation
                    ${script_index}
                    "${PROJECT_SOURCE_DIR}/tests/support/run_case.py"
                    --script
                    "${script}"
                    --case
                    "${case}"
                    --)
            endif()
            add_test(NAME "${id}" COMMAND ${invocation})
            if(NOT case STREQUAL "__script__")
                set_tests_properties("${id}" PROPERTIES SKIP_RETURN_CODE 77)
            endif()
            set(labels "unit")
            if(script MATCHES "/terminal/" OR name MATCHES "terminal")
                set(labels "terminal")
            elseif(script MATCHES "/integration/" OR name MATCHES "integration")
                set(labels "integration")
            elseif(name STREQUAL "api-contract" OR name STREQUAL "emojis-catalogue")
                set(labels "contract")
            endif()
            if(name MATCHES "media|image|emoji|app-modes" OR case MATCHES "media_")
                list(APPEND labels "media")
            endif()
            if(name
               MATCHES
               "media-backends|media-modal|terminal-synchronization|login-terminal|message-send-product"
            )
                list(APPEND labels "lifecycle")
            endif()
            if(FFMPEG_CLI
               AND NOT WIN32
               AND "media" IN_LIST labels)
                set_tests_properties("${id}" PROPERTIES FIXTURES_REQUIRED media-assets)
            endif()
            if(case MATCHES "\\[(kitty|sixel|iterm2)")
                list(APPEND labels "${CMAKE_MATCH_1}")
            endif()
            if(WIN32)
                list(APPEND labels "windows")
            else()
                list(APPEND labels "linux")
            endif()
            set_tests_properties(
                "${id}"
                PROPERTIES
                    LABELS
                    "${labels}"
                    TIMEOUT
                    60
                    WORKING_DIRECTORY
                    "${PROJECT_BINARY_DIR}"
                    ENVIRONMENT
                    "PYTHONPATH=${pythonpath};PYTHONUTF8=1;PYTHONIOENCODING=utf-8;TZ=UTC;PAPO_TEST_REPORTS=${PROJECT_BINARY_DIR}/Testing/Temporary/cases;PAPO_TEST_ASSETS=${PROJECT_BINARY_DIR}/test-assets"
            )
            if(NOT WIN32 AND "media" IN_LIST labels)
                set_property(
                    TEST "${id}"
                    APPEND
                    PROPERTY ENVIRONMENT
                             "FONTCONFIG_FILE=${PROJECT_BINARY_DIR}/test-assets/fonts.conf")
            endif()
            foreach(key IN ITEMS TIMEOUT WORKING_DIRECTORY ENVIRONMENT)
                get_property(value GLOBAL PROPERTY "PAPO_${name}_${key}")
                if(value)
                    if(key STREQUAL "ENVIRONMENT")
                        set_property(
                            TEST "${id}"
                            APPEND
                            PROPERTY ENVIRONMENT "${value}")
                    elseif(key STREQUAL "TIMEOUT" AND NOT case STREQUAL "__script__")
                        # Individual cases no longer inherit a whole module's multi-minute timeout.
                        if(name MATCHES "synchronization")
                            set_tests_properties("${id}" PROPERTIES TIMEOUT 180)
                        endif()
                    else()
                        set_tests_properties("${id}" PROPERTIES "${key}" "${value}")
                    endif()
                endif()
            endforeach()
        endforeach()
    endforeach()
    get_property(
        targets
        DIRECTORY
        PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(sources ${target} SOURCES)
        if("${sources}" MATCHES "tests/")
            target_compile_options(${target} PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic -Werror)
        endif()
    endforeach()
endfunction()
