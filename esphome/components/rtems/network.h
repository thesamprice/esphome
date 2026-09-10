#pragma once

#ifdef USE_RTEMS

namespace esphome::rtems {

/// Whether the default network interface is up.
///
/// Nothing in ESPHome brings an interface up on this platform yet -- there is
/// no `wifi:` or `ethernet:` for it, and the application does it -- so this
/// reports what the stack says rather than what ESPHome arranged.
///
/// A BSP with no network stack has nothing to report and answers false.
bool network_is_up();

}  // namespace esphome::rtems

#endif  // USE_RTEMS
