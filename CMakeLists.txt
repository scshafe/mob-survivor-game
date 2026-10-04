cmake_minimum_required(VERSION 3.28)

project(mob-survivor-game LANGUAGES CXX)

include(CTest)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for tooling" FORCE)

# Every first-party target gets the same language level and warnings, so the
# requirements cannot drift between directories.
function(mob_survivor_configure_target target_name)
  get_target_property(target_type ${target_name} TYPE)
  if(target_type STREQUAL "EXECUTABLE")
    target_compile_features(${target_name} PRIVATE cxx_std_20)
  else()
    target_compile_features(${target_name} PUBLIC cxx_std_20)
  endif()
  set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF CXX_SCAN_FOR_MODULES OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

# The game core: the deterministic simulation (levels, mobs, gates, matches),
# with no rendering, networking or platform code, so it is testable headless.
add_library(mob_survivor_core
  src/core/bot.cpp
  src/core/level.cpp
  src/core/match.cpp
  src/core/upgrades.cpp
  src/core/world.cpp
)
target_include_directories(mob_survivor_core PUBLIC include)
mob_survivor_configure_target(mob_survivor_core)

# Everything the server needs that does not touch a socket: HTTP and
# WebSocket framing, JSON, the wire protocol and the rooms. Tested headless.
add_library(mob_survivor_net
  src/net/base64.cpp
  src/net/http.cpp
  src/net/json.cpp
  src/net/leaderboard.cpp
  src/net/lobby.cpp
  src/net/protocol.cpp
  src/net/sha1.cpp
  src/net/websocket.cpp
)
target_include_directories(mob_survivor_net PUBLIC src)
target_link_libraries(mob_survivor_net PUBLIC mob_survivor_core)
mob_survivor_configure_target(mob_survivor_net)

# The game server: one process, many rooms, static web client and WebSocket.
add_executable(mob-survivor-server src/server/main.cpp src/server/server.cpp)
target_link_libraries(mob-survivor-server PRIVATE mob_survivor_net)
mob_survivor_configure_target(mob-survivor-server)

if(BUILD_TESTING)
  foreach(test_name world_test match_test net_test lobby_test)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE mob_survivor_net)
    mob_survivor_configure_target(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  # The browser's snapshot decoder against a snapshot the server encoded,
  # and a syntax check of every client module. Needs Node.js.
  add_executable(snapshot_fixture tests/snapshot_fixture.cpp)
  target_link_libraries(snapshot_fixture PRIVATE mob_survivor_net)
  mob_survivor_configure_target(snapshot_fixture)
  find_program(NODE_EXECUTABLE node)
  if(NODE_EXECUTABLE)
    set(fixture_dir ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
    file(MAKE_DIRECTORY ${fixture_dir})
    add_test(NAME snapshot_fixture COMMAND snapshot_fixture ${fixture_dir})
    set_tests_properties(snapshot_fixture PROPERTIES FIXTURES_SETUP snapshot)
    add_test(NAME web_snapshot_test
      COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/web_snapshot_test.mjs ${fixture_dir})
    set_tests_properties(web_snapshot_test PROPERTIES FIXTURES_REQUIRED snapshot)
    file(GLOB web_modules ${CMAKE_CURRENT_SOURCE_DIR}/web/js/*.js)
    foreach(module ${web_modules})
      get_filename_component(module_name ${module} NAME_WE)
      add_test(NAME web_syntax_${module_name} COMMAND ${NODE_EXECUTABLE} --check ${module})
    endforeach()
  else()
    message(STATUS "node not found: skipping the web client tests")
  endif()

  # End to end: the real server on a free port, driven by WebSocket clients.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME server_e2e
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/server_e2e.py
              $<TARGET_FILE:mob-survivor-server> ${CMAKE_CURRENT_SOURCE_DIR}/web)
  endif()
endif()
