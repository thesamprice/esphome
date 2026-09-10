#pragma once
#ifdef USE_RTEMS

#include "esphome/core/preference_backend.h"

#include <cstring>
#include <map>
#include <vector>

namespace esphome::rtems {

/// Preferences in a map, optionally written through to a file.
///
/// With no `preferences_path:` the store is RAM for the lifetime of one boot,
/// which is all a BSP that configures no filesystem can offer. Given a path,
/// every sync() rewrites the whole file, and load_store() at startup reads it
/// back -- so preferences survive a restart exactly as far as the filesystem
/// under that path does.
///
/// On a board whose filesystem is a RAM disk that is still one boot. Saying
/// which of the two a given configuration has is the point of the path being
/// explicit rather than defaulted.
class RTEMSPreferences final : public PreferencesMixin<RTEMSPreferences> {
 public:
  using PreferencesMixin<RTEMSPreferences>::make_preference;

  /// Write the store out.  Everything above this keeps working in RAM whether
  /// or not the write lands, which is deliberate: a node that cannot persist
  /// should still run.
  bool sync();

  /// Forget everything, on disk as well as in memory.  ESPHome calls this to
  /// mean "factory reset", so leaving the file behind would be wrong.
  bool reset();

  /// Replace the in-memory state with what the store file says -- whatever it
  /// says.  A file that does not read back cleanly leaves it empty rather than
  /// partly loaded.
  void load_store();

  /// Where the store lives.  Unset by default, because which filesystem is
  /// mounted and where is a property of the board rather than of this class.
  void set_path(const char *path) { this->path_ = path; }

  ESPPreferenceObject make_preference(size_t length, uint32_t type, bool in_flash);
  ESPPreferenceObject make_preference(size_t length, uint32_t type) { return make_preference(length, type, false); }

  bool save(uint32_t key, const uint8_t *data, size_t len) {
    if (len > 255)
      return false;
    this->data_[key] = std::vector<uint8_t>(data, data + len);
    this->dirty_ = true;
    return true;
  }

  /// One-shot read of a stored preference by key, without allocating a backend
  bool load_from_key(uint32_t type, uint8_t *data, size_t len) { return this->load(type, data, len); }

  bool load(uint32_t key, uint8_t *data, size_t len) {
    if (len > 255)
      return false;
    auto it = this->data_.find(key);
    if (it == this->data_.end())
      return false;
    const auto &vec = it->second;
    // A length mismatch means the stored value is not the thing being asked
    // for -- a changed struct, or a key collision -- so refuse rather than
    // hand back a partial read.
    if (vec.size() != len)
      return false;
    std::memcpy(data, vec.data(), len);
    return true;
  }

 protected:
  bool write_store_();

  std::map<uint32_t, std::vector<uint8_t>> data_{};
  const char *path_{nullptr};
  /// Set when the in-memory state has changed since the last successful
  /// write, so a sync() with nothing to do costs nothing.
  bool dirty_{false};
};

RTEMSPreferences *get_preferences();
void setup_preferences();
extern RTEMSPreferences *rtems_preferences;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace esphome::rtems

DECLARE_PREFERENCE_ALIASES(esphome::rtems::RTEMSPreferences)

#endif  // USE_RTEMS
