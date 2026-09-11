#include "esphome/core/defines.h"
#if defined(USE_RTEMS) && defined(USE_MDNS)

#include "esphome/components/network/ip_address.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "mdns_component.h"

/*
 * rtems-lwip carries lwIP's own responder.  Whether it is compiled in is a
 * property of how the library was built -- liblwip.a is built once and
 * installed, so an application cannot turn LWIP_MDNS_RESPONDER on -- which is
 * why this asks the headers rather than assuming.
 */
#if __has_include(<lwip/apps/mdns.h>)
#include <lwip/apps/mdns.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#endif

namespace esphome::mdns {

static const char *const TAG = "mdns";

#if defined(LWIP_MDNS_RESPONDER) && LWIP_MDNS_RESPONDER

/// Hand one service's TXT records to lwIP.
///
/// lwIP asks for them through a callback rather than taking them up front, so
/// the service outlives this call: the pointer is into MDNSComponent's own
/// services_, which lives as long as the component.
static void add_txt_records(struct mdns_service *service, void *userdata) {
  const auto *entry = static_cast<const MDNSService *>(userdata);
  for (const auto &record : entry->txt_records) {
    // "key=value", which is the form RFC 6763 section 6.3 puts on the wire and
    // what lwIP expects to be handed.
    std::string item = std::string(MDNS_STR_ARG(record.key)) + "=" + MDNS_STR_ARG(record.value);
    err_t err = mdns_resp_add_service_txtitem(service, item.c_str(), item.size());
    if (err != ERR_OK) {
      ESP_LOGW(TAG, "Could not add TXT record %s: %d", item.c_str(), (int) err);
    }
  }
}

/// Advertise everything the configuration asked for.
///
/// Called through setup_buffers_and_register_(), which is where the shared
/// component keeps the buffer and record-compilation dance -- doing it here
/// instead would duplicate it and would reach for services_, which only exists
/// under USE_MDNS_STORE_SERVICES.
static void register_rtems(MDNSComponent *comp, StaticVector<MDNSService, MDNS_SERVICE_COUNT> &services) {
  (void) comp;

  // Which interface, rather than all of them: this platform brings up exactly
  // one, and lwIP's responder is per-netif.
  struct netif *netif = netif_default;
  if (netif == nullptr) {
    ESP_LOGW(TAG, "No network interface is up; this node will not be discoverable");
    return;
  }

  const char *hostname = App.get_name().c_str();

  // The responder asserts LWIP_ASSERT_CORE_LOCKED() on every entry point.
  LwIPLock lock;

  mdns_resp_init();

  err_t err = mdns_resp_add_netif(netif, hostname);
  if (err != ERR_OK) {
    ESP_LOGE(TAG, "Cannot advertise on this interface: %d", (int) err);
    return;
  }

  for (auto &service : services) {
    const char *service_type = MDNS_STR_ARG(service.service_type);
    const char *proto_str = MDNS_STR_ARG(service.proto);

    // lwIP takes the protocol as an enum and the service type as a string that
    // keeps its leading underscore, which is the form ESPHome already stores.
    enum mdns_sd_proto proto = DNSSD_PROTO_TCP;
    if (proto_str != nullptr && strstr(proto_str, "udp") != nullptr) {
      proto = DNSSD_PROTO_UDP;
    }

    int8_t slot = mdns_resp_add_service(netif, hostname, service_type, proto, service.port.value(),
                                        add_txt_records, &service);
    if (slot < 0) {
      // MDNS_MAX_SERVICES is a compile-time limit in the library, so this is a
      // configuration that asks for more than it was built for rather than a
      // transient failure -- worth naming the service that did not fit.
      ESP_LOGW(TAG, "Could not advertise %s.%s: %d", service_type, proto_str, (int) slot);
    }
  }

  mdns_resp_announce(netif);
  ESP_LOGD(TAG, "Advertising %s with %u service(s)", hostname, (unsigned) services.size());
}

void MDNSComponent::setup() { this->setup_buffers_and_register_(register_rtems); }

void MDNSComponent::on_shutdown() {
  struct netif *netif = netif_default;
  if (netif == nullptr) {
    return;
  }
  LwIPLock lock;
  mdns_resp_remove_netif(netif);
}

#else  // no responder in the installed lwIP

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
   * Said out loud rather than left silent: a node that is simply not
   * discoverable looks like a network problem from the other end, and whoever
   * meets that deserves to be pointed at the reason rather than at tcpdump.
   */
  ESP_LOGW(TAG, "This lwIP was built without LWIP_MDNS_RESPONDER; this node will not be discoverable");
  ESP_LOGW(TAG, "  reach it by address instead");
}

void MDNSComponent::on_shutdown() {}

#endif

}  // namespace esphome::mdns

#endif
