#include "esphome/core/defines.h"
#if defined(USE_RTEMS) && defined(USE_MDNS)

#include "esphome/components/network/ip_address.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "mdns_component.h"

#include <memory>
#include <string>
#include <vector>

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

/// The TXT records for one service, owned by us.
///
/// lwIP asks for TXT records through a callback at announce time, which is
/// long after setup returns -- and the list it was given does not survive that
/// long.  When USE_MDNS_STORE_SERVICES is off, setup_buffers_and_register_()
/// builds the services vector, the MAC buffer and the config-hash buffer as
/// stack locals, so every pointer in them dangles the moment it returns.
/// Pointing lwIP at them faulted the lwIP thread inside strlen() during the
/// first announcement, which took the whole stack down and presented as "mDNS
/// never announces".
///
/// So copy what we will be asked for.  Held in unique_ptrs because lwIP keeps
/// the address we hand it and a vector that reallocates would move them.
struct OwnedService {
  std::vector<std::string> txt;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::vector<std::unique_ptr<OwnedService>> owned_services;

static void add_txt_records(struct mdns_service *service, void *userdata) {
  const auto *owned = static_cast<const OwnedService *>(userdata);
  for (const auto &item : owned->txt) {
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

    // Copy the TXT records now, while the list we were handed is still alive.
    auto owned = std::make_unique<OwnedService>();
    for (const auto &record : service.txt_records) {
      const char *key = MDNS_STR_ARG(record.key);
      const char *value = MDNS_STR_ARG(record.value);
      // A record with no key is nothing to send.  A record with no value is a
      // real thing: RFC 6763 section 6.4 allows "key=" with nothing after it.
      if (key == nullptr) {
        continue;
      }
      std::string item(key);
      item += "=";
      if (value != nullptr) {
        item += value;
      }
      owned->txt.push_back(std::move(item));
    }
    owned_services.push_back(std::move(owned));

    int8_t slot = mdns_resp_add_service(netif, hostname, service_type, proto, service.port.value(),
                                        add_txt_records, owned_services.back().get());
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
