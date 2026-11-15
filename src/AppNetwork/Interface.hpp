
#ifndef APPNETWORK_INTERFACE
#define APPNETWORK_INTERFACE

#include <string>

#include "esp_err.h"
#include "tcpip_adapter.h"

namespace zw::esp8266::app::network {

extern ip_addr_t StationIPAddr(void);
extern const std::string& StationMAC(void);
extern const std::string& Hostname(void);
extern std::string DefaultHostname(const std::string& prefix = "");

extern esp_err_t RefreshHostname(void);
extern void ApplyStationConfig(void);

}  // namespace zw::esp8266::app::network

#endif  // APPNETWORK_INTERFACE
