#include <EspManager.h>
#include <FakePlatform.h>
#include <core/Events.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::ConnectedInfo;
using espmgr::ConnectionState;
using espmgr::Events;
using espmgr::FailureReason;
using espmgr::FakePlatform;
using espmgr::Ipv4Address;
using espmgr::Logger;
using espmgr::LogLevel;

namespace espmgr
{

// Lets the tests raise events the way the internal parts will.
struct EspManagerTestAccess
{
    static Events& events(EspManager& esp)
    {
        return esp.events_;
    }
};

}  // namespace espmgr

namespace
{

struct Fixture
{
    FakePlatform fakes;
    Logger log{fakes.log, fakes.clock};
    Events events{log};
    std::vector<std::string> seen;

    Fixture()
    {
        log.setLevel(LogLevel::Verbose);
    }

    // Records every event as text, in the order delivered.
    void recordAll()
    {
        events.connected.add([this](const ConnectedInfo& info)
                             { seen.push_back("connected " + info.ssid); });
        events.disconnected.add([this](FailureReason) { seen.push_back("disconnected"); });
        events.connectFailed.add([this](const std::string& ssid, FailureReason)
                                 { seen.push_back("failed " + ssid); });
        events.connectionStateChanged.add(
            [this](ConnectionState from, ConnectionState to)
            {
                seen.push_back("state " + std::to_string(static_cast<int>(from)) + ">" +
                               std::to_string(static_cast<int>(to)));
            });
        events.portalStarted.add([this](const std::string& ssid)
                                 { seen.push_back("portal started " + ssid); });
        events.portalStopped.add([this] { seen.push_back("portal stopped"); });
        events.provisioningCompleted.add([this](const std::string& ssid)
                                         { seen.push_back("provisioned " + ssid); });
    }
};

std::string joined(const std::vector<std::string>& items)
{
    std::string out;
    for (const std::string& item : items)
    {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_raised_events_wait_for_deliver()
{
    Fixture f;
    f.recordAll();
    f.events.raisePortalStopped();
    f.events.raiseDisconnected(FailureReason::ConnectionLost);
    TEST_ASSERT_EQUAL(0, f.seen.size());
    TEST_ASSERT_EQUAL(2, f.events.pending());
}

void test_events_are_delivered_in_the_order_raised()
{
    Fixture f;
    f.recordAll();
    f.events.raiseConnectionStateChanged(ConnectionState::Idle, ConnectionState::Connecting);
    f.events.raiseConnectFailed("Home", FailureReason::WrongPassword);
    f.events.raiseConnectionStateChanged(ConnectionState::Connecting, ConnectionState::Waiting);
    f.events.raisePortalStarted("esp-abcdef");
    f.events.raiseProvisioningCompleted("Office");
    f.events.raisePortalStopped();
    f.events.raiseConnected({"Office", Ipv4Address(10, 0, 0, 5), 11});
    f.events.raiseDisconnected(FailureReason::ConnectionLost);
    f.events.deliver();
    TEST_ASSERT_EQUAL_STRING("state 0>1, failed Home, state 1>3, portal started esp-abcdef, "
                             "provisioned Office, portal stopped, connected Office, disconnected",
                             joined(f.seen).c_str());
    TEST_ASSERT_EQUAL(0, f.events.pending());
}

void test_every_listener_gets_the_event_in_the_order_added()
{
    Fixture f;
    f.events.portalStopped.add([&f] { f.seen.push_back("first"); });
    f.events.portalStopped.add([&f] { f.seen.push_back("second"); });
    f.events.raisePortalStopped();
    f.events.raisePortalStopped();
    f.events.deliver();
    TEST_ASSERT_EQUAL_STRING("first, second, first, second", joined(f.seen).c_str());
}

void test_payloads_arrive_intact()
{
    Fixture f;
    ConnectedInfo got;
    std::string failedSsid;
    FailureReason failedReason = FailureReason::None;
    FailureReason lostReason = FailureReason::None;
    f.events.connected.add([&](const ConnectedInfo& info) { got = info; });
    f.events.connectFailed.add(
        [&](const std::string& ssid, FailureReason reason)
        {
            failedSsid = ssid;
            failedReason = reason;
        });
    f.events.disconnected.add([&](FailureReason reason) { lostReason = reason; });

    std::string ssid = "Home";
    f.events.raiseConnected({ssid, Ipv4Address(192, 168, 1, 50), 6});
    f.events.raiseConnectFailed(ssid, FailureReason::NetworkNotFound);
    f.events.raiseDisconnected(FailureReason::Timeout);
    ssid = "changed after raising";
    f.events.deliver();

    TEST_ASSERT_EQUAL_STRING("Home", got.ssid.c_str());
    TEST_ASSERT_TRUE(got.ip == Ipv4Address(192, 168, 1, 50));
    TEST_ASSERT_EQUAL_UINT8(6, got.channel);
    TEST_ASSERT_EQUAL_STRING("Home", failedSsid.c_str());
    TEST_ASSERT_EQUAL(FailureReason::NetworkNotFound, failedReason);
    TEST_ASSERT_EQUAL(FailureReason::Timeout, lostReason);
}

void test_events_raised_by_a_listener_follow_in_the_same_delivery()
{
    Fixture f;
    f.recordAll();
    // Like a listener that stops the portal as soon as the device connects.
    f.events.connected.add([&f](const ConnectedInfo&) { f.events.raisePortalStopped(); });
    f.events.raiseConnected({"Home", Ipv4Address(), 1});
    f.events.raiseConnectionStateChanged(ConnectionState::Connecting, ConnectionState::Connected);
    f.events.deliver();
    TEST_ASSERT_EQUAL_STRING("connected Home, state 1>2, portal stopped", joined(f.seen).c_str());
}

void test_listener_added_during_delivery_gets_the_next_event()
{
    Fixture f;
    f.events.portalStopped.add(
        [&f]
        {
            f.seen.push_back("original");
            if (f.events.portalStopped.size() == 1)
            {
                f.events.portalStopped.add([&f] { f.seen.push_back("added"); });
            }
        });
    f.events.raisePortalStopped();
    f.events.raisePortalStopped();
    f.events.deliver();
    TEST_ASSERT_EQUAL_STRING("original, original, added", joined(f.seen).c_str());
}

void test_deliver_from_inside_a_listener_does_nothing()
{
    Fixture f;
    f.recordAll();
    f.events.portalStopped.add([&f] { f.events.deliver(); });
    f.events.raisePortalStopped();
    f.events.raiseDisconnected(FailureReason::ConnectionLost);
    f.events.deliver();
    TEST_ASSERT_EQUAL_STRING("portal stopped, disconnected", joined(f.seen).c_str());
}

void test_listeners_raising_each_other_cannot_hang_process()
{
    Fixture f;
    int calls = 0;
    f.events.portalStopped.add(
        [&]
        {
            ++calls;
            f.events.raisePortalStopped();
        });
    f.events.raisePortalStopped();
    f.events.deliver();
    TEST_ASSERT_EQUAL(Events::kMaxPerDelivery, calls);
    TEST_ASSERT_EQUAL(1, f.events.pending());
    TEST_ASSERT_EQUAL(LogLevel::Warn, f.fakes.log.lines.back().level);
}

void test_empty_listener_is_ignored()
{
    Fixture f;
    f.events.portalStopped.add(nullptr);
    f.events.raisePortalStopped();
    f.events.deliver();
    TEST_ASSERT_EQUAL(0, f.events.portalStopped.size());
}

void test_espmanager_delivers_on_process_to_every_listener()
{
    FakePlatform fakes;
    EspManager esp(fakes.platform);
    std::vector<std::string> seen;
    EspManager& chained =
        esp.onConnected([&](const ConnectedInfo& info) { seen.push_back("a " + info.ssid); })
            .onConnected([&](const ConnectedInfo& info) { seen.push_back("b " + info.ssid); })
            .onDisconnected([&](FailureReason) { seen.push_back("lost"); })
            .onConnectFailed([&](const std::string&, FailureReason) {})
            .onConnectionStateChanged([&](ConnectionState, ConnectionState) {})
            .onPortalStarted([&](const std::string&) {})
            .onPortalStopped([&] {})
            .onProvisioningCompleted([&](const std::string&) {});
    TEST_ASSERT_EQUAL_PTR(&esp, &chained);

    esp.begin();
    Events& events = espmgr::EspManagerTestAccess::events(esp);
    events.raiseConnected({"Home", Ipv4Address(), 1});
    events.raiseDisconnected(FailureReason::ConnectionLost);
    TEST_ASSERT_EQUAL(0, seen.size());

    esp.process();
    TEST_ASSERT_EQUAL_STRING("a Home, b Home, lost", joined(seen).c_str());
    esp.process();
    TEST_ASSERT_EQUAL(3, seen.size());
}

void test_listener_may_call_process()
{
    FakePlatform fakes;
    EspManager esp(fakes.platform);
    std::vector<std::string> seen;
    esp.onPortalStopped(
        [&]
        {
            seen.push_back("stopped");
            esp.process();
        });
    esp.onDisconnected([&](FailureReason) { seen.push_back("lost"); });
    Events& events = espmgr::EspManagerTestAccess::events(esp);
    events.raisePortalStopped();
    events.raiseDisconnected(FailureReason::ConnectionLost);
    esp.process();
    TEST_ASSERT_EQUAL_STRING("stopped, lost", joined(seen).c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_raised_events_wait_for_deliver);
    RUN_TEST(test_events_are_delivered_in_the_order_raised);
    RUN_TEST(test_every_listener_gets_the_event_in_the_order_added);
    RUN_TEST(test_payloads_arrive_intact);
    RUN_TEST(test_events_raised_by_a_listener_follow_in_the_same_delivery);
    RUN_TEST(test_listener_added_during_delivery_gets_the_next_event);
    RUN_TEST(test_deliver_from_inside_a_listener_does_nothing);
    RUN_TEST(test_listeners_raising_each_other_cannot_hang_process);
    RUN_TEST(test_empty_listener_is_ignored);
    RUN_TEST(test_espmanager_delivers_on_process_to_every_listener);
    RUN_TEST(test_listener_may_call_process);
    return UNITY_END();
}
