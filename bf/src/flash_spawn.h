// Direct deployment-map mouse selection; independent of native pad handling.
#pragma once

#include <cstdint>
#include <rex/ppc/context.h>
#include <string>
#include <vector>

namespace bf1943 {
struct SpawnMouseMarker {
  std::string name;
  uint32_t entity = 0;
  float x = 0, y = 0; // actual movie marker origin in 1280x720 stage units
};
std::vector<SpawnMouseMarker> SpawnMouseMarkers();
void SpawnSelectionFocus(bool focused);
bool QueueSpawnSelection(const std::string &point_name,
                         uint32_t capture_entity);
void CancelSpawnSelection();
void ProcessSpawnSelection(PPCContext &ctx, uint8_t *base, uint32_t ui,
                           bool allowed, bool navigation);
} // namespace bf1943
