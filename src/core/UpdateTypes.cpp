#include "core/UpdateTypes.h"

namespace espmgr
{

const char* toString(UpdateErrorCode code)
{
    switch (code)
    {
    case UpdateErrorCode::None:
        return "none";
    case UpdateErrorCode::ServerUnreachable:
        return "server-unreachable";
    case UpdateErrorCode::HttpStatus:
        return "http-status";
    case UpdateErrorCode::ManifestInvalid:
        return "manifest-invalid";
    case UpdateErrorCode::VersionInvalid:
        return "version-invalid";
    case UpdateErrorCode::TooOld:
        return "too-old";
    case UpdateErrorCode::NoSpace:
        return "no-space";
    case UpdateErrorCode::LowMemory:
        return "low-memory";
    case UpdateErrorCode::NoOtaPartition:
        return "no-ota-partition";
    case UpdateErrorCode::Unsigned:
        return "unsigned";
    case UpdateErrorCode::DownloadFailed:
        return "download-failed";
    case UpdateErrorCode::SizeMismatch:
        return "size-mismatch";
    case UpdateErrorCode::HashMismatch:
        return "hash-mismatch";
    case UpdateErrorCode::SignatureInvalid:
        return "signature-invalid";
    case UpdateErrorCode::WriteFailed:
        return "write-failed";
    case UpdateErrorCode::RolledBack:
        return "rolled-back";
    }
    return "none";
}

const char* toString(UpdateState state)
{
    switch (state)
    {
    case UpdateState::Idle:
        return "idle";
    case UpdateState::Checking:
        return "checking";
    case UpdateState::Available:
        return "available";
    case UpdateState::Downloading:
        return "downloading";
    case UpdateState::Verifying:
        return "verifying";
    case UpdateState::Installing:
        return "installing";
    case UpdateState::Restarting:
        return "restarting";
    }
    return "idle";
}

const char* toString(UpdateOutcome outcome)
{
    switch (outcome)
    {
    case UpdateOutcome::None:
        return "none";
    case UpdateOutcome::Pending:
        return "pending";
    case UpdateOutcome::Succeeded:
        return "succeeded";
    case UpdateOutcome::Failed:
        return "failed";
    case UpdateOutcome::RolledBack:
        return "rolled-back";
    }
    return "none";
}

const char* toString(UpdateMethod method)
{
    return method == UpdateMethod::Automatic ? "automatic" : "manual";
}

}  // namespace espmgr
