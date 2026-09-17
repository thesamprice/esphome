#ifdef USE_RTEMS

#include "network.h"
#include "esphome/core/log.h"

/*
 * The platform layer is where a stack-specific header is allowed, which is why
 * this is here and not in components/network: that one must not know which
 * stack is underneath it.
 *
 * __has_include rather than an unconditional include, for the same reason as
 * i2c.cpp and uart.cpp -- a BSP without lwip installed is a BSP where this
 * file still has to compile.
 */
#if __has_include(<lwip/netif.h>)
#include <lwip/dhcp.h>
#include <lwip/netif.h>
#define ESPHOME_RTEMS_HAS_LWIP 1
#endif

#if defined(USE_RTEMS_NETWORK) && __has_include(<netstart.h>)
#include <netstart.h>
#define ESPHOME_RTEMS_HAS_NETSTART 1
#endif

namespace esphome::rtems {

static const char *const TAG = "rtems.network";

bool network_is_up() {
#ifdef ESPHOME_RTEMS_HAS_LWIP
  // netif_default is what start_networking() sets. Checking it beats assuming
  // a link is present, which is what a platform with nothing to consult has to
  // do -- and it means a configuration that never brought an interface up says
  // so rather than reporting itself connected.
  return netif_default != nullptr && netif_is_up(netif_default) && netif_is_link_up(netif_default);
#else
  return false;
#endif
}

#ifdef USE_RTEMS_NETWORK

// One interface, and its lifetime is the program's, so it is here rather than
// a member: start_networking() keeps the pointer.
static struct netif s_netif;

void RTEMSNetwork::set_mac_address(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f) {
  this->mac_[0] = a;
  this->mac_[1] = b;
  this->mac_[2] = c;
  this->mac_[3] = d;
  this->mac_[4] = e;
  this->mac_[5] = f;
}

float RTEMSNetwork::get_setup_priority() const { return setup_priority::WIFI; }

void RTEMSNetwork::setup() {
#ifndef ESPHOME_RTEMS_HAS_NETSTART
  ESP_LOGE(TAG, "This BSP has no network stack, so no interface can be brought up");
  this->mark_failed();
#else
  ip_addr_t address, netmask, gateway;

  if (this->use_dhcp_) {
    // start_networking() wants an address to install. Zero means "none yet",
    // which is what DHCP needs before it can ask for one.
    ip_addr_set_zero(&address);
    ip_addr_set_zero(&netmask);
    ip_addr_set_zero(&gateway);
  } else {
    // The octets, spelled out.  These arrive as (a<<24)|(b<<16)|(c<<8)|d, and
    // ip_addr_set_ip4_u32() wants network order, so handing the value over
    // directly gets the address backwards -- 10.0.2.15 becomes 15.2.0.10, and
    // it looks plausible enough in a log to be missed.  IP4_ADDR takes octets
    // and leaves nothing to infer.
    IP4_ADDR(ip_2_ip4(&address), (this->address_ >> 24) & 0xff, (this->address_ >> 16) & 0xff,
             (this->address_ >> 8) & 0xff, this->address_ & 0xff);
    IP4_ADDR(ip_2_ip4(&netmask), (this->netmask_ >> 24) & 0xff, (this->netmask_ >> 16) & 0xff,
             (this->netmask_ >> 8) & 0xff, this->netmask_ & 0xff);
    IP4_ADDR(ip_2_ip4(&gateway), (this->gateway_ >> 24) & 0xff, (this->gateway_ >> 16) & 0xff,
             (this->gateway_ >> 8) & 0xff, this->gateway_ & 0xff);
    IP_SET_TYPE_VAL(address, IPADDR_TYPE_V4);
    IP_SET_TYPE_VAL(netmask, IPADDR_TYPE_V4);
    IP_SET_TYPE_VAL(gateway, IPADDR_TYPE_V4);
  }

  if (start_networking(&s_netif, &address, &netmask, &gateway, this->mac_) != 0) {
    ESP_LOGE(TAG, "Could not bring the interface up");
    this->mark_failed();
    return;
  }

  if (this->use_dhcp_) {
    if (dhcp_start(&s_netif) != ERR_OK) {
      ESP_LOGE(TAG, "Could not start DHCP");
      this->mark_failed();
      return;
    }
    // Not waited for here. The address arrives asynchronously and everything
    // that needs one already waits on network::is_connected(), so blocking
    // setup() for a DHCP round trip would stall the loop for no benefit.
    ESP_LOGI(TAG, "DHCP started");
  }
#endif
}

void RTEMSNetwork::dump_config() {
  ESP_LOGCONFIG(TAG, "Network:");
#ifdef ESPHOME_RTEMS_HAS_LWIP
  if (netif_default != nullptr) {
    ESP_LOGCONFIG(TAG, "  Address: %s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    ESP_LOGCONFIG(TAG, "  Netmask: %s", ip4addr_ntoa(netif_ip4_netmask(netif_default)));
    ESP_LOGCONFIG(TAG, "  Gateway: %s", ip4addr_ntoa(netif_ip4_gw(netif_default)));
  }
#endif
  ESP_LOGCONFIG(TAG, "  DHCP: %s", YESNO(this->use_dhcp_));
}

#endif  // USE_RTEMS_NETWORK

}  // namespace esphome::rtems

#endif  // USE_RTEMS
