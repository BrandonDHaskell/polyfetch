cmake_minimum_required(VERSION 3.16)
project(polyfetch_cpp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_err)
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=native" has_march_native)

# libcurl is the one documented external dependency for the C++ client (the C++
# standard library has no HTTP). SHA-256 is vendored (sha256.cpp) rather than
# pulled from OpenSSL, to keep the external dependency surface to libcurl alone.
find_package(CURL REQUIRED)

# std::thread needs the threads library.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(polyfetch
  src/main.cpp
  src/fetch.cpp
  src/process.cpp
  src/report.cpp
  src/sha256.cpp
)

target_include_directories(polyfetch PRIVATE include)
target_link_libraries(polyfetch PRIVATE CURL::libcurl Threads::Threads)
target_compile_options(polyfetch PRIVATE -Wall -Wextra)

if(ipo_ok)
  set_property(TARGET polyfetch PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(has_march_native)
  target_compile_options(polyfetch PRIVATE $<$<CONFIG:Release>:-march=native>)
endif()