cmake_minimum_required(VERSION 3.20)
project(android_vcam_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(vcam_pattern STATIC
    hal/src/PatternGenerator.cpp
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

enable_testing()
add_test(NAME pattern_generator_test COMMAND pattern_generator_test)
add_test(NAME rgb_frame_test COMMAND rgb_frame_test)
