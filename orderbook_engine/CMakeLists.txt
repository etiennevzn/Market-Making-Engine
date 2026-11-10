cmake_minimum_required(VERSION 3.14)
project(OrderbookEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MINGW)
    add_link_options(-static)
endif()

# Memory-error detection (catches use-after-free etc.):  cmake -B build -DENABLE_SANITIZERS=ON
option(ENABLE_SANITIZERS "Build with AddressSanitizer (+ UBSan on GCC/Clang)" OFF)
if(ENABLE_SANITIZERS)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

if(NOT MSVC)
    add_compile_options(-Wall -Wextra)
endif()

find_package(Threads REQUIRED)

add_library(orderbook STATIC Orderbook.cpp)
target_include_directories(orderbook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(orderbook PUBLIC Threads::Threads)

add_executable(orderbook_demo main.cpp)
target_link_libraries(orderbook_demo PRIVATE orderbook)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(OrderbookTest)
endif()
