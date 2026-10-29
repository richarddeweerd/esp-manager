#include "core/NetworkScanner.h"

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
    finishedAtMs_ = clock_.nowMs();
}

}  // namespace espmgr
