#pragma once

// HTTPD reserves three descriptors in addition to accepted client sockets.
// The USB tunnel also owns the client end of its loopback connection. Leave
// one extra descriptor for other network services; counting HTTP clients alone
// can either stop HTTPD at startup or silently break USB connections.
#define LORABLE_HTTP_CLIENT_SOCKETS 5
#define LORABLE_HTTP_INTERNAL_SOCKETS 3
#define LORABLE_USB_CLIENT_SOCKETS 1
#define LORABLE_SPARE_SOCKETS 1
#define LORABLE_MIN_NETWORK_SOCKETS (LORABLE_HTTP_CLIENT_SOCKETS + \
    LORABLE_HTTP_INTERNAL_SOCKETS + LORABLE_USB_CLIENT_SOCKETS + LORABLE_SPARE_SOCKETS)

#if !defined(CONFIG_LWIP_MAX_SOCKETS)
#error "Include sdkconfig.h before portal_socket_budget.h"
#elif CONFIG_LWIP_MAX_SOCKETS < LORABLE_MIN_NETWORK_SOCKETS
#error "Increase CONFIG_LWIP_MAX_SOCKETS: HTTP portal plus USB tunnel need at least 10 sockets"
#endif
