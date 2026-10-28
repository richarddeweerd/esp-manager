#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

using espmgr::ConnectionState;
using espmgr::FailureReason;
using espmgr::FakePlatform;

namespace
{

struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};

    explicit Device(bool withNetwork = true)
    {
        if (withNetwork)
        {
            TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        }
    }

    // Lets the fake router answer while waitForConnection() waits: the scan finishes after
    // `scanMs` and the connection succeeds `connectMs` after that.
    void routerAnswers(uint64_t scanMs, uint64_t connectMs)
    {
        fakes.clock.onDelay = [this, scanMs, connectMs]
        {
            const uint64_t now = fakes.clock.nowMs();
            if (!fakes.wifi.scanComplete() && now >= scanMs)
            {
                fakes.wifi.completeScan({{"Home", -55, 6, true}});
                connectAtMs_ = now + connectMs;
            }
            if (connectAtMs_ != 0 && now >= connectAtMs_ &&
                fakes.wifi.status() == espmgr::StationStatus::Connecting)
            {
                fakes.wifi.completeConnect();
            }
        };
    }

private:
    uint64_t connectAtMs_ = 0;
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_returns_true_once_connected()
{
    Device d;
    d.routerAnswers(1500, 2000);
    d.esp.begin();
    TEST_ASSERT_TRUE(d.esp.waitForConnection(10000));
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
    // Scan after 1.5 s, connected 2 s later, checked every 10 ms.
    TEST_ASSERT_UINT64_WITHIN(20, 3500, d.fakes.clock.nowMs());
}

void test_returns_false_when_the_timeout_passes()
{
    Device d;  // the router never answers
    d.esp.begin();
    TEST_ASSERT_FALSE(d.esp.waitForConnection(5000));
    TEST_ASSERT_EQUAL_UINT64(5000, d.fakes.clock.nowMs());
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, d.esp.state());
}

void test_keeps_connecting_after_a_timeout()
{
    Device d;
    d.esp.begin();
    TEST_ASSERT_FALSE(d.esp.waitForConnection(1000));
    d.fakes.wifi.completeScan({{"Home", -55, 6, true}});
    d.esp.process();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
}

void test_failures_inside_the_timeout_are_retried()
{
    Device d;
    int failures = 0;
    d.esp.onConnectFailed([&](const std::string&, FailureReason) { ++failures; });
    // The first attempt is refused at once, the retry succeeds.
    d.fakes.clock.onDelay = [&]
    {
        if (!d.fakes.wifi.scanComplete())
        {
            d.fakes.wifi.completeScan({{"Home", -55, 6, true}});
        }
        else if (d.fakes.wifi.status() == espmgr::StationStatus::Connecting)
        {
            if (failures == 0)
            {
                d.fakes.wifi.failConnect(FailureReason::WrongPassword);
            }
            else
            {
                d.fakes.wifi.completeConnect();
            }
        }
    };
    d.esp.begin();
    TEST_ASSERT_TRUE(d.esp.waitForConnection(10000));
    TEST_ASSERT_EQUAL(1, failures);
}

void test_returns_false_at_once_without_a_saved_network()
{
    Device d(false);
    d.esp.begin();
    TEST_ASSERT_FALSE(d.esp.waitForConnection(10000));
    TEST_ASSERT_EQUAL(0, d.fakes.clock.delays);
}

void test_returns_false_at_once_before_begin()
{
    Device d;
    TEST_ASSERT_FALSE(d.esp.waitForConnection(10000));
    TEST_ASSERT_EQUAL(0, d.fakes.clock.delays);
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.scansStarted);
}

void test_returns_true_at_once_when_already_connected()
{
    Device d;
    d.routerAnswers(0, 0);
    d.esp.begin();
    TEST_ASSERT_TRUE(d.esp.waitForConnection(10000));
    const int delays = d.fakes.clock.delays;
    TEST_ASSERT_TRUE(d.esp.waitForConnection(10000));
    TEST_ASSERT_EQUAL(delays, d.fakes.clock.delays);
}

void test_listeners_are_called_while_waiting()
{
    Device d;
    d.routerAnswers(100, 100);
    bool connected = false;
    d.esp.onConnected([&](const espmgr::ConnectedInfo&) { connected = true; });
    d.esp.begin();
    TEST_ASSERT_TRUE(d.esp.waitForConnection(10000));
    TEST_ASSERT_TRUE(connected);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_returns_true_once_connected);
    RUN_TEST(test_returns_false_when_the_timeout_passes);
    RUN_TEST(test_keeps_connecting_after_a_timeout);
    RUN_TEST(test_failures_inside_the_timeout_are_retried);
    RUN_TEST(test_returns_false_at_once_without_a_saved_network);
    RUN_TEST(test_returns_false_at_once_before_begin);
    RUN_TEST(test_returns_true_at_once_when_already_connected);
    RUN_TEST(test_listeners_are_called_while_waiting);
    return UNITY_END();
}
