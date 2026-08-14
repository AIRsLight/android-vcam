cmake_minimum_required(VERSION 3.20)
project(android_vcam_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(vcam_pattern STATIC
    hal/src/FrameRenderer.cpp
    hal/src/PatternGenerator.cpp
    hal/src/RouteResolver.cpp
    hal/src/ScopedCameraRouter.cpp
    hal/src/RgbFrame.cpp
)
target_include_directories(vcam_pattern PUBLIC hal/include)

add_executable(pattern_generator_test
    tests/pattern_generator_test.cpp
)
target_link_libraries(pattern_generator_test PRIVATE vcam_pattern)

add_executable(rgb_frame_test
    tests/rgb_frame_test.cpp
)
target_link_libraries(rgb_frame_test PRIVATE vcam_pattern)

add_executable(frame_renderer_test
    tests/frame_renderer_test.cpp
)
target_link_libraries(frame_renderer_test PRIVATE vcam_pattern)

add_executable(route_resolver_test
    tests/route_resolver_test.cpp
)
target_link_libraries(route_resolver_test PRIVATE vcam_pattern)

add_executable(scoped_camera_router_test
    tests/scoped_camera_router_test.cpp
)
target_link_libraries(scoped_camera_router_test PRIVATE vcam_pattern)

foreach(test_target IN ITEMS
        pattern_generator_test
        rgb_frame_test
        frame_renderer_test
        route_resolver_test
        scoped_camera_router_test)
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
endforeach()

enable_testing()
add_test(NAME pattern_generator_test COMMAND pattern_generator_test)
add_test(NAME rgb_frame_test COMMAND rgb_frame_test)
add_test(NAME frame_renderer_test COMMAND frame_renderer_test)
add_test(NAME route_resolver_test COMMAND route_resolver_test)
add_test(NAME scoped_camera_router_test COMMAND scoped_camera_router_test)
