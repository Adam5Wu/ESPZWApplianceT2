
#ifndef APPNETWORK_INTERFACE_PRIVATE
#define APPNETWORK_INTERFACE_PRIVATE

#include "tcpip_adapter.h"

#include "AppConfig/Interface.hpp"

namespace zw::esp8266::app::network {

struct InternalStates {
  union {
    uint32_t flag_data;
    struct {
      unsigned PROVISION : 1;
    };
  };
  std::string StationMAC;
  std::string APName;
  std::string Hostname;
  ip_addr_t StationIPAddr;
};

extern const InternalStates& internal_states(void);

}  // namespace zw::esp8266::app::network

#endif  // APPNETWORK_INTERFACE_PRIVATE
