#include "FakeHttpServer.h"
#include "web/Pages.h"

#include <unity.h>

using namespace espmgr;

void setUp()
{
}

void tearDown()
{
}

// The page's sources are checked by scripts/build-web.py --check; this checks what the
// firmware gets from the generated header.
void test_portal_page_is_gzip()
{
    TEST_ASSERT_GREATER_THAN(100, web::kPortalPageSize);
    TEST_ASSERT_EQUAL_HEX8(0x1f, web::kPortalPage[0]);
    TEST_ASSERT_EQUAL_HEX8(0x8b, web::kPortalPage[1]);
    TEST_ASSERT_EQUAL_HEX8(0x08, web::kPortalPage[2]);  // deflate
}

void test_portal_page_unpacks_to_more_than_it_takes()
{
    // A gzip file ends with the uncompressed size, little-endian.
    const uint8_t* end = web::kPortalPage + web::kPortalPageSize;
    const uint32_t unpacked =
        end[-4] | (end[-3] << 8) | (end[-2] << 16) | (static_cast<uint32_t>(end[-1]) << 24);
    TEST_ASSERT_GREATER_THAN(web::kPortalPageSize, unpacked);
}

void test_portal_page_is_sent_gzipped_as_html()
{
    FakeHttpServer http;
    http.on(HttpMethod::Get, "/", [&http](const HttpRequest&) { web::sendPortalPage(http); });
    http.begin(80);

    const FakeHttpServer::Response response = http.request(HttpMethod::Get, "/");

    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", response.contentType.c_str());
    TEST_ASSERT_TRUE(response.gzipped);
    TEST_ASSERT_EQUAL_size_t(web::kPortalPageSize, response.body.size());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_portal_page_is_gzip);
    RUN_TEST(test_portal_page_unpacks_to_more_than_it_takes);
    RUN_TEST(test_portal_page_is_sent_gzipped_as_html);
    return UNITY_END();
}
