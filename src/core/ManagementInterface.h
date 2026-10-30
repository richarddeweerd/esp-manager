#pragma once

#include "core/Clock.h"
#include "core/DeviceStatus.h"
#include "core/DigestAuth.h"
#include "core/Logger.h"
#include "core/SavedNetworks.h"
#include "core/Storage.h"
#include "core/WebRoutes.h"
#include "core/WifiConnection.h"

#include <functional>
#include <string>

namespace espmgr
{

// The Management Interface (EP03): web pages on the station's network where the Owner sees the
// device's status and, in later versions, manages its Saved Networks and Settings. Reached
// through esp.management().
//
// It's off unless the Developer enables it, and always password-protected with HTTP Digest
// authentication. The Owner chooses the password in the Provisioning Portal; only its Digest
// hash (HA1) is stored, in its own namespace. Without a stored password the Management
// Interface stays off. The hash includes the user and the realm, so changing either makes the
// stored password useless, and the Owner sets it again through the Provisioning Portal.
//
// It answers while the station is connected, on port 80, and only on the station's address.
//
// The Device tab restarts the device and starts a Network Reset or a Factory Reset (EP09). These
// are POST requests that must also carry the header `X-Requested-With: EspManager`: another
// web site can't add it, so it can't trigger them through the Owner's browser, which may
// remember the password. Each runs a moment after its answer has gone out.
class ManagementInterface
{
public:
    // What the Device tab can do; EspManager fills these in.
    struct Actions
    {
        std::function<void()> restart;
        std::function<void()> networkReset;
        std::function<void()> factoryReset;
    };

    // How long an action waits after its answer.
    static constexpr uint64_t kActionDelayMs = 1000;

    static constexpr const char* kDefaultUser = "admin";
    static constexpr const char* kStorageNamespace = "espmgr-mgmt";

    // `deviceName` gives the default realm, `status` what GET /api/status shows.
    ManagementInterface(WebRoutes& routes, Storage& storage, Clock& clock,
                        WifiConnection& connection, SavedNetworks& networks, Logger& log,
                        std::function<std::string()> deviceName,
                        std::function<DeviceStatus()> status, Actions actions)
        : routes_(routes), storage_(storage), clock_(clock), connection_(connection),
          networks_(networks), log_(log), deviceName_(std::move(deviceName)),
          status_(std::move(status)), actions_(std::move(actions)), auth_(clock)
    {
    }

    ManagementInterface(const ManagementInterface&) = delete;
    ManagementInterface& operator=(const ManagementInterface&) = delete;

    // --- Settings, for the Developer ---

    // Turns the Management Interface on; call it before esp.begin(). The Provisioning Portal
    // then asks the Owner for its password.
    ManagementInterface& enable();
    bool enabled() const
    {
        return enabled_;
    }

    // The user name for logging in; "admin" by default. 1 to 32 characters, without '"', '\'
    // or ':'; returns false, changing nothing, otherwise.
    bool setUser(const std::string& user);
    std::string user() const
    {
        return user_;
    }

    // The realm the browser shows when it asks for the password; the hostname by default.
    // At most 64 characters, without '"' or '\'; returns false, changing nothing, otherwise.
    bool setRealm(const std::string& realm);
    std::string realm() const;

    // Stores a new password, for an Owner who enters it some other way than the Provisioning
    // Portal (over serial or Bluetooth, say). Never hard-code a password in the firmware: every
    // device would share it. Returns false if it isn't valid or couldn't be stored. Call it
    // after setUser() and setRealm(), since the stored hash depends on both.
    bool setPassword(const std::string& password);
    // True once a password is stored for the current user and realm.
    bool hasPassword() const;

    // 8 to 63 printable ASCII characters.
    static bool isValidPassword(const std::string& password);

    // --- Driven by EspManager ---

    void begin();
    void process();

    // True while it answers requests.
    bool active() const
    {
        return routes_.active(WebRoutes::Site::Management);
    }

private:
    void registerRoutes();
    // True if `request` carries the Owner's credentials. Otherwise answers it with the 401
    // challenge itself.
    bool authorize(const HttpRequest& request);
    void sendNetworks();
    // POST /api/restart, /api/network-reset and /api/factory-reset.
    void registerAction(const char* path, const char* name,
                        std::function<void()> Actions::* action);
    std::string storedId() const;

    WebRoutes& routes_;
    Storage& storage_;
    Clock& clock_;
    WifiConnection& connection_;
    SavedNetworks& networks_;
    Logger& log_;
    std::function<std::string()> deviceName_;
    std::function<DeviceStatus()> status_;
    Actions actions_;
    DigestAuth auth_;

    bool enabled_ = false;
    bool begun_ = false;
    std::string user_ = kDefaultUser;
    std::string realm_;  // empty: the hostname
    // The stored hash, if it belongs to the current user and realm; empty otherwise.
    std::string ha1_;
    // The action asked for, run at actionAtMs_.
    std::function<void()> pending_;
    uint64_t actionAtMs_ = 0;
};

}  // namespace espmgr
