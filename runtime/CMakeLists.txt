cmake_minimum_required(VERSION 3.20)
project(vcam_runtime_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(vcam_runtime_abi_guard STATIC
    Arm64PatchPlanner.cpp
    BinderPassThroughBridge.cpp
    RuntimeAbiGuard.cpp
    RuntimeHookStrategy.cpp
)
target_include_directories(vcam_runtime_abi_guard PUBLIC include)
target_link_libraries(vcam_runtime_abi_guard PUBLIC dl)

add_library(vcam_cameraserver_agent SHARED cameraserver_agent.cpp)
target_link_libraries(vcam_cameraserver_agent PRIVATE vcam_runtime_abi_guard dl)
target_link_options(vcam_cameraserver_agent PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
)
target_compile_options(vcam_cameraserver_agent PRIVATE
    -fvisibility=hidden
    -Wall
    -Wextra
    -Werror
)

if(ANDROID AND ANDROID_ABI STREQUAL "arm64-v8a")
    target_compile_options(vcam_runtime_abi_guard PRIVATE -mbranch-protection=standard)
    target_compile_options(vcam_cameraserver_agent PRIVATE -mbranch-protection=standard)
endif()
