# Pinned static codecs, downloaded once into the tools folder and built with the project compiler.
if(NOT HDRSHOT_TOOLS)
    get_filename_component(HDRSHOT_TOOLS "${CMAKE_SOURCE_DIR}/../tools" ABSOLUTE)
endif()
set(downloads "${HDRSHOT_TOOLS}/downloads")
set(codec_args
    -G Ninja
    -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_BUILD_TYPE=Release)

set(jpeg_include "${CMAKE_BINARY_DIR}/jpeg-include")
ExternalProject_Add(libjpeg
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz
    URL_HASH SHA256=6F30092CEF9FB839779646608F4EE14AE3CBAC989C47FA05E841B0841F09878E
    DOWNLOAD_DIR "${downloads}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CMAKE_ARGS ${codec_args} -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF
        -DWITH_SIMD=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target jpeg-static
    INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory "${jpeg_include}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different <SOURCE_DIR>/src/jpeglib.h <SOURCE_DIR>/src/jmorecfg.h
        <SOURCE_DIR>/src/jerror.h <BINARY_DIR>/jconfig.h "${jpeg_include}"
    BUILD_BYPRODUCTS <BINARY_DIR>/libjpeg.a)
ExternalProject_Get_Property(libjpeg BINARY_DIR)
set(jpeg_library "${BINARY_DIR}/libjpeg.a")

ExternalProject_Add(libultrahdr
    DEPENDS libjpeg
    URL https://codeload.github.com/google/libultrahdr/tar.gz/refs/tags/v2.0.2
    URL_HASH SHA256=AA8D193BB887C348C419780511DD03B374F4E07AF8812B6D3F80C8537CF1EF2C
    DOWNLOAD_NAME libultrahdr-v2.0.2.tar.gz
    DOWNLOAD_DIR "${downloads}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CMAKE_ARGS ${codec_args} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DBUILD_SHARED_LIBS=OFF -DUHDR_BUILD_EXAMPLES=OFF -DUHDR_BUILD_TESTS=OFF
        -DUHDR_BUILD_DEPS=OFF -DUHDR_BUILD_JAVA=OFF -DUHDR_ENABLE_HEIF=OFF -DUHDR_ENABLE_GLES=OFF
        -DUHDR_ENABLE_INTRINSICS=OFF -DUHDR_ENABLE_INSTALL=OFF -DUHDR_WRITE_XMP=ON -DUHDR_WRITE_ISO=ON
        -DJPEG_LIBRARY=${jpeg_library} -DJPEG_INCLUDE_DIR=${jpeg_include}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target uhdr
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS <BINARY_DIR>/libuhdr.a)
ExternalProject_Get_Property(libultrahdr SOURCE_DIR BINARY_DIR)
set(uhdr_include "${SOURCE_DIR}")
set(uhdr_library "${BINARY_DIR}/libuhdr.a")
