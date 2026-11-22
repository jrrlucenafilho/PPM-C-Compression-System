cmake_minimum_required(VERSION 3.16)
project(ppm_c LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(ppm src/main.cpp)
target_compile_options(ppm PRIVATE -O3 -Wall -Wextra)
target_link_libraries(ppm PRIVATE Threads::Threads)
