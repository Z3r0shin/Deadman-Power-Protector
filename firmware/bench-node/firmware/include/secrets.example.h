// Copy to secrets.h and fill in. secrets.h is never committed.
#pragma once
#define PORTAL_USER "admin"
#define PORTAL_PASS ""              // also the node's own access point password: 8 characters minimum
#define SHELLY_HOST ""              // the Shelly's reserved IP address
// House WiFi used when none is stored in NVS (the portal's System tab stores one and overrides this).
#define WIFI_SSID ""
#define WIFI_PASS ""
// Network name: http://<MDNS_NAME>.local/ (also the DHCP host name). Left empty: bench-node.
#define MDNS_NAME ""
