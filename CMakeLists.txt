cmake_minimum_required(VERSION 3.10)
project(myglib C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Glob all MinHook source files
file(GLOB MINHOOK_SRC 
minhook/src/*.c
minhook/src/hde/*c
)

file(GLOB MY_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

# Build MinHook static library
add_library(minhook STATIC ${MINHOOK_SRC})
target_include_directories(minhook PUBLIC minhook/include)


# Define the test executable
add_executable(cookie_test cookie_test.c fopencookie.c)

# Include dirs
target_include_directories(cookie_test PRIVATE minhook/include)

# Link Windows libraries and MinHook
target_link_libraries(cookie_test PRIVATE kernel32 user32 minhook )

# MinGW specific options
if(MINGW)
    target_link_options(cookie_test PRIVATE -mconsole)
endif()
