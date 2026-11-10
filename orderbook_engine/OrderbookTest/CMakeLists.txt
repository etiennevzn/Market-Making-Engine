include(FetchContent)

# GoogleTest is downloaded automatically at configure time (no submodule needed)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(googletest)

add_executable(orderbook_tests mytests.cpp engine_tests.cpp)
target_link_libraries(orderbook_tests PRIVATE orderbook gtest)
target_compile_definitions(orderbook_tests PRIVATE TEST_FILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/TestFiles")

include(GoogleTest)
gtest_discover_tests(orderbook_tests PROPERTIES TIMEOUT 30)
