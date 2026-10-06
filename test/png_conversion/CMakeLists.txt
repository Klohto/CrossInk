find_package(ZLIB REQUIRED)

add_executable(PngConversionTest
  PngConversionTest.cpp
  ${REPO_ROOT}/lib/PngToBmpConverter/PngToBmpConverter.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)

target_include_directories(PngConversionTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/bitmap_resample/stubs
  ${REPO_ROOT}/lib/PngToBmpConverter
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(PngConversionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  ZLIB::ZLIB
)

gtest_discover_tests(PngConversionTest)
