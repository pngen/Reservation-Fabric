cmake_minimum_required(VERSION 3.20)
project(rf_consumer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ReservationFabric CONFIG REQUIRED)

add_executable(rf_consumer main.cpp)
target_link_libraries(rf_consumer PRIVATE ReservationFabric::ReservationFabric)
