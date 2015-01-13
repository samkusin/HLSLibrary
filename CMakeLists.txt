cmake_minimum_required(VERSION 2.8.10)

project( cinekav )

set( LOCAL_CPP_COMPILE_FLAGS "-Wall -Wno-missing-braces -std=c++11 -stdlib=libc++ -fno-exceptions -fno-rtti" )
set( LOCAL_CPP_LINK_FLAGS "-std=c++11 -stdlib=libc++" )

#
# Build Project
#
set( PROJECT_INCLUDES
     ${CMAKE_CURRENT_SOURCE_DIR}/avdefs.hpp
     ${CMAKE_CURRENT_SOURCE_DIR}/avlib.hpp
     ${CMAKE_CURRENT_SOURCE_DIR}/mpegts.hpp )
set( PROJECT_SOURCES
     ${CMAKE_CURRENT_SOURCE_DIR}/avlib.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/mpegts.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp )
set( PROJECT_LIBRARIES )

set( PROJECT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" )

add_executable( ckavlib ${PROJECT_SOURCES} ${PROJECT_INCLUDES} )
set_target_properties( ckavlib PROPERTIES COMPILE_FLAGS ${LOCAL_CPP_COMPILE_FLAGS} )
set_target_properties( ckavlib PROPERTIES LINK_FLAGS ${LOCAL_CPP_LINK_FLAGS} )
target_link_libraries( ckavlib ${PROJECT_LIBRARIES} )
