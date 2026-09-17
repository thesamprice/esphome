#pragma once

#ifdef USE_RTEMS

#include <cstdint>

#include "esphome/core/component.h"

namespace esphome::rtems {

/// Whether the default network interface is up.
///
/// Reports what the stack says rather than what ESPHome arranged, because on
/// this platform they can differ: an application may bring an interface up
/// without RTEMSNetwork, and a configuration without RTEMSNetwork has none.
///
/// A BSP with no network stack has nothing to report and answers false.
bool network_is_up();

#ifdef USE_RTEMS_NETWORK

/// Brings up the BSP's network interface.
///
/// This is not `ethernet:`. That component configures a MAC and an external
/// PHY over SMI, and none of that is reachable here: which controller a BSP
/// has, and how it is wired, is decided when the BSP is built. What is left
/// for a configuration to say is the address, so that is all this takes.
///
/// It is a component rather than a call in `on_boot` because the API server
/// and everything else that waits on a link check it during setup, before any
/// boot automation runs.
class RTEMSNetwork : public Component {
 public:
  void setup() override;
  void dump_config() override;
  /// Before anything that waits for a link, which is what setup_priority::WIFI
  /// means on the platforms that have one.
  float get_setup_priority() const override;

  void set_use_dhcp(bool use_dhcp) { this->use_dhcp_ = use_dhcp; }
  void set_static_ip(uint32_t address, uint32_t netmask, uint32_t gateway) {
    this->address_ = address;
    this->netmask_ = netmask;
    this->gateway_ = gateway;
  }
  /// Six arguments rather than an array, because that is what codegen can
  /// pass: a brace-enclosed initializer does not convert to a pointer.
  void set_mac_address(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f);

 protected:
  uint32_t address_{0};
  uint32_t netmask_{0};
  uint32_t gateway_{0};
  uint8_t mac_[6]{};
  bool use_dhcp_{true};
};

#endif  // USE_RTEMS_NETWORK

}  // namespace esphome::rtems

#endif  // USE_RTEMS
