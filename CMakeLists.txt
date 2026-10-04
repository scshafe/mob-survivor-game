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

# The game core: deterministic simulation with no rendering or platform code,
# so it is testable headless. A renderer and input layer come later.
add_library(mob_survivor_core src/world.cpp)
target_include_directories(mob_survivor_core PUBLIC include)
mob_survivor_configure_target(mob_survivor_core)

add_executable(mob-survivor-game src/main.cpp)
target_link_libraries(mob-survivor-game PRIVATE mob_survivor_core)
mob_survivor_configure_target(mob-survivor-game)

if(BUILD_TESTING)
  add_executable(world_test tests/world_test.cpp)
  target_link_libraries(world_test PRIVATE mob_survivor_core)
  mob_survivor_configure_target(world_test)
  add_test(NAME world_test COMMAND world_test)
endif()
