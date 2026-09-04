cmake_minimum_required(VERSION 3.22)
project(android_vcam_hal LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "This project must be configured with the Android NDK toolchain")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(VCAM_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(AOSP_REF "${VCAM_ROOT}/.reference")

set(PLATFORM_INCLUDE_DIRS
    "${AOSP_REF}/libhardware/include"
    "${AOSP_REF}/system_core/libcutils/include"
    "${AOSP_REF}/system_core/libsystem/include"
    "${AOSP_REF}/system_media/camera/include"
    "${AOSP_REF}/frameworks_native/libs/nativebase/include"
)

foreach(required_path IN LISTS PLATFORM_INCLUDE_DIRS)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Missing AOSP headers: ${required_path}. Run tools/fetch-platform-deps.ps1")
    endif()
endforeach()

# These two libraries are link-only ABI shims. They intentionally are not
# installed: Android supplies the real libhardware.so and libcamera_metadata.so.
add_library(hardware_abi SHARED stubs/hardware_stub.c)
set_target_properties(hardware_abi PROPERTIES OUTPUT_NAME hardware)
target_include_directories(hardware_abi PRIVATE ${PLATFORM_INCLUDE_DIRS})

add_library(camera_metadata_abi SHARED stubs/camera_metadata_stub.c)
set_target_properties(camera_metadata_abi PROPERTIES OUTPUT_NAME camera_metadata)
target_include_directories(camera_metadata_abi PRIVATE ${PLATFORM_INCLUDE_DIRS})

set(JPEG_DIR "${AOSP_REF}/libjpeg_turbo")
set(JPEG_SOURCES
    jaricom.c jcapimin.c jcapistd.c jcarith.c jccoefct.c jccolor.c
    jcdctmgr.c jchuff.c jcicc.c jcinit.c jcmainct.c jcmarker.c jcmaster.c
    jcomapi.c jcparam.c jcphuff.c jcprepct.c jcsample.c jctrans.c
    jdapimin.c jdapistd.c jdarith.c jdatadst.c jdatasrc.c jdcoefct.c
    jdcolor.c jddctmgr.c jdhuff.c jdicc.c jdinput.c jdmainct.c jdmarker.c
    jdmaster.c jdmerge.c jdphuff.c jdpostct.c jdsample.c jdtrans.c
    jerror.c jfdctflt.c jfdctfst.c jfdctint.c jidctflt.c jidctfst.c
    jidctint.c jidctred.c jmemmgr.c jmemnobs.c jpeg_nbits_table.c
    jquant1.c jquant2.c jutils.c jsimd_none.c
)
list(TRANSFORM JPEG_SOURCES PREPEND "${JPEG_DIR}/")

add_library(vcam_jpeg STATIC ${JPEG_SOURCES})
set_target_properties(vcam_jpeg PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vcam_jpeg PUBLIC "${JPEG_DIR}")
target_compile_definitions(vcam_jpeg PRIVATE NO_GETENV)
target_compile_options(vcam_jpeg PRIVATE -O3 -Wno-sign-compare -Wno-unused-parameter)

find_library(log_library log REQUIRED)

add_library(vcam_proxy SHARED
    proxy_bootstrap.cpp
    "${VCAM_ROOT}/hal/src/FrameRenderer.cpp"
    "${VCAM_ROOT}/hal/src/MetadataBuilder.cpp"
    "${VCAM_ROOT}/hal/src/PatternGenerator.cpp"
    "${VCAM_ROOT}/hal/src/RgbFrame.cpp"
    "${VCAM_ROOT}/hal/src/RouteResolver.cpp"
    "${VCAM_ROOT}/hal/src/VirtualCamera.cpp"
)
set_target_properties(vcam_proxy PROPERTIES OUTPUT_NAME vcam_proxy)
target_include_directories(vcam_proxy PRIVATE
    "${VCAM_ROOT}/hal/include"
    "${JPEG_DIR}"
    ${PLATFORM_INCLUDE_DIRS}
)
target_compile_options(vcam_proxy PRIVATE
    -Wall -Wextra -Wno-gnu-designator -fvisibility=hidden
)
target_link_options(vcam_proxy PRIVATE
    "-Wl,-z,defs"
    "-Wl,--exclude-libs,ALL"
)
target_link_libraries(vcam_proxy PRIVATE
    vcam_jpeg
    hardware_abi
    camera_metadata_abi
    dl
    ${log_library}
)

add_library(camera_vcam SHARED
    "${VCAM_ROOT}/hal/src/FrameRenderer.cpp"
    "${VCAM_ROOT}/hal/src/MetadataBuilder.cpp"
    "${VCAM_ROOT}/hal/src/PatternGenerator.cpp"
    "${VCAM_ROOT}/hal/src/RgbFrame.cpp"
    "${VCAM_ROOT}/hal/src/RouteResolver.cpp"
    "${VCAM_ROOT}/hal/src/VirtualCamera.cpp"
    "${VCAM_ROOT}/hal/src/VirtualCameraStandaloneModule.cpp"
)
set_target_properties(camera_vcam PROPERTIES
    OUTPUT_NAME camera.vcam
    PREFIX ""
)
target_include_directories(camera_vcam PRIVATE
    "${VCAM_ROOT}/hal/include"
    "${JPEG_DIR}"
    ${PLATFORM_INCLUDE_DIRS}
)
target_compile_options(camera_vcam PRIVATE
    -Wall -Wextra -Werror -Wno-gnu-designator -fvisibility=hidden
)
target_link_options(camera_vcam PRIVATE
    "-Wl,-z,defs"
    "-Wl,--exclude-libs,ALL"
)
target_link_libraries(camera_vcam PRIVATE
    vcam_jpeg
    hardware_abi
    camera_metadata_abi
    dl
    ${log_library}
)

install(TARGETS camera_vcam LIBRARY DESTINATION .)

add_executable(vcam_smoke_loader smoke_loader.c)
target_include_directories(vcam_smoke_loader PRIVATE ${PLATFORM_INCLUDE_DIRS})
target_link_libraries(vcam_smoke_loader PRIVATE camera_metadata_abi dl)

add_executable(vcam_frame_publisher frame_publisher.c)
set_target_properties(vcam_frame_publisher PROPERTIES OUTPUT_NAME vcam-publisher)

add_executable(vcam_control_daemon control_daemon.c)
set_target_properties(vcam_control_daemon PROPERTIES OUTPUT_NAME vcamd)
target_compile_options(vcam_control_daemon PRIVATE -Wall -Wextra -Werror)

set(FFMPEG_ANDROID_ROOT "${AOSP_REF}/ffmpeg-android/${ANDROID_ABI}" CACHE PATH
    "Pinned static FFmpeg SDK built for the selected Android ABI")
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ANDROID_ROOT}/include")
set(FFMPEG_LIBRARY_DIR "${FFMPEG_ANDROID_ROOT}/lib")
set(MBEDTLS_ANDROID_ROOT "${AOSP_REF}/mbedtls-android/${ANDROID_ABI}" CACHE PATH
    "Pinned static Mbed TLS SDK built for the selected Android ABI")
if(EXISTS "${FFMPEG_INCLUDE_DIR}/libavformat/avformat.h" AND
   EXISTS "${FFMPEG_LIBRARY_DIR}/libavformat.a")
    foreach(ffmpeg_library IN ITEMS avutil avcodec avformat swscale swresample)
        add_library("bundled_${ffmpeg_library}" STATIC IMPORTED)
        set_target_properties("bundled_${ffmpeg_library}" PROPERTIES
            IMPORTED_LOCATION "${FFMPEG_LIBRARY_DIR}/lib${ffmpeg_library}.a")
    endforeach()
    foreach(mbedtls_library IN ITEMS mbedtls mbedx509 mbedcrypto)
        set(mbedtls_path "${MBEDTLS_ANDROID_ROOT}/lib/lib${mbedtls_library}.a")
        if(NOT EXISTS "${mbedtls_path}")
            message(FATAL_ERROR
                "Pinned static Mbed TLS SDK missing: ${mbedtls_path}. Run tools/build-mbedtls-android.sh")
        endif()
        add_library("bundled_${mbedtls_library}" STATIC IMPORTED)
        set_target_properties("bundled_${mbedtls_library}" PROPERTIES
            IMPORTED_LOCATION "${mbedtls_path}")
    endforeach()
    add_executable(vcam_streamer stream_provider.c)
    set_target_properties(vcam_streamer PROPERTIES OUTPUT_NAME vcam-streamer)
    target_include_directories(vcam_streamer PRIVATE "${FFMPEG_INCLUDE_DIR}")
    target_compile_options(vcam_streamer PRIVATE -Wall -Wextra)
    target_link_libraries(vcam_streamer PRIVATE
        "-Wl,--start-group"
        bundled_avformat bundled_avcodec bundled_swscale bundled_swresample
        bundled_avutil bundled_mbedtls bundled_mbedx509 bundled_mbedcrypto
        "-Wl,--end-group"
        z m dl)
else()
    message(WARNING
        "Pinned static Android FFmpeg SDK missing; run tools/build-ffmpeg-android.sh")
endif()

add_executable(vcam_gralloc_probe gralloc_probe.c)
set_target_properties(vcam_gralloc_probe PROPERTIES OUTPUT_NAME vcam-gralloc-probe)
target_include_directories(vcam_gralloc_probe PRIVATE ${PLATFORM_INCLUDE_DIRS})
target_link_libraries(vcam_gralloc_probe PRIVATE hardware_abi)
