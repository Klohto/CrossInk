add_executable(HalSpiBusTest HalSpiBusTest.cpp ${REPO_ROOT}/lib/hal/HalSpiBus.cpp)
target_include_directories(HalSpiBusTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/hal
)
target_link_libraries(HalSpiBusTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HalSpiBusTest)
