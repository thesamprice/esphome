#include "esphome/core/defines.h"
#if defined(USE_RTEMS) && defined(USE_MDNS)

#include "esphome/components/network/ip_address.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "mdns_component.h"

namespace esphome::mdns {

static const char *const TAG = "mdns";

void MDNSComponent::setup() {
#ifdef USE_MDNS_STORE_SERVICES
#ifdef USE_MDNS_DEVICE_INFO_TXT
  get_mac_address_into_buffer(this->mac_address_);
  char *mac_ptr = this->mac_address_;
  format_hex_to(this->config_hash_str_, App.get_config_hash());
  char *cfg_ptr = this->config_hash_str_;
#else
  char *mac_ptr = nullptr;
  char *cfg_ptr = nullptr;
#endif
  this->compile_records_(this->services_, mac_ptr, cfg_ptr);
#endif

  /*
   * The records are compiled and nothing announces them.
   *
   * rtems-lwip carries a responder -- the sources are imported on a branch --
   * and turning it on is not a matter of configuration: joining a multicast
   * group goes through the Xilinx driver's MAC filter update, which hangs and,
   * once that is bounded, leaves the interface unable to transmit at all.
   * rtems-esphome#63 and #64 have the detail.
   *
   * Said out loud rather than left silent, because a node that is simply not
   * discoverable looks like a network problem from the other end, and whoever
   * meets that deserves to be pointed at the reason rather than at tcpdump.
   */
  ESP_LOGW(TAG, "mDNS is not implemented on RTEMS; this node will not be discoverable");
  ESP_LOGW(TAG, "  reach it by address instead");
}

void MDNSComponent::on_shutdown() {}

}  // namespace esphome::mdns

#endif
