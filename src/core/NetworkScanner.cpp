#include "core/NetworkScanner.h"

#include <algorithm>

namespace espmgr
{

bool NetworkScanner::start()
{
    if (busy_)
    {
        return true;
    }
    if (!wifi_.startScan())
    {
        log_.debug("scan", "scan didn't start");
        return false;
    }
    busy_ = true;
    startedAtMs_ = clock_.nowMs();
    return true;
}

void NetworkScanner::process()
{
    if (!busy_)
    {
        return;
    }
    if (wifi_.scanComplete())
    {
        results_ = wifi_.scanResults();
        log_.debug("scan", "found %u networks", static_cast<unsigned>(results_.size()));
    }
    else if (clock_.nowMs() - startedAtMs_ >= kScanTimeoutMs)
    {
        results_.clear();
        log_.warn("scan", "scan timed out");
    }
    else
    {
        return;
    }
    busy_ = false;
    hasFinished_ = true;
    finishedAtMs_ = clock_.nowMs();
}

std::vector<ScanResult> NetworkScanner::networkList(const std::vector<ScanResult>& results,
                                                    int8_t minSignalDbm)
{
    std::vector<ScanResult> list;
    for (const ScanResult& result : results)
    {
        if (result.ssid.empty() || result.rssi < minSignalDbm)
        {
            continue;
        }
        auto same = std::find_if(list.begin(), list.end(), [&result](const ScanResult& listed)
                                 { return listed.ssid == result.ssid; });
        if (same == list.end())
        {
            list.push_back(result);
        }
        else if (result.rssi > same->rssi)
        {
            *same = result;
        }
    }
    std::stable_sort(list.begin(), list.end(),
                     [](const ScanResult& a, const ScanResult& b) { return a.rssi > b.rssi; });
    return list;
}

}  // namespace espmgr
