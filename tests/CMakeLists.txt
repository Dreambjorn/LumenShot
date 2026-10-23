set(evidence "${CMAKE_BINARY_DIR}/evidence")
file(MAKE_DIRECTORY "${evidence}")

# Fixtures share hotkeys, windows and the clipboard owner, so they never run in parallel.
function(hdrshot_test name)
    add_executable(${name} ${name}.cpp)
    hdrshot_target(${name})
    if(NOT name STREQUAL "color_tests")
        target_link_options(${name} PRIVATE -municode)
    endif()
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:${name}>
        -DEVIDENCE_ROOT=${evidence} -DNAME=${name} "-DARGUMENTS=${ARGN}" -P ${CMAKE_CURRENT_SOURCE_DIR}/run.cmake)
    set_tests_properties(${name} PROPERTIES RUN_SERIAL TRUE TIMEOUT 75)
endfunction()

hdrshot_test(color_tests)
hdrshot_test(sharpen_tests "${evidence}/sharpen_tests")
hdrshot_test(scaling_tests "${evidence}/scaling_tests")
hdrshot_test(hdr_output_tests "${evidence}/hdr_output_tests")
hdrshot_test(multimonitor_tests "${evidence}/multimonitor_tests")
set_tests_properties(multimonitor_tests PROPERTIES TIMEOUT 30)
hdrshot_test(startup_tests "${evidence}/startup_tests")
hdrshot_test(settings_autosave "${evidence}/settings_autosave")
hdrshot_test(platform_tests "${evidence}/fixtures")
hdrshot_test(selector_tests "${evidence}/fixtures")
hdrshot_test(app_controls)
target_sources(app_controls PRIVATE "${HDRSHOT_NOTICES_RC}")
hdrshot_test(app_workflow "${evidence}/workflow")

get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
add_test(NAME audit COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:LumenShot>
    -DOBJDUMP=${compiler_bin}/objdump.exe -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/src -P ${CMAKE_CURRENT_SOURCE_DIR}/audit.cmake)
