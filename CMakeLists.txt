cmake_minimum_required(VERSION 3.10)
project(multithreaded_web_server C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(web_server
    src/main.c
    src/server.c
    src/thread_pool.c
)

target_include_directories(web_server PRIVATE include)
target_link_libraries(web_server PRIVATE Threads::Threads)

if(NOT WIN32)
    target_compile_options(web_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS web_server RUNTIME DESTINATION bin)
