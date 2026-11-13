// Firmware updates: the device checks the Update Server once a day, the Owner sees the update in
// the Management Interface's Updates tab, and installs it there; the sketch prints the progress.
//
// Needs two app slots of about 1.9 MB, because the HTTPS client doesn't fit the default ones:
//   PlatformIO:  board_build.partitions = min_spiffs.csv
//   Arduino IDE: Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"
//
// Make a Signing Key once with `scripts/sign-firmware.py new-key signing-key.pem` and put the
// Public Key it prints below. For each release, raise kFirmwareVersion, build, then sign the image
// and write its Manifest:
//   scripts/sign-firmware.py sign --key signing-key.pem --version 1.1.0 firmware.bin
// and put stable.json and firmware.bin in the server folder.

#include <Arduino.h>
#include <EspManager.h>

namespace
{

EspManager esp;

constexpr const char* kFirmwareVersion = "1.0.0";

// Replace this with your own Public Key. Images that aren't signed with its Signing Key are
// refused.
constexpr const char* kPublicKey = R"(-----BEGIN PUBLIC KEY-----
REPLACE WITH THE PUBLIC KEY FROM scripts/sign-firmware.py new-key
-----END PUBLIC KEY-----
)";

}  // namespace

void setup()
{
    Serial.begin(115200);

    esp.setFirmwareVersion(kFirmwareVersion);
    esp.updates().setServer("https://updates.example.com/my-device/");  // <server>stable.json
    esp.updates()
        .setPublicKey(kPublicKey)
        .setAutoCheck(true)  // about a minute after connecting, then every 24 hours
        .ownerCanEdit()      // the Owner may change the checks, the policy and the channel
        .editableInManagement();
    esp.management().enable();  // the Updates tab; a password is set in the Provisioning Portal

    esp.onUpdateAvailable([](const espmgr::UpdateInfo& info)
                          { Serial.printf("Version %s is available.\n", info.version.c_str()); });
    esp.onUpdateProgress(
        [](const espmgr::UpdateProgress& progress)
        { Serial.printf("Downloading: %u%%\n", static_cast<unsigned>(progress.percent)); });
    esp.onUpdateCompleted([](const espmgr::UpdateInfo& info)
                          { Serial.printf("Installed %s, restarting.\n", info.version.c_str()); });
    esp.onUpdateFailed([](const espmgr::UpdateError& error)
                       { Serial.printf("Update failed: %s\n", espmgr::toString(error.code)); });

    esp.begin();

    const espmgr::UpdateRecord& last = esp.updates().lastResult();
    if (last.outcome != espmgr::UpdateOutcome::None)
    {
        Serial.printf("Last update to %s: %s\n", last.toVersion.c_str(),
                      espmgr::toString(last.outcome));
    }
}

void loop()
{
    esp.process();
}
