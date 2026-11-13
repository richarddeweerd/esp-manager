// Firmware updates (EP17): SemVer comparison, the Manifest parser, URLs and base64.

#include <core/Base64.h>
#include <core/Manifest.h>
#include <core/SemVer.h>
#include <unity.h>

#include <string>

using espmgr::decodeBase64;
using espmgr::Manifest;
using espmgr::resolveUrl;
using espmgr::SemVer;
using espmgr::withoutQuery;

namespace
{

const std::string kHash = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";

int cmp(const char* a, const char* b)
{
    const auto va = SemVer::parse(a);
    const auto vb = SemVer::parse(b);
    TEST_ASSERT_TRUE(va.has_value());
    TEST_ASSERT_TRUE(vb.has_value());
    return SemVer::compare(*va, *vb);
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_semver_parses_versions()
{
    const auto v = SemVer::parse("1.10.3-beta.2+build.7");
    TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_EQUAL_UINT64(1, v->major);
    TEST_ASSERT_EQUAL_UINT64(10, v->minor);
    TEST_ASSERT_EQUAL_UINT64(3, v->patch);
    TEST_ASSERT_EQUAL(2, v->preRelease.size());
    TEST_ASSERT_EQUAL_STRING("beta", v->preRelease[0].c_str());
    TEST_ASSERT_EQUAL_STRING("2", v->preRelease[1].c_str());
}

void test_semver_refuses_what_isnt_semver()
{
    for (const char* text : {"", "1", "1.2", "1.2.3.4", "v1.2.3", "01.2.3", "1.2.3-", "1.2.3-01",
                             "1.2.3+", "1.2.x", "1.2.3-be ta", "1..3"})
    {
        TEST_ASSERT_FALSE_MESSAGE(SemVer::parse(text).has_value(), text);
    }
}

void test_semver_compares_by_precedence()
{
    TEST_ASSERT_TRUE(cmp("1.10.0", "1.9.5") > 0);
    TEST_ASSERT_TRUE(cmp("2.0.0", "1.99.99") > 0);
    TEST_ASSERT_TRUE(cmp("1.0.1", "1.0.0") > 0);
    TEST_ASSERT_EQUAL(0, cmp("1.2.3", "1.2.3"));
    TEST_ASSERT_EQUAL(0, cmp("1.2.3+a", "1.2.3+b"));
    TEST_ASSERT_TRUE(cmp("1.5.0-beta.1", "1.5.0") < 0);
    // The order from the SemVer spec.
    const char* order[] = {"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                           "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
    for (size_t i = 0; i + 1 < sizeof(order) / sizeof(order[0]); ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(cmp(order[i], order[i + 1]) < 0, order[i]);
        TEST_ASSERT_TRUE_MESSAGE(cmp(order[i + 1], order[i]) > 0, order[i]);
    }
}

void test_manifest_reads_every_field()
{
    const std::string json =
        R"({ "version": "1.4.2", "url": "fw\/1.4.2.bin", "size": 1048576,
        "sha256": ")" +
        std::string("9F86D081884C7D659A2FEAA0C55AD015A3BF4F1B2B0B822CD15D6C15B0F00A08") +
        R"(", "signature": "MEUCIQ==", "minimumVersion": "1.2.0",
        "notes": "Fixed \"portal\" timeout\né", "extra": {"a": [1, 2, {"b": null}]},
        "flag": true, "n": -1.5e3 })";
    const auto m = Manifest::parse(json);
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_EQUAL_STRING("1.4.2", m->version.c_str());
    TEST_ASSERT_EQUAL_STRING("fw/1.4.2.bin", m->url.c_str());
    TEST_ASSERT_EQUAL(1048576, m->size);
    TEST_ASSERT_EQUAL_STRING(kHash.c_str(), m->sha256.c_str());
    TEST_ASSERT_EQUAL_STRING("MEUCIQ==", m->signature.c_str());
    TEST_ASSERT_EQUAL_STRING("1.2.0", m->minimumVersion.c_str());
    TEST_ASSERT_EQUAL_STRING("Fixed \"portal\" timeout\n\xc3\xa9", m->notes.c_str());
}

void test_manifest_needs_the_required_fields()
{
    const std::string ok =
        R"("version":"1.0.0","url":"a.bin","size":10,"sha256":")" + kHash + R"(")";
    TEST_ASSERT_TRUE(Manifest::parse("{" + ok + "}").has_value());
    TEST_ASSERT_FALSE(
        Manifest::parse(R"({"url":"a.bin","size":10,"sha256":")" + kHash + R"("})").has_value());
    TEST_ASSERT_FALSE(
        Manifest::parse(R"({"version":"1.0.0","size":10,"sha256":")" + kHash + R"("})")
            .has_value());
    TEST_ASSERT_FALSE(
        Manifest::parse(R"({"version":"1.0.0","url":"a","sha256":")" + kHash + R"("})")
            .has_value());
    TEST_ASSERT_FALSE(
        Manifest::parse(R"({"version":"1.0.0","url":"a","size":0,"sha256":")" + kHash + R"("})")
            .has_value());
    TEST_ASSERT_FALSE(
        Manifest::parse(R"({"version":"1.0.0","url":"a","size":10,"sha256":"abc"})").has_value());
    // Not JSON, or not one object.
    for (const char* bad : {"", "[]", "{", "{\"version\":}", "{\"a\":1,}", "{} {}", "nope"})
    {
        TEST_ASSERT_FALSE_MESSAGE(Manifest::parse(bad).has_value(), bad);
    }
    TEST_ASSERT_FALSE(Manifest::parse("{" + ok + ",\"size\":\"10\"}").has_value());
}

void test_manifest_has_a_size_limit_and_cuts_long_notes()
{
    const std::string ok =
        R"("version":"1.0.0","url":"a.bin","size":10,"sha256":")" + kHash + R"(")";
    std::string notes(1100, 'x');
    // A two-byte character straddling the limit is dropped whole.
    notes[1023] = '\xc3';
    notes[1024] = '\xa9';
    const auto m = Manifest::parse("{" + ok + ",\"notes\":\"" + notes + "\"}");
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_EQUAL(1023, m->notes.size());
    const std::string big = "{" + ok + ",\"pad\":\"" + std::string(4100, 'y') + "\"}";
    TEST_ASSERT_FALSE(Manifest::parse(big).has_value());
}

void test_image_urls_resolve_against_the_manifest()
{
    const std::string base = "https://updates.example.com/lamp/stable.json";
    TEST_ASSERT_EQUAL_STRING("https://cdn.example.com/x.bin",
                             resolveUrl(base, "https://cdn.example.com/x.bin").c_str());
    TEST_ASSERT_EQUAL_STRING("https://updates.example.com/lamp/fw/x.bin",
                             resolveUrl(base, "fw/x.bin").c_str());
    TEST_ASSERT_EQUAL_STRING("https://updates.example.com/bin/x.bin",
                             resolveUrl(base, "/bin/x.bin").c_str());
    TEST_ASSERT_EQUAL_STRING("https://cdn.example.com/x.bin",
                             resolveUrl(base, "//cdn.example.com/x.bin").c_str());
    TEST_ASSERT_EQUAL_STRING("https://updates.example.com/lamp/x.bin",
                             resolveUrl(base + "?token=1", "x.bin").c_str());
    TEST_ASSERT_EQUAL_STRING("", resolveUrl(base, "http://example.com/x.bin").c_str());
    TEST_ASSERT_EQUAL_STRING("", resolveUrl(base, "ftp://example.com/x.bin").c_str());
    TEST_ASSERT_EQUAL_STRING("https://h/a", withoutQuery("https://h/a?token=s").c_str());
}

void test_base64_decodes()
{
    const auto bytes = decodeBase64("TWFu");
    TEST_ASSERT_TRUE(bytes.has_value());
    TEST_ASSERT_EQUAL_STRING("Man", std::string(bytes->begin(), bytes->end()).c_str());
    const auto padded = decodeBase64("TWE=");
    TEST_ASSERT_EQUAL_STRING("Ma", std::string(padded->begin(), padded->end()).c_str());
    const auto unpadded = decodeBase64("TQ");
    TEST_ASSERT_EQUAL_STRING("M", std::string(unpadded->begin(), unpadded->end()).c_str());
    TEST_ASSERT_TRUE(decodeBase64("").has_value());
    TEST_ASSERT_FALSE(decodeBase64("TWF*").has_value());
    TEST_ASSERT_FALSE(decodeBase64("T").has_value());
    TEST_ASSERT_FALSE(decodeBase64("TQ===").has_value());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_semver_parses_versions);
    RUN_TEST(test_semver_refuses_what_isnt_semver);
    RUN_TEST(test_semver_compares_by_precedence);
    RUN_TEST(test_manifest_reads_every_field);
    RUN_TEST(test_manifest_needs_the_required_fields);
    RUN_TEST(test_manifest_has_a_size_limit_and_cuts_long_notes);
    RUN_TEST(test_image_urls_resolve_against_the_manifest);
    RUN_TEST(test_base64_decodes);
    return UNITY_END();
}
