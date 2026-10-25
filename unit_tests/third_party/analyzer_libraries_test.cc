// Checks that the analyzer side's third-party libraries link and work with
// the settings of third_party/CMakeLists.txt.

#include <libxml/parser.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <pcre2.h>
#include <yaml.h>
#include <yyjson.h>
#include <zip.h>
#include <zlib.h>

#include <array>
#include <string>
#include <string_view>

#include "gtest/gtest.h"

namespace cc {
namespace {

TEST(ThirdPartyTest, ZlibRoundTrip) {
  const std::string text(1000, 'a');
  std::array<Bytef, 128> packed{};
  uLongf packed_size = packed.size();
  ASSERT_EQ(compress2(packed.data(), &packed_size,
                      reinterpret_cast<const Bytef*>(text.data()),
                      text.size(), Z_BEST_COMPRESSION),
            Z_OK);
  std::string unpacked(text.size(), '\0');
  uLongf unpacked_size = unpacked.size();
  ASSERT_EQ(uncompress(reinterpret_cast<Bytef*>(unpacked.data()),
                       &unpacked_size, packed.data(), packed_size),
            Z_OK);
  EXPECT_EQ(unpacked, text);
}

TEST(ThirdPartyTest, Pcre2MatchesPythonSyntaxWithJit) {
  uint32_t jit = 0;
  pcre2_config(PCRE2_CONFIG_JIT, &jit);
  ASSERT_EQ(jit, 1u);

  constexpr std::string_view kPattern = R"((?P<word>\w+)(?<!x))";
  int error = 0;
  PCRE2_SIZE offset = 0;
  pcre2_code* code = pcre2_compile(
      reinterpret_cast<PCRE2_SPTR>(kPattern.data()), kPattern.size(),
      PCRE2_UTF | PCRE2_UCP, &error, &offset, nullptr);
  ASSERT_NE(code, nullptr);
  EXPECT_EQ(pcre2_jit_compile(code, PCRE2_JIT_COMPLETE), 0);

  constexpr std::string_view kSubject = "héllo";
  pcre2_match_data* match = pcre2_match_data_create_from_pattern(code, nullptr);
  EXPECT_EQ(pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(kSubject.data()),
                        kSubject.size(), 0, 0, match, nullptr),
            2);
  EXPECT_EQ(pcre2_get_ovector_pointer(match)[1], kSubject.size());
  pcre2_match_data_free(match);
  pcre2_code_free(code);
}

TEST(ThirdPartyTest, YyjsonReadsNanLikePython) {
  constexpr std::string_view kJson = "[1, NaN, -Infinity]";
  yyjson_doc* doc =
      yyjson_read(kJson.data(), kJson.size(), YYJSON_READ_ALLOW_INF_AND_NAN);
  ASSERT_NE(doc, nullptr);
  EXPECT_EQ(yyjson_arr_size(yyjson_doc_get_root(doc)), 3u);
  yyjson_doc_free(doc);
}

TEST(ThirdPartyTest, LibyamlParses) {
  constexpr std::string_view kYaml = "a: yes\n";
  yaml_parser_t parser;
  ASSERT_TRUE(yaml_parser_initialize(&parser));
  yaml_parser_set_input_string(
      &parser, reinterpret_cast<const unsigned char*>(kYaml.data()),
      kYaml.size());
  int scalars = 0;
  yaml_event_t event;
  bool done = false;
  while (!done) {
    ASSERT_TRUE(yaml_parser_parse(&parser, &event));
    scalars += event.type == YAML_SCALAR_EVENT;
    done = event.type == YAML_STREAM_END_EVENT;
    yaml_event_delete(&event);
  }
  yaml_parser_delete(&parser);
  EXPECT_EQ(scalars, 2);
}

TEST(ThirdPartyTest, Libxml2ParsesPlist) {
  constexpr std::string_view kXml = "<plist><dict/></plist>";
  xmlDocPtr doc = xmlReadMemory(kXml.data(), kXml.size(), nullptr, nullptr,
                                XML_PARSE_NONET);
  ASSERT_NE(doc, nullptr);
  EXPECT_STREQ(reinterpret_cast<const char*>(xmlDocGetRootElement(doc)->name),
               "plist");
  xmlFreeDoc(doc);
}

TEST(ThirdPartyTest, LibzipOverwritesEntries) {
  const std::string path = testing::TempDir() + "third_party_test.zip";
  constexpr std::string_view kText = "data";
  int error = 0;
  zip_t* archive = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
  ASSERT_NE(archive, nullptr);
  for (int i = 0; i < 2; ++i) {
    zip_source_t* source =
        zip_source_buffer(archive, kText.data(), kText.size(), 0);
    ASSERT_GE(zip_file_add(archive, "a.txt", source, ZIP_FL_OVERWRITE), 0);
  }
  ASSERT_EQ(zip_close(archive), 0);

  archive = zip_open(path.c_str(), ZIP_RDONLY, &error);
  ASSERT_NE(archive, nullptr);
  EXPECT_EQ(zip_get_num_entries(archive, 0), 1);
  zip_discard(archive);
}

TEST(ThirdPartyTest, OpensslDigests) {
  constexpr std::string_view kText = "abc";
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int size = 0;
  ASSERT_EQ(EVP_Digest(kText.data(), kText.size(), digest.data(), &size,
                       EVP_md5(), nullptr),
            1);
  ASSERT_EQ(size, 16u);
  EXPECT_EQ(digest[0], 0x90);
  EXPECT_EQ(digest[15], 0x72);
  // Names from Python's hashlib.algorithms_guaranteed, in OpenSSL spelling.
  for (const char* name : {"SHA3-256", "BLAKE2b512", "SHAKE256"}) {
    EXPECT_NE(EVP_get_digestbyname(name), nullptr) << name;
  }
}

TEST(ThirdPartyTest, OpensslCreatesTlsContext) {
  SSL_CTX* context = SSL_CTX_new(TLS_method());
  ASSERT_NE(context, nullptr);
  SSL_CTX_free(context);
}

}  // namespace
}  // namespace cc
