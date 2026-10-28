#include <FakePlatform.h>
#include <core/SavedNetworkStore.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::FakePlatform;
using espmgr::Ipv4Address;
using espmgr::Logger;
using espmgr::LogLevel;
using espmgr::SavedNetwork;
using espmgr::SavedNetworkStore;
using Problem = SavedNetwork::Problem;
using LoadStatus = SavedNetworkStore::LoadStatus;

namespace
{

const char* const kNs = SavedNetworkStore::kNamespace;
const char* const kKey = SavedNetworkStore::kKey;

SavedNetwork home()
{
    SavedNetwork network;
    network.ssid = "Home";
    network.password = "correct horse";
    return network;
}

SavedNetwork office()
{
    SavedNetwork network;
    network.ssid = "Office 5G";
    network.password = "battery staple";
    network.priority = 2;
    network.dhcp = false;
    network.staticIp = {Ipv4Address(10, 0, 0, 20), Ipv4Address(10, 0, 0, 1),
                        Ipv4Address(255, 255, 255, 0), Ipv4Address(10, 0, 0, 1),
                        Ipv4Address(1, 1, 1, 1)};
    return network;
}

struct Fixture
{
    FakePlatform fakes;
    Logger log{fakes.log, fakes.clock};
    SavedNetworkStore store{fakes.storage, log};

    Fixture()
    {
        log.setLevel(LogLevel::Verbose);
    }

    std::vector<uint8_t>& record()
    {
        return std::get<std::vector<uint8_t>>(fakes.storage.data[kNs][kKey]);
    }

    bool logged(const std::string& text) const
    {
        for (const auto& line : fakes.log.lines)
        {
            if (line.text.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- validate() ---

void test_valid_networks_have_no_problem()
{
    TEST_ASSERT_EQUAL(Problem::None, home().validate());
    TEST_ASSERT_EQUAL(Problem::None, office().validate());

    SavedNetwork open = home();
    open.password.clear();
    TEST_ASSERT_EQUAL(Problem::None, open.validate());

    SavedNetwork hexKey = home();
    hexKey.password = std::string(64, 'a').replace(0, 4, "09AF");
    TEST_ASSERT_EQUAL(Problem::None, hexKey.validate());

    SavedNetwork longest = home();
    longest.ssid = std::string(32, 's');
    longest.password = std::string(63, '~');
    TEST_ASSERT_EQUAL(Problem::None, longest.validate());
}

void test_ssid_must_be_1_to_32_bytes()
{
    SavedNetwork network = home();
    network.ssid.clear();
    TEST_ASSERT_EQUAL(Problem::SsidEmpty, network.validate());
    network.ssid = std::string(33, 's');
    TEST_ASSERT_EQUAL(Problem::SsidTooLong, network.validate());
}

void test_password_must_be_empty_8_to_63_printable_or_64_hex()
{
    SavedNetwork network = home();
    network.password = "short12";
    TEST_ASSERT_EQUAL(Problem::PasswordTooShort, network.validate());
    network.password = std::string(65, 'a');
    TEST_ASSERT_EQUAL(Problem::PasswordTooLong, network.validate());
    network.password = std::string(64, 'g');
    TEST_ASSERT_EQUAL(Problem::PasswordNotHex, network.validate());
    network.password = "tab\there!";
    TEST_ASSERT_EQUAL(Problem::PasswordInvalidCharacter, network.validate());
    network.password = "caf\xc3\xa9 au lait";
    TEST_ASSERT_EQUAL(Problem::PasswordInvalidCharacter, network.validate());
}

void test_static_ip_needs_address_gateway_and_a_real_netmask()
{
    SavedNetwork network = office();
    network.staticIp.ip = Ipv4Address();
    TEST_ASSERT_EQUAL(Problem::StaticIpMissing, network.validate());

    network = office();
    network.staticIp.gateway = Ipv4Address();
    TEST_ASSERT_EQUAL(Problem::StaticIpMissing, network.validate());

    const Ipv4Address badMasks[] = {Ipv4Address(), Ipv4Address(255, 0, 255, 0),
                                    Ipv4Address(255, 255, 255, 1), Ipv4Address(0, 255, 255, 255)};
    for (const Ipv4Address& mask : badMasks)
    {
        network = office();
        network.staticIp.subnet = mask;
        TEST_ASSERT_EQUAL_MESSAGE(Problem::SubnetInvalid, network.validate(),
                                  mask.toString().c_str());
    }

    const Ipv4Address goodMasks[] = {Ipv4Address(255, 0, 0, 0), Ipv4Address(255, 255, 252, 0),
                                     Ipv4Address(255, 255, 255, 255)};
    for (const Ipv4Address& mask : goodMasks)
    {
        network = office();
        network.staticIp.subnet = mask;
        TEST_ASSERT_EQUAL_MESSAGE(Problem::None, network.validate(), mask.toString().c_str());
    }
}

void test_dhcp_ignores_the_static_settings()
{
    SavedNetwork network = home();
    network.staticIp.subnet = Ipv4Address(1, 2, 3, 4);
    TEST_ASSERT_EQUAL(Problem::None, network.validate());
}

// --- SavedNetworkStore ---

void test_nothing_saved_loads_as_missing()
{
    Fixture f;
    const auto result = f.store.load();
    TEST_ASSERT_EQUAL(LoadStatus::Missing, result.status);
    TEST_ASSERT_EQUAL(0, result.networks.size());
}

void test_round_trips_a_single_network()
{
    Fixture f;
    TEST_ASSERT_TRUE(f.store.save({home()}));
    const auto result = f.store.load();
    TEST_ASSERT_EQUAL(LoadStatus::Ok, result.status);
    TEST_ASSERT_EQUAL(1, result.networks.size());
    TEST_ASSERT_TRUE(result.networks[0] == home());
}

void test_round_trips_a_list_with_static_addressing_in_order()
{
    Fixture f;
    SavedNetwork open = home();
    open.ssid = "Cafe";
    open.password.clear();
    open.priority = 9;
    TEST_ASSERT_TRUE(f.store.save({office(), home(), open}));
    const auto result = f.store.load();
    TEST_ASSERT_EQUAL(LoadStatus::Ok, result.status);
    TEST_ASSERT_EQUAL(3, result.networks.size());
    TEST_ASSERT_TRUE(result.networks[0] == office());
    TEST_ASSERT_TRUE(result.networks[1] == home());
    TEST_ASSERT_TRUE(result.networks[2] == open);
}

void test_saving_replaces_the_whole_list()
{
    Fixture f;
    TEST_ASSERT_TRUE(f.store.save({home(), office()}));
    TEST_ASSERT_TRUE(f.store.save({office()}));
    const auto result = f.store.load();
    TEST_ASSERT_EQUAL(1, result.networks.size());
    TEST_ASSERT_TRUE(result.networks[0] == office());

    TEST_ASSERT_TRUE(f.store.save({}));
    TEST_ASSERT_EQUAL(LoadStatus::Ok, f.store.load().status);
    TEST_ASSERT_EQUAL(0, f.store.load().networks.size());
}

void test_record_is_versioned()
{
    Fixture f;
    TEST_ASSERT_TRUE(f.store.save({home()}));
    TEST_ASSERT_EQUAL_UINT8(SavedNetworkStore::kFormatVersion, f.record().at(0));
    TEST_ASSERT_EQUAL_UINT8(1, f.record().at(1));
}

void test_invalid_network_is_not_saved_and_keeps_the_old_list()
{
    Fixture f;
    TEST_ASSERT_TRUE(f.store.save({home()}));
    SavedNetwork bad = office();
    bad.password = "short12";
    TEST_ASSERT_FALSE(f.store.save({office(), bad}));
    TEST_ASSERT_TRUE(f.logged("not saving Saved Network 1: password is shorter"));
    const auto result = f.store.load();
    TEST_ASSERT_EQUAL(1, result.networks.size());
    TEST_ASSERT_TRUE(result.networks[0] == home());
}

void test_failed_write_is_reported()
{
    Fixture f;
    f.fakes.storage.writable = false;
    TEST_ASSERT_FALSE(f.store.save({home()}));
    TEST_ASSERT_TRUE(f.logged("could not write"));
}

void test_damaged_records_load_as_unreadable_and_are_kept()
{
    Fixture f;
    TEST_ASSERT_TRUE(f.store.save({home(), office()}));
    const std::vector<uint8_t> good = f.record();

    std::vector<std::vector<uint8_t>> damaged;
    damaged.push_back({});                                                  // empty
    damaged.push_back(std::vector<uint8_t>(good.begin(), good.end() - 1));  // cut short
    damaged.push_back(good);
    damaged.back().push_back(0);  // trailing byte
    damaged.push_back(good);
    damaged.back()[0] = SavedNetworkStore::kFormatVersion + 1;  // newer firmware
    damaged.push_back(good);
    damaged.back()[1] = 3;  // more networks than present
    damaged.push_back(good);
    damaged.back()[2] = 0;  // empty SSID
    damaged.push_back(good);
    damaged.back()[2] = 200;  // SSID longer than the record

    for (size_t i = 0; i < damaged.size(); ++i)
    {
        f.record() = damaged[i];
        const auto result = f.store.load();
        TEST_ASSERT_EQUAL_MESSAGE(LoadStatus::Unreadable, result.status, std::to_string(i).c_str());
        TEST_ASSERT_EQUAL(0, result.networks.size());
        TEST_ASSERT_TRUE_MESSAGE(f.record() == damaged[i], "record must be left in place");
    }
    TEST_ASSERT_TRUE(f.logged("Saved Networks record unreadable"));
}

void test_unknown_flags_make_the_record_unreadable()
{
    Fixture f;
    TEST_ASSERT_TRUE(f.store.save({home()}));
    // version, count, ssid length + "Home", password length + 13 bytes, priority, flags
    const size_t flags = 2 + 1 + 4 + 1 + 13 + 1;
    f.record()[flags] = 0x80;
    TEST_ASSERT_EQUAL(LoadStatus::Unreadable, f.store.load().status);
}

void test_value_of_another_type_loads_as_missing()
{
    Fixture f;
    f.fakes.storage.data[kNs][kKey] = std::string("not a record");
    TEST_ASSERT_EQUAL(LoadStatus::Missing, f.store.load().status);
}

void test_next_save_replaces_an_unreadable_record()
{
    Fixture f;
    f.fakes.storage.data[kNs][kKey] = std::vector<uint8_t>{99, 1, 2, 3};
    TEST_ASSERT_TRUE(f.store.save({office()}));
    TEST_ASSERT_EQUAL(LoadStatus::Ok, f.store.load().status);
}

void test_clear_removes_only_the_saved_networks()
{
    Fixture f;
    TEST_ASSERT_TRUE(f.store.save({home()}));
    TEST_ASSERT_TRUE(f.fakes.storage.putU32(kNs, "other", 7));
    TEST_ASSERT_TRUE(f.fakes.storage.putString("espmgr-app", "name", "x"));
    TEST_ASSERT_TRUE(f.store.clear());
    TEST_ASSERT_EQUAL(LoadStatus::Missing, f.store.load().status);
    TEST_ASSERT_EQUAL_UINT32(7, f.fakes.storage.getU32(kNs, "other").value());
    TEST_ASSERT_TRUE(f.fakes.storage.getString("espmgr-app", "name").has_value());
}

void test_passwords_never_reach_the_log()
{
    Fixture f;
    SavedNetwork bad = home();
    bad.password = "secret7";  // too short, so save() logs why
    f.store.save({bad});
    f.store.save({home()});
    f.record()[0] = 42;
    f.store.load();
    f.fakes.storage.writable = false;
    f.store.save({home()});

    TEST_ASSERT_TRUE(f.fakes.log.lines.size() >= 3);
    TEST_ASSERT_FALSE(f.logged("secret7"));
    TEST_ASSERT_FALSE(f.logged(home().password));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_networks_have_no_problem);
    RUN_TEST(test_ssid_must_be_1_to_32_bytes);
    RUN_TEST(test_password_must_be_empty_8_to_63_printable_or_64_hex);
    RUN_TEST(test_static_ip_needs_address_gateway_and_a_real_netmask);
    RUN_TEST(test_dhcp_ignores_the_static_settings);
    RUN_TEST(test_nothing_saved_loads_as_missing);
    RUN_TEST(test_round_trips_a_single_network);
    RUN_TEST(test_round_trips_a_list_with_static_addressing_in_order);
    RUN_TEST(test_saving_replaces_the_whole_list);
    RUN_TEST(test_record_is_versioned);
    RUN_TEST(test_invalid_network_is_not_saved_and_keeps_the_old_list);
    RUN_TEST(test_failed_write_is_reported);
    RUN_TEST(test_damaged_records_load_as_unreadable_and_are_kept);
    RUN_TEST(test_unknown_flags_make_the_record_unreadable);
    RUN_TEST(test_value_of_another_type_loads_as_missing);
    RUN_TEST(test_next_save_replaces_an_unreadable_record);
    RUN_TEST(test_clear_removes_only_the_saved_networks);
    RUN_TEST(test_passwords_never_reach_the_log);
    return UNITY_END();
}
