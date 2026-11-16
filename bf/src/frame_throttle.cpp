#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>

REXCVAR_DEFINE_BOOL(bf_two_frames_in_flight, false, "BF1943/Performance",
                    "Direct3D's frame throttle waits for the frame before the last presented one, so the game prepares "
                    "the next frame while the renderer draws the current one (one more frame of latency)");

REX_EXTERN(__imp__sub_821659B0);
REX_HOOK_RAW(sub_821659B0) {
  const uint32_t lr = uint32_t(ctx.lr);
  if ((lr == 0x82164E8C || lr == 0x82160F5C) && REXCVAR_GET(bf_two_frames_in_flight)) {
    static uint32_t last = 0, older = 0;
    if (ctx.r4.u32 != last) older = last, last = ctx.r4.u32;
    if (!older) return;
    ctx.r4.u64 = older;
  }
  __imp__sub_821659B0(ctx, base);
}
