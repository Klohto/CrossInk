find_package(ZLIB REQUIRED)

add_executable(UzlibInflateTest
  UzlibInflateTest.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${REPO_ROOT}/lib/uzlib/src/uzlib_checksums.c
)
target_include_directories(UzlibInflateTest PRIVATE ${REPO_ROOT}/lib/uzlib/src)
target_link_libraries(UzlibInflateTest PRIVATE crosspoint_test_common GTest::gtest_main ZLIB::ZLIB)
gtest_discover_tests(UzlibInflateTest)
