cmake_minimum_required(VERSION 3.16)
project(drmario64-vs VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Off for source and development builds: only published release binaries post anonymous faults.
option(DM_FAULT_REPORTS "Let the game post anonymous fault reports" OFF)
option(DM_BUILD_APP "Build the SDL2 frontend (the core and its tests need no SDL)" ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

# Deterministic engine core: no SDL, no clock, no I/O. Everything the original game
# decides happens here, so it can be golden-tested headless frame by frame.
add_library(dmcore STATIC src/core/rng.c src/core/setup.c)
target_include_directories(dmcore PUBLIC src/core ${CMAKE_BINARY_DIR}/generated)

enable_testing()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE dmcore)
add_test(NAME core COMMAND test_core ${CMAKE_SOURCE_DIR}/tests/fixtures)

if(DM_BUILD_APP)
  find_package(SDL2 REQUIRED)
  if(DM_FAULT_REPORTS)
    set(DM_FAULT_REPORTS_VALUE 1)
  else()
    set(DM_FAULT_REPORTS_VALUE 0)
  endif()
  add_executable(drmario64-vs src/app/main.c src/app/fault.c)
  target_compile_definitions(drmario64-vs PRIVATE DM_FAULT_REPORTS=${DM_FAULT_REPORTS_VALUE})
  target_link_libraries(drmario64-vs PRIVATE dmcore)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(drmario64-vs PRIVATE SDL2::SDL2main)
  endif()
  if(TARGET SDL2::SDL2)
    target_link_libraries(drmario64-vs PRIVATE SDL2::SDL2)
  else()
    target_include_directories(drmario64-vs PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(drmario64-vs PRIVATE ${SDL2_LIBRARIES})
  endif()
  add_test(NAME app_smoke COMMAND drmario64-vs --headless --frames 120)
  set_tests_properties(app_smoke PROPERTIES TIMEOUT 30 ENVIRONMENT "DM_NO_REPORT=1;SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()
