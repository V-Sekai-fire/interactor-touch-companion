# SPDX-License-Identifier: Apache-2.0 OR MIT
cmake_minimum_required(VERSION 3.24)
project(interactor_touch_companion C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The native companion uses Windows Graphics Capture and SendInput; browser/ needs no build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(COMPANION_GPU_TESTS "Register tests that need a desktop session and a GPU" OFF)

include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
FetchContent_MakeAvailable(nlohmann_json)

set(PANELSPUN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PANELSPUN_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/panelspun)

add_library(companion_input STATIC src/keymap.cpp src/guard.cpp)
target_include_directories(companion_input PUBLIC src)
target_link_libraries(companion_input PUBLIC nlohmann_json::nlohmann_json user32)

add_library(companion_gpu STATIC src/frame_share.cpp src/capture.cpp)
target_include_directories(companion_gpu PUBLIC src)
target_link_libraries(companion_gpu PUBLIC panelspun d3d11 dxgi dwmapi windowsapp)

foreach(t companion_input companion_gpu)
  target_compile_options(${t} PRIVATE /W4 /permissive- /EHsc)
endforeach()

add_executable(touch-companion src/main.cpp)
target_link_libraries(touch-companion PRIVATE companion_input companion_gpu)
target_compile_options(touch-companion PRIVATE /W4 /permissive- /EHsc)

add_executable(companion-test-game tests/test_game.cpp)
add_executable(companion-tests tests/tests.cpp)
target_link_libraries(companion-tests PRIVATE companion_input companion_gpu)

enable_testing()
set(COMPANION_CASES
  keymap.default-map-loads
  keymap.unknown-code-refused
  layout.matches-browser-keypad
  guard.refuses-foreign-foreground
  guard.refuses-live-foreground-for-windowless-game
  sender.button-sends-exactly-its-key
  sender.refused-sends-nothing
  sender.click-posts-to-the-game)
if(COMPANION_GPU_TESTS)
  list(APPEND COMPANION_CASES gpu.capture-yields-frames-from-a-test-window gpu.companion-presents-the-captured-game)
endif()
foreach(case IN LISTS COMPANION_CASES)
  add_test(NAME ${case} COMMAND companion-tests ${case} ${CMAKE_CURRENT_SOURCE_DIR}
           $<TARGET_FILE:companion-test-game> $<TARGET_FILE:touch-companion>)
endforeach()
