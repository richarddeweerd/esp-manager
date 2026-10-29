#pragma once

#include "core/HttpServer.h"
#include "web/generated/ManagementPage.h"
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

// The same for the Management Interface page.
inline void sendManagementPage(HttpServer& http)
{
    http.send(200, "text/html; charset=utf-8", kManagementPage, kManagementPageSize, true);
}

}  // namespace web
}  // namespace espmgr
