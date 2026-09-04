set(RF_WARN_COMMON)
if(MSVC)
  set(RF_WARN_COMMON /W4 /WX)
else()
  set(RF_WARN_COMMON -Wall -Wextra -Werror)
endif()

set(RF_TESTS
  test_interval
  test_lifecycle
  test_accounting
  test_bundle
  test_stale
  test_persistence
  test_property
  test_race
  test_adversarial
)
foreach(t IN LISTS RF_TESTS)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE ReservationFabric::ReservationFabric)
  target_compile_options(${t} PRIVATE ${RF_WARN_COMMON})
  add_test(NAME ${t} COMMAND ${t})
endforeach()

if(RESERVATION_FABRIC_ENABLE_ASAN AND MSVC)
  foreach(t IN LISTS RF_TESTS)
    target_compile_options(${t} PRIVATE /fsanitize=address /O1 /Zi)
    target_link_options(${t} PRIVATE /fsanitize=address)
  endforeach()
endif()