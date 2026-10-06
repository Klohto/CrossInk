#include <gtest/gtest.h>

#include "lib/JsonParser/ReleaseJsonParser.h"
#include "src/network/OtaReleasePolicy.h"

TEST(OtaReleasePolicy, OrdersForkVersions) {
  EXPECT_GT(OtaReleasePolicy::compare("1.6.1-2.1", "1.6.1-2.0"), 0);
  EXPECT_GT(OtaReleasePolicy::compare("1.6.1-2.0", "1.6.1-1.0"), 0);
  EXPECT_GT(OtaReleasePolicy::compare("1.6.1-10.0", "1.6.1-2.9"), 0);
  EXPECT_LT(OtaReleasePolicy::compare("1.6.1-1.0", "1.6.1-2.0"), 0);
  EXPECT_EQ(OtaReleasePolicy::compare("v1.6.1-2.0", "1.6.1-2.0"), 0);
}
TEST(OtaReleasePolicy, KeepsUpstreamAndCandidateOrder) {
  EXPECT_GT(OtaReleasePolicy::compare("v1.6.2", "1.6.1-2.1"), 0);
  EXPECT_GT(OtaReleasePolicy::compare("1.6.1-2.0", "v1.6.1"), 0);
  EXPECT_GT(OtaReleasePolicy::compare("v1.6.1", "v1.6.1-rc1"), 0);
  EXPECT_LT(OtaReleasePolicy::compare("1.6.1-2.1-rc1", "1.6.1-2.1"), 0);
  EXPECT_EQ(OtaReleasePolicy::compare("1.6.1-2.1", "1.6.1-2.1"), 0);
}
TEST(OtaReleasePolicy, RejectsMalformedAndOverflowingVersions) {
  for (const auto* text : {"", "garbage", "1.6.", "1.6.1-2.", "1.6.1-2.1.4", "99999999999999999.6.1"}) {
    EXPECT_FALSE(OtaReleasePolicy::parse(text).valid) << text;
  }
  EXPECT_FALSE(OtaReleasePolicy::parse(nullptr).valid);
}
TEST(OtaReleasePolicy, SelectsTheForkX3Binary) {
  EXPECT_TRUE(OtaReleasePolicy::firmwareAssetMatches("crossink-1.6.1-2.1-x3.bin", "x3-x4"));
  EXPECT_TRUE(OtaReleasePolicy::firmwareAssetMatches("firmware-x3-x4.bin", "x3-x4"));
  EXPECT_FALSE(OtaReleasePolicy::firmwareAssetMatches("crossink-1.6.1-2.1-x3.zip", "x3-x4"));
  EXPECT_FALSE(OtaReleasePolicy::firmwareAssetMatches("crossink-1.6.1-2.1-x4-pro.bin", "x3-x4"));
  EXPECT_FALSE(OtaReleasePolicy::firmwareAssetMatches("crossink-1.6.1-2.1-x3.bin", "sticky"));
  EXPECT_TRUE(OtaReleasePolicy::firmwareAssetMatches("firmware-sticky.bin", "sticky"));
  EXPECT_FALSE(OtaReleasePolicy::firmwareAssetMatches("firmware-stickything.bin", "sticky"));
  EXPECT_FALSE(OtaReleasePolicy::firmwareAssetMatches(nullptr, "x3-x4"));
}

TEST(OtaReleasePolicy, ReadsForkManifestAndRetainsDownloadDigest) {
  constexpr char manifest[] =
      R"({"tag_name":"1.6.1-2.0","assets":[{"name":"crossink-1.6.1-2.0-x3.bin","size":6333744,"digest":"sha256:67847a8dc2ce29578db7e2a8db74951323278779cf46588f1494c211f311e924","browser_download_url":"https://github.com/Klohto/CrossInk/releases/download/1.6.1-2.0/crossink-1.6.1-2.0-x3.bin"}]})";
  ReleaseJsonParser parser([](const char* name) { return OtaReleasePolicy::firmwareAssetMatches(name, "x3-x4"); });
  for (size_t offset = 0; offset < sizeof(manifest) - 1; ++offset) parser.feed(manifest + offset, 1);
  ASSERT_TRUE(parser.foundTag());
  ASSERT_TRUE(parser.foundFirmware());
  EXPECT_STREQ(parser.getTagName(), "1.6.1-2.0");
  EXPECT_EQ(parser.getFirmwareSize(), 6333744U);
  EXPECT_STREQ(parser.getFirmwareSha256(), "67847a8dc2ce29578db7e2a8db74951323278779cf46588f1494c211f311e924");
  EXPECT_STREQ(parser.getFirmwareUrl(),
               "https://github.com/Klohto/CrossInk/releases/download/1.6.1-2.0/crossink-1.6.1-2.0-x3.bin");
}
