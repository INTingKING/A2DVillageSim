cmake_minimum_required(VERSION 3.20)
project(A2DVillageSim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SDL3: use an installed copy (macOS: `brew install sdl3`), else fetch and build it.
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not found, fetching release-3.4.18")
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)  # not needed, avoids an extra Linux dev package
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

add_library(villagesim_core STATIC src/world.cpp)
target_include_directories(villagesim_core PUBLIC src)

add_executable(villagesim src/main.cpp src/render.cpp)
target_link_libraries(villagesim PRIVATE villagesim_core SDL3::SDL3)
# Windows with a shared, pre-installed SDL3: copy SDL3.dll next to the exe.
if(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.21)
  add_custom_command(TARGET villagesim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:villagesim> $<TARGET_FILE_DIR:villagesim>
    COMMAND_EXPAND_LISTS)
endif()

# Headless simulation test (no window, no SDL).
add_executable(villagesim_test src/selftest.cpp)
target_link_libraries(villagesim_test PRIVATE villagesim_core)
enable_testing()
add_test(NAME sim_selftest COMMAND villagesim_test)

# Art review sheet: every building in every age (no SDL).
add_executable(villagesim_gallery tools/gallery.cpp src/render.cpp)
target_link_libraries(villagesim_gallery PRIVATE villagesim_core)
