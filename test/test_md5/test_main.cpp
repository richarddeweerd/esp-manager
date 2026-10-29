#include <core/Md5.h>
#include <unity.h>

#include <string>

using espmgr::md5Hex;

void setUp()
{
}

void tearDown()
{
}

// The test suite in RFC 1321, appendix A.5.
void test_rfc_1321_test_suite()
{
    TEST_ASSERT_EQUAL_STRING("d41d8cd98f00b204e9800998ecf8427e", md5Hex("").c_str());
    TEST_ASSERT_EQUAL_STRING("0cc175b9c0f1b6a831c399e269772661", md5Hex("a").c_str());
    TEST_ASSERT_EQUAL_STRING("900150983cd24fb0d6963f7d28e17f72", md5Hex("abc").c_str());
    TEST_ASSERT_EQUAL_STRING("f96b697d7cb7938d525a2f31aaf161d0", md5Hex("message digest").c_str());
    TEST_ASSERT_EQUAL_STRING("c3fcd3d76192e4007dfb496cca67e13b",
                             md5Hex("abcdefghijklmnopqrstuvwxyz").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "d174ab98d277d9f5a5611c2c9f419d9f",
        md5Hex("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789").c_str());
    TEST_ASSERT_EQUAL_STRING("57edf4a22be3c955ac49da2e2107b67a",
                             md5Hex(std::string("1234567890123456789012345678901234567890"
                                                "1234567890123456789012345678901234567890"))
                                 .c_str());
}

// Where the padding needs a second block, or a block ends exactly.
void test_lengths_around_the_block_size()
{
    TEST_ASSERT_EQUAL_STRING("ef1772b6dff9a122358552954ad0df65",
                             md5Hex(std::string(55, 'a')).c_str());
    TEST_ASSERT_EQUAL_STRING("3b0c8ac703f828b04c6c197006d17218",
                             md5Hex(std::string(56, 'a')).c_str());
    TEST_ASSERT_EQUAL_STRING("014842d480b571495a4a0363793f7367",
                             md5Hex(std::string(64, 'a')).c_str());
    TEST_ASSERT_EQUAL_STRING("8a7bd0732ed6a28ce75f6dabc90e1613",
                             md5Hex(std::string(119, 'a')).c_str());
    TEST_ASSERT_EQUAL_STRING("5f61c0ccad4cac44c75ff505e1f1e537",
                             md5Hex(std::string(120, 'a')).c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc_1321_test_suite);
    RUN_TEST(test_lengths_around_the_block_size);
    return UNITY_END();
}
