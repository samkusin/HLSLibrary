cmake_minimum_required(VERSION 3.15.7)

project( cinekav )

#
# Build Project
#
set( PROJECT_INCLUDES
     ${CMAKE_CURRENT_SOURCE_DIR}/avdefs.hpp
     ${CMAKE_CURRENT_SOURCE_DIR}/avlib.hpp
     ${CMAKE_CURRENT_SOURCE_DIR}/elemstream.hpp
     ${CMAKE_CURRENT_SOURCE_DIR}/hlstream.hpp
     ${CMAKE_CURRENT_SOURCE_DIR}/hlsplaylist.hpp
     ${CMAKE_CURRENT_SOURCE_DIR}/mpegts.hpp )
set( PROJECT_SOURCES
     ${CMAKE_CURRENT_SOURCE_DIR}/avlib.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/elemstream.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/hlstream.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/hlsplaylist.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/mpegts.cpp )


set( PROJECT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" )

add_library( ckavlib ${PROJECT_SOURCES} ${PROJECT_INCLUDES} )

target_include_directories( ckavlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} )

target_compile_features( ckavlib PRIVATE cxx_std_17 )
if(CMAKE_CXX_COMPILER_ID MATCHES Clang OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ckavlib PRIVATE -Wall -Wextra -pedantic -Wno-missing-braces -Wno-unused-parameter)
endif()

if (HLSLIBRARY_BUILD_TESTS)
add_subdirectory(tests)
endif()

if (HLSLIBRARY_BUILD_SIMPLE)
# This is used by the sample.
# Linux: Install one of the libcurl packages
# Windows: Install libcurl locally and provide either a CMAKE_PREFIX_PATH or
#          CURL_DIR CMake cache variable on configuration.
find_package(CURL)


endif()
