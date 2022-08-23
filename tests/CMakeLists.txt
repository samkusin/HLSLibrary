project(ckavlib_tests)

include(CTest)

add_executable(hlsplaylist_test
    "${CMAKE_CURRENT_SOURCE_DIR}/hlsplaylist_test.cpp")

target_link_libraries(hlsplaylist_test ckavlib)

add_test(NAME HLSPlaylistTest COMMAND hlsplaylist_test)
