# Uses a pinned WinLibs GCC from a tools folder beside the project, downloading it when missing.
if(DEFINED ENV{HDRSHOT_TOOLS})
    set(HDRSHOT_TOOLS "$ENV{HDRSHOT_TOOLS}")
else()
    get_filename_component(HDRSHOT_TOOLS "${CMAKE_CURRENT_LIST_DIR}/../../tools" ABSOLUTE)
endif()
set(HDRSHOT_GCC_NAME "winlibs-gcc-15.3.0-ucrt-r1")
set(HDRSHOT_GCC_BIN "${HDRSHOT_TOOLS}/${HDRSHOT_GCC_NAME}/mingw64/bin")

if(NOT EXISTS "${HDRSHOT_GCC_BIN}/g++.exe")
    set(archive "${HDRSHOT_TOOLS}/downloads/winlibs-x86_64-posix-seh-gcc-15.3.0-mingw-w64ucrt-14.0.0-r1.zip")
    message(STATUS "Downloading GCC 15.3.0 into ${HDRSHOT_TOOLS}")
    file(DOWNLOAD
        "https://github.com/brechtsanders/winlibs_mingw/releases/download/15.3.0posix-14.0.0-ucrt-r1/winlibs-x86_64-posix-seh-gcc-15.3.0-mingw-w64ucrt-14.0.0-r1.zip"
        "${archive}"
        EXPECTED_HASH SHA256=AE31DE2D5C1DA830422B2B65384E3AD17F59C4DAA8F11D9234E67090C32734E2
        SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${HDRSHOT_TOOLS}/${HDRSHOT_GCC_NAME}")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER "${HDRSHOT_GCC_BIN}/gcc.exe")
set(CMAKE_CXX_COMPILER "${HDRSHOT_GCC_BIN}/g++.exe")
set(CMAKE_RC_COMPILER "${HDRSHOT_GCC_BIN}/windres.exe")
set(CMAKE_MAKE_PROGRAM "${HDRSHOT_GCC_BIN}/ninja.exe" CACHE FILEPATH "" FORCE)
