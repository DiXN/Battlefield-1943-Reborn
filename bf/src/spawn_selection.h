// Host-side planning for a mouse spawn request. No guest addresses or timing
// policy.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace bf1943::spawn_selection {

struct Entry {
  std::string name;
  int id = -1;
  bool eligible = false;
  bool excluded = true;
};

struct Plan {
  int filtered_index;
  int point_id;
  int flag_mode;
};

// Match the same filtering/order as igm_mp_cq.setSpawnInfo, without inferring
// IDs from entity order, team ownership, letters, or the currently selected ID.
inline std::optional<Plan> PlanSelection(std::span<const Entry> entries,
                                         std::string_view name,
                                         std::span<const std::string> modes) {
  if (name.empty() || entries.empty() || entries.size() > 64 ||
      modes.size() != 2)
    return {};
  int flag = -1;
  for (size_t i = 0; i < modes.size(); ++i) {
    if (modes[i] == "flag") {
      if (flag != -1)
        return {};
      flag = int(i);
    } else if (modes[i] != "squad" && modes[i] != "frontline") {
      return {};
    }
  }
  if (flag < 0)
    return {};
  int filtered = 0;
  std::optional<Plan> found;
  for (const Entry &entry : entries) {
    if (!entry.eligible || entry.excluded)
      continue;
    if (entry.id < 0 || entry.id >= 255 || entry.name.empty())
      return {};
    if (entry.name == name) {
      if (found)
        return {}; // ambiguous names must not select an arbitrary entry
      found = Plan{filtered, entry.id, flag};
    }
    ++filtered;
  }
  return found;
}

struct Request {
  std::string name;
  uint32_t entity = 0, screen = 0;
  uint64_t generation = 0;
};

// The caller owns synchronization. Requests are coalesced, consumed once, and
// invalidated on a screen/focus/lifecycle change. There is no retry timer.
class Requests {
public:
  void Observe(uint32_t screen, bool allowed) {
    const uint32_t next = allowed ? screen : 0;
    if (screen_ != next) {
      Cancel();
      screen_ = next;
    }
    if (!allowed)
      Cancel();
  }
  bool Queue(std::string name, uint32_t entity) {
    if (!screen_ || name.empty() || !entity)
      return false;
    ++generation_;
    pending_ = Request{std::move(name), entity, screen_, generation_};
    return true;
  }
  void Cancel() {
    ++generation_;
    pending_.reset();
  }
  std::optional<Request> Take() { return std::exchange(pending_, {}); }
  bool Current(const Request &request) const {
    return screen_ && request.screen == screen_ &&
           request.generation == generation_;
  }

private:
  uint32_t screen_ = 0;
  uint64_t generation_ = 0;
  std::optional<Request> pending_;
};

} // namespace bf1943::spawn_selection
