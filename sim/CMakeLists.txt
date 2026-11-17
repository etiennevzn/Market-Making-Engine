add_library(sim STATIC NoiseTraders.cpp StatsCollector.cpp Portfolio.cpp NaiveMarketMaker.cpp)
target_include_directories(sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sim PUBLIC orderbook)

add_executable(sim_run main.cpp)
target_link_libraries(sim_run PRIVATE sim)

add_executable(mm_run mm_run.cpp)
target_link_libraries(mm_run PRIVATE sim)

if(BUILD_TESTING)
    if(NOT TARGET gtest)
        include(FetchContent)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(googletest 
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(sim_tests tests/sim_tests.cpp tests/portfolio_tests.cpp)
    target_link_libraries(sim_tests PRIVATE sim gtest gtest_main)

    include(GoogleTest)
    gtest_discover_tests(sim_tests PROPERTIES TIMEOUT 60)
endif()
