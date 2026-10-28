cmake_minimum_required(VERSION 3.16)

project(RailwayReservationSystem VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(railway_reservation
    src/main.cpp
)

enable_testing()

add_executable(test_basic
    tests/test_basic.cpp
)

add_test(NAME BasicApplicationTest COMMAND test_basic)
