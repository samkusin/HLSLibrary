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
     ${CMAKE_CURRENT_SOURCE_DIR}/mpegts.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp )


set( PROJECT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" )

add_executable( ckavlib ${PROJECT_SOURCES} ${PROJECT_INCLUDES} )

target_compile_features( ckavlib PRIVATE cxx_std_17 )
if(CMAKE_CXX_COMPILER_ID MATCHES Clang OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ckavlib PRIVATE -Wall -Wextra -pedantic -Wno-missing-braces)
endif()
