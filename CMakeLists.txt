set(CMAKE_SKIP_BUILD_RPATH TRUE)
cmake_minimum_required(VERSION 3.22)
project(DepthOfField VERSION 1.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(PRELOADER_LIB)
  add_library(preloader SHARED IMPORTED)
  set_target_properties(preloader PROPERTIES IMPORTED_LOCATION "${PRELOADER_LIB}")
else()
  message(STATUS "PRELOADER_LIB not set: building a link-only stub of libpreloader.so (do not ship it)")
  add_library(preloader SHARED stub/preloader_stub.cpp)
  target_include_directories(preloader PRIVATE vendor)
  set_target_properties(preloader PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stub")
endif()

add_library(DepthOfField SHARED
  src/main.cpp
  src/Mod.cpp
  src/Config.cpp
  src/Menu.cpp
  src/GlHooks.cpp)

set_target_properties(DepthOfField PROPERTIES PREFIX "" OUTPUT_NAME "DepthOfField")
target_include_directories(DepthOfField PRIVATE include vendor)
target_compile_options(DepthOfField PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -Wall -Wextra -Wpedantic)
target_link_options(DepthOfField PRIVATE
  -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-z,relro -Wl,-z,now -Wl,--build-id=sha1 -Wl,--no-undefined)

find_library(dl-lib dl REQUIRED)
target_link_libraries(DepthOfField PRIVATE preloader ${dl-lib})
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(TARGET DepthOfField POST_BUILD
  COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:DepthOfField>"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_levipack.py"
    --library "$<TARGET_FILE:DepthOfField>"
    --output "$<TARGET_FILE_DIR:DepthOfField>/DepthOfField.levipack"
  VERBATIM)
