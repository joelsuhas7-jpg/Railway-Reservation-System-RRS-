cmake_minimum_required(VERSION 3.16)
project(RailwayReservationSystem VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Core library: every module lives here; main.cpp stays tiny.
file(GLOB RRS_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER RRS_CORE_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
add_library(rrs_core STATIC ${RRS_CORE_SOURCES})
target_include_directories(rrs_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
add_executable(railway_reservation src/main.cpp)
target_link_libraries(railway_reservation PRIVATE rrs_core)

enable_testing()
include(FetchContent)
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.4)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

file(GLOB RRS_TEST_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/tests/unit/*.cpp ${CMAKE_SOURCE_DIR}/tests/integration/*.cpp)
add_executable(rrs_tests ${RRS_TEST_SOURCES})
target_link_libraries(rrs_tests PRIVATE rrs_core Catch2::Catch2WithMain)
catch_discover_tests(rrs_tests)
