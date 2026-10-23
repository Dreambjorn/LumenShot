cmake_minimum_required(VERSION 3.25)
project(LumenShot VERSION 0.1.2 LANGUAGES C CXX RC)
include(ExternalProject)

include(cmake/codecs.cmake)
include(cmake/notices.cmake)

# WinLibs' default manifest is not one ld can replace, so an empty object shadows it.
set(manifest_shadow "${CMAKE_BINARY_DIR}/manifest-shadow")
file(WRITE "${manifest_shadow}/empty.c" "")
add_custom_command(OUTPUT "${manifest_shadow}/default-manifest.o"
    COMMAND "${CMAKE_C_COMPILER}" -c "${manifest_shadow}/empty.c" -o "${manifest_shadow}/default-manifest.o")
add_custom_target(manifest_shadow DEPENDS "${manifest_shadow}/default-manifest.o")

function(hdrshot_target target)
    add_dependencies(${target} libultrahdr manifest_shadow)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target} PRIVATE "${uhdr_include}" "${CMAKE_SOURCE_DIR}/src")
    target_compile_options(${target} PRIVATE -O2 -Wall -Wextra -Werror -flarge-source-files -fstack-protector-strong)
    target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++ "-B${manifest_shadow}/"
        -Wl,--dynamicbase,--nxcompat,--high-entropy-va)
    target_link_libraries(${target} PRIVATE "${uhdr_library}" "${jpeg_library}" d3d11 dxgi dxguid runtimeobject
        windowscodecs ole32 oleaut32 uuid user32 gdi32 shell32 comctl32 comdlg32 advapi32 dwmapi uxtheme)
endfunction()

add_executable(LumenShot WIN32 src/main.cpp src/app.rc "${HDRSHOT_NOTICES_RC}")
hdrshot_target(LumenShot)
target_link_options(LumenShot PRIVATE -s -municode)

enable_testing()
add_subdirectory(tests)
