cmake_minimum_required(VERSION 3.10)
project(MGLIB C)

set(MGLIB_MAJOR_VERSION 0)
set(MGLIB_MINOR_VERSION 0)
set(MGLIB_PATCH_VERSION 0)
set(MGLIB_VERSION ${MGLIB_MAJOR_VERSION}.${MGLIB_MINOR_VERSION}.${MGLIB_PATCH_VERSION})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

add_subdirectory(minhook)

set(MY_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/test/cookie_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fopencookie.c
)

add_executable(cookie_test ${MY_SRC})

target_include_directories(cookie_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)

target_link_libraries(cookie_test PRIVATE kernel32 user32 minhook)

if(MINGW)
    target_link_options(cookie_test PRIVATE -mconsole)
endif()
