#include "esphome/core/defines.h"

#ifdef USE_RTEMS

#include "preferences.h"

#include "esphome/core/preferences.h"
#include "esphome/core/log.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome::rtems {

static const char *const TAG = "rtems.preferences";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
RTEMSPreferences *rtems_preferences;

ESPPreferenceObject RTEMSPreferences::make_preference(size_t length, uint32_t type, bool in_flash) {
  // in_flash is ignored: there is no flash to put anything in on this platform,
  // and pretending otherwise would only move the surprise later.
  (void) length;
  (void) in_flash;
  return ESPPreferenceObject(new RTEMSPreferenceBackend(type));
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static RTEMSPreferences s_preferences;

RTEMSPreferences *get_preferences() { return &s_preferences; }

void setup_preferences() {
  rtems_preferences = &s_preferences;
  global_preferences = &s_preferences;
#ifdef USE_RTEMS_PREFERENCES_PATH
  s_preferences.set_path(USE_RTEMS_PREFERENCES_PATH);
#endif
  s_preferences.load_store();
}

/*
 * The on-disk form.
 *
 * A header, then one record per key.  Rewritten whole rather than updated in
 * place: the whole store is a few hundred bytes, and a scheme that edits
 * records in place has to answer what happens when it stops halfway, which
 * this one does not have to.
 *
 * What this does and does not promise, since #16 asks for it to be said:
 *
 *   It does     survive a clean reboot, if the filesystem does.
 *   It does     reject a truncated, short, or corrupt file, whole, and start
 *               empty rather than load half of it.
 *   It does NOT survive power loss during the write.  The temporary file and
 *               rename below narrow that window to the rename itself, which is
 *               as far as POSIX takes it; on a filesystem where rename is not
 *               atomic it does not even do that.
 *   It does NOT do wear levelling, which is NVS's other job.  On a filesystem
 *               over raw flash, every sync() rewrites the same blocks.
 *
 * So this is right for CI and for a board with an SD card, and is not yet the
 * thing to put on a device that loses power in the field.
 */
namespace {

constexpr uint32_t STORE_MAGIC = 0x52545053;  // "RTPS"
constexpr uint32_t STORE_VERSION = 1;

struct StoreHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t count;
  uint32_t checksum;  // over every record byte that follows
};

struct RecordHeader {
  uint32_t key;
  uint32_t len;
};

uint32_t checksum_of(const std::map<uint32_t, std::vector<uint8_t>> &data) {
  // FNV-1a over key and value of every record, in the order they are written.
  uint32_t hash = 0x811c9dc5;
  auto feed = [&hash](const uint8_t *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
      hash ^= bytes[i];
      hash *= 0x01000193;
    }
  };
  for (const auto &entry : data) {
    feed(reinterpret_cast<const uint8_t *>(&entry.first), sizeof(entry.first));
    const uint32_t len = static_cast<uint32_t>(entry.second.size());
    feed(reinterpret_cast<const uint8_t *>(&len), sizeof(len));
    feed(entry.second.data(), entry.second.size());
  }
  return hash;
}

}  // namespace

void RTEMSPreferences::load_store() {
  if (this->path_ == nullptr) {
    return;  // nowhere configured to keep it; stay in memory
  }

  FILE *f = ::fopen(this->path_, "rb");
  if (f == nullptr) {
    // No store yet is the ordinary first boot, not an error.
    return;
  }

  std::map<uint32_t, std::vector<uint8_t>> loaded;
  StoreHeader header{};
  bool ok = ::fread(&header, sizeof(header), 1, f) == 1 && header.magic == STORE_MAGIC &&
            header.version == STORE_VERSION;

  for (uint32_t i = 0; ok && i < header.count; i++) {
    RecordHeader record{};
    if (::fread(&record, sizeof(record), 1, f) != 1 || record.len > 255) {
      ok = false;
      break;
    }
    std::vector<uint8_t> value(record.len);
    if (record.len != 0 && ::fread(value.data(), 1, record.len, f) != record.len) {
      ok = false;
      break;
    }
    loaded.emplace(record.key, std::move(value));
  }

  if (ok && checksum_of(loaded) != header.checksum) {
    ok = false;
  }

  ::fclose(f);

  if (!ok) {
    // Discard the lot, in memory as well as on disk.  A store that half-loads
    // is worse than one that starts empty: the caller cannot tell which of its
    // keys survived.  Clearing here also makes this total -- after
    // load_store() the in-memory state is what the file says, whatever it
    // said -- which is what makes it testable without a reboot.
    ESP_LOGW(TAG, "Ignoring a preferences store that did not read back cleanly: %s", this->path_);
    this->data_.clear();
    this->dirty_ = false;
    return;
  }

  this->data_ = std::move(loaded);
  this->dirty_ = false;
  ESP_LOGD(TAG, "Loaded %u preferences from %s", (unsigned) this->data_.size(), this->path_);
}

bool RTEMSPreferences::write_store_() {
  // Write beside the real file and rename over it, so a reader never sees a
  // half-written store -- only the old one or the new one.
  std::string tmp = std::string(this->path_) + ".new";

  FILE *f = ::fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    ESP_LOGW(TAG, "Cannot write %s: %s", tmp.c_str(), ::strerror(errno));
    return false;
  }

  StoreHeader header{STORE_MAGIC, STORE_VERSION, static_cast<uint32_t>(this->data_.size()),
                     checksum_of(this->data_)};
  bool ok = ::fwrite(&header, sizeof(header), 1, f) == 1;

  for (const auto &entry : this->data_) {
    if (!ok) {
      break;
    }
    RecordHeader record{entry.first, static_cast<uint32_t>(entry.second.size())};
    ok = ::fwrite(&record, sizeof(record), 1, f) == 1;
    if (ok && !entry.second.empty()) {
      ok = ::fwrite(entry.second.data(), 1, entry.second.size(), f) == entry.second.size();
    }
  }

  if (ok) {
    ok = ::fflush(f) == 0;
  }
  ::fclose(f);

  if (!ok) {
    ::remove(tmp.c_str());
    return false;
  }

  if (::rename(tmp.c_str(), this->path_) == 0) {
    return true;
  }

  /*
   * RTEMS's rename() does not replace an existing destination: _rename_r()
   * evaluates the new path with RTEMS_FS_EXCLUSIVE, so it fails with EEXIST
   * whatever the filesystem underneath.  POSIX says it should replace, and
   * atomically, which is the whole reason for writing beside and renaming --
   * so unlink and retry, and accept that on this platform there is a moment
   * with no store at all.
   *
   * That moment is the honest limit of what this can promise here.  A crash
   * inside it loses the preferences rather than keeping the previous ones,
   * which is worse than a POSIX rename and better than a partial file, and it
   * is why the class does not claim power-loss safety.
   */
  if (errno == EEXIST && ::remove(this->path_) == 0 && ::rename(tmp.c_str(), this->path_) == 0) {
    return true;
  }

  ESP_LOGW(TAG, "Cannot replace %s: %s", this->path_, ::strerror(errno));
  ::remove(tmp.c_str());
  return false;
}

bool RTEMSPreferences::sync() {
  if (this->path_ == nullptr || !this->dirty_) {
    return true;
  }
  if (!this->write_store_()) {
    return false;
  }
  this->dirty_ = false;
  return true;
}

bool RTEMSPreferences::reset() {
  this->data_.clear();
  this->dirty_ = false;
  if (this->path_ != nullptr) {
    // Remove it rather than writing an empty one: reset means factory state,
    // and a store that exists but is empty is a different thing from none.
    ::remove(this->path_);
  }
  return true;
}

bool RTEMSPreferenceBackend::save(const uint8_t *data, size_t len) const {
  return get_preferences()->save(this->key_, data, len);
}

bool RTEMSPreferenceBackend::load(uint8_t *data, size_t len) const {
  return get_preferences()->load(this->key_, data, len);
}

}  // namespace esphome::rtems

namespace esphome {

// The definition, not just the assignment in setup_preferences(). Every
// platform backend owns this: core/preferences.h only declares it extern.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
ESPPreferences *global_preferences;

}  // namespace esphome

#endif  // USE_RTEMS
