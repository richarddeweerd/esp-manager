#include <DigestClient.h>
#include <FakeClock.h>
#include <core/DigestAuth.h>
#include <unity.h>

#include <string>

using espmgr::DigestAuth;
using espmgr::DigestClient;
using espmgr::FakeClock;
using espmgr::HttpMethod;
using espmgr::HttpRequest;

namespace
{

const char* const kRealm = "esp-abcdef";

DigestClient owner()
{
    DigestClient client;
    client.realm = kRealm;
    client.password = "owner password";
    return client;
}

const std::string kHa1 = DigestAuth::ha1("admin", kRealm, "owner password");

HttpRequest request(const std::string& path, const std::string& authorization)
{
    HttpRequest request;
    request.method = HttpMethod::Get;
    request.path = path;
    if (!authorization.empty())
    {
        request.headers.emplace_back("Authorization", authorization);
    }
    return request;
}

struct Fixture
{
    FakeClock clock;
    DigestAuth auth{clock};

    // A nonce from a new challenge; `seed` makes it different from the others.
    std::string nonce(uint32_t seed = 1)
    {
        clock.randomValues = {seed, seed, seed, seed};
        return DigestClient::nonceFrom(auth.challenge(kRealm, false));
    }

    DigestAuth::Check check(const std::string& path, const std::string& authorization)
    {
        return auth.check(request(path, authorization), "admin", kRealm, kHa1);
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// The example in RFC 2617, section 3.5, so the test's client is right.
void test_client_matches_rfc_2617()
{
    DigestClient client;
    client.user = "Mufasa";
    client.realm = "testrealm@host.com";
    client.password = "Circle Of Life";
    TEST_ASSERT_EQUAL_STRING(
        "6629fae49393a05397450978507c4ef1",
        client.response("dcd98b7102dd2f0e8b11d0f600bfb0c093", "GET", "/dir/index.html", "00000001")
            .c_str());
}

// The device didn't hand out this nonce, but the answer is right: the browser should retry
// with a new nonce without asking again.
void test_rfc_2617_request_is_stale_here()
{
    FakeClock clock;
    DigestAuth auth{clock};
    DigestClient client;
    client.user = "Mufasa";
    client.realm = "testrealm@host.com";
    client.password = "Circle Of Life";
    const std::string header =
        client.header("dcd98b7102dd2f0e8b11d0f600bfb0c093", "GET", "/dir/index.html");
    TEST_ASSERT_EQUAL(
        DigestAuth::Check::Stale,
        auth.check(request("/dir/index.html", header), "Mufasa", "testrealm@host.com",
                   DigestAuth::ha1("Mufasa", "testrealm@host.com", "Circle Of Life")));
}

void test_challenge_names_realm_qop_and_a_random_nonce()
{
    FakeClock clock;
    DigestAuth auth{clock};
    clock.randomValues = {0x01234567, 0x89abcdef, 0xdeadbeef, 0x00000042};
    TEST_ASSERT_EQUAL_STRING("Digest realm=\"esp-abcdef\", qop=\"auth\", algorithm=MD5, "
                             "nonce=\"0123456789abcdefdeadbeef00000042\"",
                             auth.challenge(kRealm, false).c_str());
    TEST_ASSERT_TRUE(auth.challenge(kRealm, true).find(", stale=true") != std::string::npos);
}

void test_right_password_with_an_issued_nonce_is_accepted()
{
    Fixture f;
    const std::string nonce = f.nonce();
    TEST_ASSERT_EQUAL(DigestAuth::Check::Ok,
                      f.check("/api/status", owner().header(nonce, "GET", "/api/status")));
}

void test_wrong_password_is_refused()
{
    Fixture f;
    DigestClient client = owner();
    client.password = "guessed password";
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused,
                      f.check("/", client.header(f.nonce(), "GET", "/")));
}

void test_other_user_or_realm_is_refused()
{
    Fixture f;
    const std::string nonce = f.nonce();
    DigestClient client = owner();
    client.user = "root";
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused, f.check("/", client.header(nonce, "GET", "/")));
    client = owner();
    client.realm = "other";
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused, f.check("/", client.header(nonce, "GET", "/")));
}

void test_no_header_or_basic_is_refused()
{
    Fixture f;
    f.nonce();
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused, f.check("/", ""));
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused, f.check("/", "Basic YWRtaW46b3duZXI="));
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused, f.check("/", "Digest username=\"admin"));
}

void test_no_password_stored_refuses_everything()
{
    Fixture f;
    const std::string header = owner().header(f.nonce(), "GET", "/");
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused,
                      f.auth.check(request("/", header), "admin", kRealm, ""));
}

void test_uri_must_be_the_requested_path()
{
    Fixture f;
    const std::string nonce = f.nonce();
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused,
                      f.check("/api/status", owner().header(nonce, "GET", "/api/networks")));
    // The query is part of the uri, not of the path.
    TEST_ASSERT_EQUAL(DigestAuth::Check::Ok,
                      f.check("/api/scan", owner().header(nonce, "GET", "/api/scan?refresh=1")));
}

void test_the_same_count_twice_is_a_replay()
{
    Fixture f;
    const std::string nonce = f.nonce();
    const std::string first = owner().header(nonce, "GET", "/");
    TEST_ASSERT_EQUAL(DigestAuth::Check::Ok, f.check("/", first));
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused, f.check("/", first));
    TEST_ASSERT_EQUAL(DigestAuth::Check::Ok,
                      f.check("/", owner().header(nonce, "GET", "/", "00000002")));
}

void test_nonce_expires_after_ten_minutes()
{
    Fixture f;
    const std::string nonce = f.nonce();
    f.clock.advance(DigestAuth::kNonceLifetimeMs - 1);
    TEST_ASSERT_EQUAL(DigestAuth::Check::Ok, f.check("/", owner().header(nonce, "GET", "/")));
    f.clock.advance(1);
    TEST_ASSERT_EQUAL(DigestAuth::Check::Stale,
                      f.check("/", owner().header(nonce, "GET", "/", "00000002")));
}

void test_oldest_nonce_is_dropped()
{
    Fixture f;
    const std::string oldest = f.nonce(1);
    for (uint32_t i = 0; i < DigestAuth::kMaxNonces; ++i)
    {
        f.nonce(100 + i);
    }
    TEST_ASSERT_EQUAL(DigestAuth::Check::Stale, f.check("/", owner().header(oldest, "GET", "/")));
}

void test_post_uses_its_own_method()
{
    Fixture f;
    const std::string nonce = f.nonce();
    HttpRequest post = request("/api/x", owner().header(nonce, "GET", "/api/x"));
    post.method = HttpMethod::Post;
    TEST_ASSERT_EQUAL(DigestAuth::Check::Refused, f.auth.check(post, "admin", kRealm, kHa1));
    post.headers = {{"Authorization", owner().header(nonce, "POST", "/api/x")}};
    TEST_ASSERT_EQUAL(DigestAuth::Check::Ok, f.auth.check(post, "admin", kRealm, kHa1));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_client_matches_rfc_2617);
    RUN_TEST(test_rfc_2617_request_is_stale_here);
    RUN_TEST(test_challenge_names_realm_qop_and_a_random_nonce);
    RUN_TEST(test_right_password_with_an_issued_nonce_is_accepted);
    RUN_TEST(test_wrong_password_is_refused);
    RUN_TEST(test_other_user_or_realm_is_refused);
    RUN_TEST(test_no_header_or_basic_is_refused);
    RUN_TEST(test_no_password_stored_refuses_everything);
    RUN_TEST(test_uri_must_be_the_requested_path);
    RUN_TEST(test_the_same_count_twice_is_a_replay);
    RUN_TEST(test_nonce_expires_after_ten_minutes);
    RUN_TEST(test_oldest_nonce_is_dropped);
    RUN_TEST(test_post_uses_its_own_method);
    return UNITY_END();
}
