cmake_minimum_required(VERSION 3.14)
project(MarketMaking LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MinGW: avoid picking up an older libstdc++/libgcc DLL from another folder of the PATH
if(MINGW)
    add_link_options(-static)
endif()

# Memory-error detection:  cmake -S . -B build -DENABLE_SANITIZERS=ON
option(ENABLE_SANITIZERS "Build with AddressSanitizer (+ UBSan on GCC/Clang)" OFF)
if(ENABLE_SANITIZERS)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

include(CTest) # enable_testing() at the root so that ctest finds the tests of every subdirectory

add_subdirectory(orderbook_engine) # matching engine + its tests
add_subdirectory(sim)              # simulator, synthetic order flow, statistics
