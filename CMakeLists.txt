cmake_minimum_required(VERSION 2.8)

project(craft)

FILE(GLOB SOURCE_FILES src/*.c)

FILE(GLOB LUA_FILES deps/lua/*.c)
LIST(REMOVE_ITEM LUA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/deps/lua/lua.c)
LIST(REMOVE_ITEM LUA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/deps/lua/luac.c)

add_executable(
    craft
    ${SOURCE_FILES}
    deps/glew/src/glew.c
    deps/lodepng/lodepng.c
    deps/noise/noise.c
    deps/sqlite/sqlite3.c
    deps/tinycthread/tinycthread.c
    ${LUA_FILES})

add_definitions(-std=c99 -O3)

set(BUILD_CURL_EXE OFF)
set(BUILD_CURL_TESTS OFF)
set(HTTP_ONLY ON)
set(CMAKE_USE_LIBSSH2 OFF)
set(CURL_STATICLIB ON)
add_subdirectory(deps/curl)
include_directories(deps/curl/include)

add_subdirectory(deps/glfw)
include_directories(deps/glew/include)
include_directories(deps/glfw/include)
include_directories(deps/lodepng)
include_directories(deps/lua)
include_directories(deps/noise)
include_directories(deps/sglib)
include_directories(deps/sqlite)
include_directories(deps/tinycthread)

set(CURL_INCLUDE_DIR deps/curl/include)
set(CURL_LIBRARIES ${CMAKE_CURRENT_SOURCE_DIR}/deps/curl/lib/libcurl${CMAKE_SHARED_LIBRARY_SUFFIX})

if(APPLE)
    target_link_libraries(craft glfw
        ${GLFW_LIBRARIES} ${CURL_LIBRARIES})
endif()

if(UNIX)
    target_link_libraries(craft dl glfw
        ${GLFW_LIBRARIES} ${CURL_LIBRARIES})
endif()

if(MINGW)
    target_link_libraries(craft ws2_32.lib glfw
        ${GLFW_LIBRARIES} ${CURL_LIBRARIES})
endif()
