#pragma once

#include "core/HttpServer.h"
#include "web/generated/PortalPage.h"

namespace espmgr
{
namespace web
{

// Answers the current request with the Provisioning Portal page, straight from flash.
inline void sendPortalPage(HttpServer& http)
{
    http.send(200, "text/html; charset=utf-8", kPortalPage, kPortalPageSize, true);
}

}  // namespace web
}  // namespace espmgr
