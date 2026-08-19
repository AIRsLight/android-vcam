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

if(UNIX)
    add_subdirectory(runtime)

    add_executable(camera_service_router_mode_test
        tests/camera_service_router_mode_test.cpp
        runtime/CameraServiceRouterMode.cpp)
    target_include_directories(camera_service_router_mode_test PRIVATE runtime/include)
    target_compile_options(camera_service_router_mode_test PRIVATE -UNDEBUG)
    add_test(NAME camera_service_router_mode_test
        COMMAND camera_service_router_mode_test)

    add_executable(cameraserver_bootstrap_mode_test
        tests/cameraserver_bootstrap_mode_test.cpp
        runtime/CameraServerBootstrapMode.cpp)
    target_include_directories(cameraserver_bootstrap_mode_test PRIVATE runtime/include)
    target_compile_options(cameraserver_bootstrap_mode_test PRIVATE -UNDEBUG)
    add_test(NAME cameraserver_bootstrap_mode_test
        COMMAND cameraserver_bootstrap_mode_test)

    add_executable(runtime_abi_guard_test tests/runtime_abi_guard_test.cpp)
    target_link_libraries(runtime_abi_guard_test PRIVATE vcam_runtime_abi_guard dl)
    target_link_options(runtime_abi_guard_test PRIVATE -Wl,--export-dynamic)
    target_compile_options(runtime_abi_guard_test PRIVATE -UNDEBUG)
    add_test(NAME runtime_abi_guard_test COMMAND runtime_abi_guard_test)

    add_executable(arm64_patch_planner_test tests/arm64_patch_planner_test.cpp)
    target_link_libraries(arm64_patch_planner_test PRIVATE vcam_runtime_abi_guard)
    target_compile_options(arm64_patch_planner_test PRIVATE -UNDEBUG)
    add_test(NAME arm64_patch_planner_test COMMAND arm64_patch_planner_test)

    add_executable(arm64_patch_install_transaction_test
        tests/arm64_patch_install_transaction_test.cpp)
    target_link_libraries(arm64_patch_install_transaction_test PRIVATE vcam_runtime_abi_guard)
    target_compile_options(arm64_patch_install_transaction_test PRIVATE -UNDEBUG)
    add_test(NAME arm64_patch_install_transaction_test
        COMMAND arm64_patch_install_transaction_test)

    add_executable(runtime_hook_strategy_test tests/runtime_hook_strategy_test.cpp)
    target_link_libraries(runtime_hook_strategy_test PRIVATE vcam_runtime_abi_guard)
    target_compile_options(runtime_hook_strategy_test PRIVATE -UNDEBUG)
    add_test(NAME runtime_hook_strategy_test COMMAND runtime_hook_strategy_test)

    add_executable(runtime_activation_preflight_test
        tests/runtime_activation_preflight_test.cpp)
    target_link_libraries(runtime_activation_preflight_test PRIVATE vcam_runtime_abi_guard)
    target_compile_options(runtime_activation_preflight_test PRIVATE -UNDEBUG)
    add_test(NAME runtime_activation_preflight_test COMMAND runtime_activation_preflight_test)

    add_executable(static_arm64_trampoline_test
        tests/static_arm64_trampoline_test.cpp)
    target_link_libraries(static_arm64_trampoline_test PRIVATE vcam_runtime_abi_guard)
    target_compile_options(static_arm64_trampoline_test PRIVATE -UNDEBUG)
    add_test(NAME static_arm64_trampoline_test COMMAND static_arm64_trampoline_test)

    add_executable(thread_quiescence_coordinator_test
        tests/thread_quiescence_coordinator_test.cpp)
    target_link_libraries(thread_quiescence_coordinator_test PRIVATE vcam_runtime_abi_guard)
    target_compile_options(thread_quiescence_coordinator_test PRIVATE -UNDEBUG)
    add_test(NAME thread_quiescence_coordinator_test
        COMMAND thread_quiescence_coordinator_test)

    add_executable(android_signal_quiescence_backend_test
        tests/android_signal_quiescence_backend_test.cpp)
    target_link_libraries(android_signal_quiescence_backend_test
        PRIVATE vcam_runtime_abi_guard)
    target_compile_options(android_signal_quiescence_backend_test PRIVATE -UNDEBUG)
    add_test(NAME android_signal_quiescence_backend_test
        COMMAND android_signal_quiescence_backend_test)

    add_executable(binder_pass_through_bridge_test
        tests/binder_pass_through_bridge_test.cpp)
    target_link_libraries(binder_pass_through_bridge_test PRIVATE vcam_runtime_abi_guard)
    target_compile_options(binder_pass_through_bridge_test PRIVATE -UNDEBUG)
    add_test(NAME binder_pass_through_bridge_test COMMAND binder_pass_through_bridge_test)

    add_executable(binder_transaction_classifier_test
        tests/binder_transaction_classifier_test.cpp)
    target_link_libraries(binder_transaction_classifier_test PRIVATE vcam_runtime_abi_guard)
    target_compile_options(binder_transaction_classifier_test PRIVATE -UNDEBUG)
    add_test(NAME binder_transaction_classifier_test COMMAND binder_transaction_classifier_test)
endif()
