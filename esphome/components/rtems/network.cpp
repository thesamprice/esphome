#ifdef USE_RTEMS

#include "network.h"

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
#include <lwip/netif.h>
#define ESPHOME_RTEMS_HAS_LWIP 1
#endif

namespace esphome::rtems {

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

}  // namespace esphome::rtems

#endif  // USE_RTEMS
