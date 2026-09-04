cmake_minimum_required(VERSION 3.20)
project(ReservationFabric VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(RESERVATION_FABRIC_BUILD_TESTS "Build tests" ON)
option(RESERVATION_FABRIC_BUILD_EXAMPLES "Build examples" ON)
option(RESERVATION_FABRIC_BUILD_TOOLS "Build CLI tooling" ON)
option(RESERVATION_FABRIC_BUILD_BENCHMARKS "Build benchmarks" ON)
option(RESERVATION_FABRIC_ENABLE_ASAN "Build with AddressSanitizer" OFF)

# ---- Core library ----
add_library(reservation_fabric STATIC
  src/runtime_core.cpp
  src/runtime_commit.cpp
  src/runtime_lifecycle.cpp
  src/runtime_query.cpp
  src/runtime_persist.cpp
)
add_library(ReservationFabric::ReservationFabric ALIAS reservation_fabric)

target_compile_features(reservation_fabric PUBLIC cxx_std_20)
target_include_directories(reservation_fabric
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(reservation_fabric PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME ReservationFabric)

if(MSVC)
  target_compile_options(reservation_fabric PRIVATE /W4 /WX)
else()
  target_compile_options(reservation_fabric PRIVATE -Wall -Wextra -Werror)
endif()

if(RESERVATION_FABRIC_ENABLE_ASAN)
  if(MSVC)
    target_compile_options(reservation_fabric PRIVATE /fsanitize=address /O1 /Zi)
    target_link_options(reservation_fabric PRIVATE /fsanitize=address)
  else()
    target_compile_options(reservation_fabric PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(reservation_fabric PRIVATE -fsanitize=address)
  endif()
endif()

# ---- Install / package ----
install(TARGETS reservation_fabric
  EXPORT ReservationFabricTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/reservation_fabric DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ReservationFabricTargets
  FILE ReservationFabricTargets.cmake
  NAMESPACE ReservationFabric::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReservationFabric
)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ReservationFabricConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ReservationFabricConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReservationFabric
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ReservationFabricConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ReservationFabricConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ReservationFabricConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReservationFabric
)

if(RESERVATION_FABRIC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(RESERVATION_FABRIC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(RESERVATION_FABRIC_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(RESERVATION_FABRIC_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(WIN32 AND NOT RESERVATION_FABRIC_ENABLE_ASAN)
  add_subdirectory(distributed)
endif()

# CUDA proof is built when a toolkit is present (it requires a real sm_120 device).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cuda/CMakeLists.txt AND NOT RESERVATION_FABRIC_ENABLE_ASAN)
  add_subdirectory(cuda)
endif()