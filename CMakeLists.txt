cmake_minimum_required(VERSION 3.20)
project(mini-kv CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB MINI_KV_HEADERS include/*.h)

add_executable(mini-kv
    src/main.cpp
    src/server.cpp
    src/conn.cpp
    src/io.cpp
    src/protocol.cpp
    src/registry.cpp
    src/stats.cpp
    src/store.cpp
    src/ctrlHandler.cpp
    ${MINI_KV_HEADERS}
)

target_include_directories(mini-kv PRIVATE include)
target_link_libraries(mini-kv PRIVATE ws2_32 mswsock)

target_compile_definitions(mini-kv PRIVATE
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(mini-kv PRIVATE /utf-8 /W4 /permissive-)
endif()
