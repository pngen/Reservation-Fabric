if(MSVC)
  set(RF_WARN /W4 /WX)
else()
  set(RF_WARN -Wall -Wextra -Werror)
endif()

# Transport library: framed protocol + TCP socket helpers. Transport is a
# reference deployment mechanism layered on the core; the core is transport-free.
add_library(reservation_fabric_transport STATIC ${CMAKE_CURRENT_SOURCE_DIR}/../src/protocol.cpp)
target_include_directories(reservation_fabric_transport
  PUBLIC ${CMAKE_SOURCE_DIR}/include
  PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(reservation_fabric_transport PUBLIC ReservationFabric::ReservationFabric)
target_compile_features(reservation_fabric_transport PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(reservation_fabric_transport PRIVATE /W4 /WX)
  target_link_libraries(reservation_fabric_transport PUBLIC ws2_32)
else()
  target_compile_options(reservation_fabric_transport PRIVATE -Wall -Wextra -Werror)
endif()

function(rf_exe target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE reservation_fabric_transport)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
    target_link_libraries(${target} PRIVATE ws2_32)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  endif()
endfunction()

rf_exe(rf_coordinator coordinator_main.cpp)
rf_exe(rf_worker worker_main.cpp)
rf_exe(mp_scenario mp_scenario.cpp)
# Multiprocess proof: real TCP coordinator + real worker kill/restart.
add_test(NAME mp_scenario COMMAND $<TARGET_FILE:mp_scenario> $<TARGET_FILE:rf_coordinator> $<TARGET_FILE:rf_worker> 37926)