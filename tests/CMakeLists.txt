set(evidence "${CMAKE_BINARY_DIR}/evidence")
file(MAKE_DIRECTORY "${evidence}")

# Fixtures share hotkeys, windows and the clipboard owner, so they never run in parallel.
function(hdrshot_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE lumenshot_core)
    hdrshot_target(${name})
    if(NOT name STREQUAL "color_tests")
        target_link_options(${name} PRIVATE -municode)
    endif()
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:${name}>
        -DEVIDENCE_ROOT=${evidence} -DNAME=${name} "-DARGUMENTS=${ARGN}" -P ${CMAKE_CURRENT_SOURCE_DIR}/run.cmake)
    set_tests_properties(${name} PROPERTIES RUN_SERIAL TRUE TIMEOUT 75)
endfunction()

hdrshot_test(color_tests image/color_tests.cpp)
hdrshot_test(sharpen_tests image/sharpen_tests.cpp "${evidence}/sharpen_tests")
hdrshot_test(scaling_tests image/scaling_tests.cpp "${evidence}/scaling_tests")
hdrshot_test(hdr_output_tests image/hdr_output_tests.cpp "${evidence}/hdr_output_tests")
hdrshot_test(multimonitor_tests capture/multimonitor_tests.cpp "${evidence}/multimonitor_tests")
set_tests_properties(multimonitor_tests PROPERTIES TIMEOUT 30)
hdrshot_test(startup_tests platform/startup_tests.cpp "${evidence}/startup_tests")
hdrshot_test(settings_autosave ui/settings_autosave.cpp "${evidence}/settings_autosave")
hdrshot_test(platform_tests capture/platform_tests.cpp "${evidence}/fixtures")
hdrshot_test(selector_tests ui/selector_tests.cpp "${evidence}/fixtures")
hdrshot_test(app_controls ui/app_controls.cpp)
target_sources(app_controls PRIVATE "${HDRSHOT_NOTICES_RC}")
hdrshot_test(app_workflow app/app_workflow.cpp "${evidence}/workflow")

get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
add_test(NAME audit COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:LumenShot>
    -DOBJDUMP=${compiler_bin}/objdump.exe -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/src -P ${CMAKE_CURRENT_SOURCE_DIR}/audit.cmake)
