// The game's own code where it crashes on our servers. The Xbox 360 game's dedicated server code never ran in
// production (DICE's servers were PC builds), so rare paths can still fault here.
//
// sub_8275B690(owner, filter, out): a query over the owner's list of object pointers (owner +32 to +36): for each
// object whose type (the word at +8) is in the filter's bit set, its method (vtable slot 2) adds it to `out`. Both
// callers pass an empty list of their own to fill. Official server 1 (Classic: All Maps 1, 7 players, Guadalcanal)
// crashed there on 11 Nov 2026 at 07:21 server time: an empty slot (0) in the owner's list, and the type read went to
// guest 0x8 ("Unhandled guest access violation: read of guest 0x00000008"; the crash report's frames are this function,
// then sub_82762EC8, sub_82763CB8, sub_82764298, ... in the server's update). The game's code below is the generated
// code exactly (bf/generated/default, bf1943_recomp.144.cpp), with one change: an empty slot adds nothing and is
// passed over, as a removed object would be.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "bf1943_pch.h"

#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REX_EXTERN(__savegprlr_27);
REX_EXTERN(__restgprlr_27);

namespace {
std::atomic<uint32_t> g_empty_slots{0};
}  // namespace

extern "C" REX_FUNC(sub_8275B690) {
  REX_FUNC_PROLOGUE();
  uint32_t ea{};
  // mflr r12
  ctx.r12.u64 = ctx.lr;
  // bl 0x827be834
  ctx.lr = 0x8275B698;
  __savegprlr_27(ctx, base);
  // stwu r1,-128(r1)
  ea = -128 + ctx.r1.u32;
  REX_STORE_U32(ea, ctx.r1.u32);
  ctx.r1.u32 = ea;
  // lwz r11,0(r4)
  ctx.r11.u64 = REX_LOAD_U32(ctx.r4.u32 + 0);
  // mr r30,r4
  ctx.r30.u64 = ctx.r4.u64;
  // lwz r27,36(r3)
  ctx.r27.u64 = REX_LOAD_U32(ctx.r3.u32 + 36);
  // mr r29,r5
  ctx.r29.u64 = ctx.r5.u64;
  // lwz r31,32(r3)
  ctx.r31.u64 = REX_LOAD_U32(ctx.r3.u32 + 32);
  // cmplw cr6,r31,r27
  ctx.cr6.compare<uint32_t>(ctx.r31.u32, ctx.r27.u32, ctx.xer);
  // lwz r10,0(r11)
  ctx.r10.u64 = REX_LOAD_U32(ctx.r11.u32 + 0);
  // clrlwi r28,r10,31
  ctx.r28.u64 = ctx.r10.u32 & 0x1;
  // beq cr6,0x8275b724
  if (ctx.cr6.eq) goto loc_8275B724;
loc_8275B6C0:
  // lwz r3,0(r31)
  ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 0);
  // Ours: an empty slot is passed over (the game read its type at guest 0x8 and the server crashed).
  if (ctx.r3.u32 == 0) [[unlikely]] {
    const uint32_t n = g_empty_slots.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 5 || n % 1000 == 0)
      REXLOG_WARN("BF game fix: an empty slot in an object list (at {:08X}) passed over ({} so far); the game "
                  "would have crashed there",
                  ctx.r31.u32, n);
    goto loc_8275B718;
  }
  // lwz r11,0(r30)
  ctx.r11.u64 = REX_LOAD_U32(ctx.r30.u32 + 0);
  // lwz r10,8(r3)
  ctx.r10.u64 = REX_LOAD_U32(ctx.r3.u32 + 8);
  // rlwinm r9,r10,16,16,31
  ctx.r9.u64 = __builtin_rotateleft64(ctx.r10.u32 | (ctx.r10.u64 << 32), 16) & 0xFFFF;
  // rlwinm r8,r10,29,19,29
  ctx.r8.u64 = __builtin_rotateleft64(ctx.r10.u32 | (ctx.r10.u64 << 32), 29) & 0x1FFC;
  // neg r7,r9
  ctx.r7.s64 = static_cast<int64_t>(-ctx.r9.u64);
  // neg r6,r9
  ctx.r6.s64 = static_cast<int64_t>(-ctx.r9.u64);
  // clrlwi r5,r10,27
  ctx.r5.u64 = ctx.r10.u32 & 0x1F;
  // srawi r4,r7,31
  ctx.xer.ca = (ctx.r7.s32 < 0) & ((ctx.r7.u32 & 0x7FFFFFFF) != 0);
  ctx.r4.s64 = ctx.r7.s32 >> 31;
  // lwzx r11,r8,r11
  ctx.r11.u64 = REX_LOAD_U32(ctx.r8.u32 + ctx.r11.u32);
  // clrlwi r10,r6,27
  ctx.r10.u64 = ctx.r6.u32 & 0x1F;
  // srw r9,r11,r5
  ctx.r9.u64 = ctx.r5.u8 & 0x20 ? 0 : (ctx.r11.u32 >> (ctx.r5.u8 & 0x3F));
  // srw r8,r4,r10
  ctx.r8.u64 = ctx.r10.u8 & 0x20 ? 0 : (ctx.r4.u32 >> (ctx.r10.u8 & 0x3F));
  // and r4,r9,r8
  ctx.r4.u64 = ctx.r9.u64 & ctx.r8.u64;
  // cmplwi cr6,r4,0
  ctx.cr6.compare<uint32_t>(ctx.r4.u32, 0, ctx.xer);
  // beq cr6,0x8275b718
  if (ctx.cr6.eq) goto loc_8275B718;
  // lwz r11,0(r3)
  ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);
  // mr r6,r28
  ctx.r6.u64 = ctx.r28.u64;
  // mr r5,r29
  ctx.r5.u64 = ctx.r29.u64;
  // lwz r10,8(r11)
  ctx.r10.u64 = REX_LOAD_U32(ctx.r11.u32 + 8);
  // mtctr r10
  ctx.ctr.u64 = ctx.r10.u64;
  // bctrl
  ctx.lr = 0x8275B718;
  REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
loc_8275B718:
  // addi r31,r31,4
  ctx.r31.s64 = ctx.r31.s64 + 4;
  // cmplw cr6,r31,r27
  ctx.cr6.compare<uint32_t>(ctx.r31.u32, ctx.r27.u32, ctx.xer);
  // bne cr6,0x8275b6c0
  if (!ctx.cr6.eq) goto loc_8275B6C0;
loc_8275B724:
  // addi r1,r1,128
  ctx.r1.s64 = ctx.r1.s64 + 128;
  // b 0x827be884
  __restgprlr_27(ctx, base);
  return;
}

// sub_821DBC68(r3, buckets, count): empties a hash table's buckets: for each bucket, every node of its chain (next at
// +36) freed (sub_82624D90), with the node's own buffer, then the bucket set to 0. sub_821E08D0(table) clears a table
// that way (buckets at +36, count at +40) and then frees the bucket array when there is more than one. Official servers
// crashed there four times, 13 to 15 Nov 2026 (0.4.0 server 6, 0.4.1 servers 10 and 13, then 0.4.6 All Maps 4 mid
// match on Iwo Jima, every player dropped): the table's bucket pointer was 1 with buckets counted, so the clear read
// guest 0x1 (the null page guard gave zeros) and then wrote there ("Unhandled guest access violation: write of guest
// 0x00000001", frames sub_821DBC68, sub_821E08D0, sub_823F0F30, sub_82410950, ... in the server's tick). What left the
// table that way is not known. A bucket pointer in the guest's first 64 KB (never a heap address) is a table with
// nothing to clear: sub_821DBC68 returns at once, and sub_821E08D0 empties it (no buckets, nothing to free) before
// its own code runs. Logged, the first 10 times.
namespace {
std::atomic<uint32_t> g_bad_tables{0};
bool BadBuckets(uint32_t buckets, uint32_t count) { return count != 0 && buckets < 0x10000; }
void NoteBadTable(const char* where, uint32_t table, uint32_t buckets, uint32_t count) {
  const uint32_t n = ++g_bad_tables;
  if (n <= 10)
    REXLOG_WARN("BF game fix: a hash table with its bucket pointer at {:08X} and {} buckets in {} (table {:08X}): "
                "treated as empty instead of a crash (time {})",
                buckets, count, where, table, n);
}
}  // namespace

REX_EXTERN(__imp__sub_821DBC68);
REX_HOOK_RAW(sub_821DBC68) {
  if (BadBuckets(ctx.r4.u32, ctx.r5.u32)) {
    NoteBadTable("sub_821DBC68", ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
    return;
  }
  __imp__sub_821DBC68(ctx, base);
}

REX_EXTERN(__imp__sub_821E08D0);
REX_HOOK_RAW(sub_821E08D0) {
  const uint32_t table = ctx.r3.u32;
  if (table >= 0x10000) {
    const uint32_t buckets = REX_LOAD_U32(table + 36), count = REX_LOAD_U32(table + 40);
    if (BadBuckets(buckets, count)) {
      NoteBadTable("sub_821E08D0", table, buckets, count);
      REX_STORE_U32(table + 36, 0);
      REX_STORE_U32(table + 40, 0);
    }
  }
  __imp__sub_821E08D0(ctx, base);
}

// sub_821DC048(object, key, f1 value) and sub_821DC178 (the same shape): set a value in a table inside an object (at
// +32), as the game does for its players and the like each tick. All Maps 3 (0.4.6.5) crashed at 19:12 UTC 15 Nov
// 2026 and All Maps 1 on 14 Nov reading such a table in sub_821D8160 (its lookup) from these two, in the server's tick
// (sub_824123F0): the object's memory had been released (a read of guest 1155ECE8, a page no longer committed), a
// minute after players left. An object whose memory is not live (not committed, readable and writable) is passed
// over: the setter returns 0 as it does for a locked object (+68), and nothing is written. Logged, the first 10 times.
namespace {
std::atomic<uint32_t> g_dead_objects{0};
bool GuestLive(uint32_t address, uint32_t size) {
  if (address < 0x10000) return false;
  auto* state = rex::system::kernel_state();
  rex::memory::Memory* memory = state ? state->memory() : nullptr;
  if (!memory) return true;
  for (uint32_t page = address & ~0xFFFu; page < address + size; page += 0x1000) {
    rex::memory::BaseHeap* heap = memory->LookupHeap(page);
    uint32_t protect = 0;
    if (!heap || !heap->QueryProtect(page, &protect)) return false;
    if (!(protect & rex::memory::kMemoryProtectRead) || !(protect & rex::memory::kMemoryProtectWrite)) return false;
  }
  return true;
}
void NoteDeadObject(const char* where, uint32_t object, uint32_t caller) {
  const uint32_t n = ++g_dead_objects;
  if (n <= 10)
    REXLOG_WARN("BF game fix: {} on an object whose memory is released ({:08X}, called from {:08X}): passed over "
                "instead of a crash (time {})",
                where, object, caller, n);
}
}  // namespace

REX_EXTERN(__imp__sub_821DC048);
REX_HOOK_RAW(sub_821DC048) {
  if (!GuestLive(ctx.r3.u32, 72)) {
    NoteDeadObject("sub_821DC048", ctx.r3.u32, uint32_t(ctx.lr));
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_821DC048(ctx, base);
}

REX_EXTERN(__imp__sub_821DC178);
REX_HOOK_RAW(sub_821DC178) {
  if (!GuestLive(ctx.r3.u32, 72)) {
    NoteDeadObject("sub_821DC178", ctx.r3.u32, uint32_t(ctx.lr));
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_821DC178(ctx, base);
}

namespace bf1943 {

#if defined(BF_DEV_TOOLS)
// BF_DEV_DEADOBJ_TEST=1 (dev builds): once, on the server's thread, a guest page allocated and released, then the
// setter called on an object there (the 15 Nov 19:12 All Maps 3 crash). With the guard it is passed over and logged;
// without, the read faults. The registers are put back after.
void RunDeadObjectTest(PPCContext& ctx, uint8_t* base) {
  static bool done = false;
  const char* v = std::getenv("BF_DEV_DEADOBJ_TEST");
  if (done || !v || *v != '1') return;
  done = true;
  rex::memory::Memory* memory = rex::system::kernel_state()->memory();
  rex::memory::BaseHeap* heap = memory->LookupHeap(0x20000000);
  uint32_t page = 0;
  if (!heap || !heap->Alloc(0x1000, 0x1000, rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
                            rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite, false, &page)) {
    REXLOG_WARN("BF game fix test: no guest page to release");
    return;
  }
  const bool live_before = GuestLive(page + 0x100, 72);
  heap->Release(page);
  PPCContext saved;
  std::memcpy(&saved, &ctx, sizeof(ctx));
  ctx.r3.u64 = page + 0x100;
  ctx.r4.u64 = 0x12345678;
  ctx.f1.f64 = 1.0;
  ctx.lr = 0x823F73E0;
  ctx.r1.u64 = ((ctx.r1.u32 - 0x1000) & ~0xFFu);
  sub_821DC048(ctx, base);
  REXLOG_INFO("BF game fix test: the setter on a released object at {:08X} came back (r3 {}; live before release: {})",
              page + 0x100, ctx.r3.u32, live_before);
  std::memcpy(&ctx, &saved, sizeof(ctx));
}
#endif

#if defined(BF_DEV_TOOLS)
// BF_DEV_FIX_TEST=1 (dev builds): once, from a hook on the game's thread, the guarded query over a list of three
// slots, the middle one empty, built on the free part of that thread's guest stack (below its frames). The first
// and last are objects whose type is not in the (empty) filter, so no method runs. Without the guard the empty slot
// faults at guest 0x8. The registers are put back after.
// BF_DEV_BADCALL_TEST=1: once, on the server's thread, a call through a broken function pointer (the stack address
// of the 15 Nov All Maps 3 stop). With guest_invalid_call_skip (on for dedicated servers, bf1943_app.h) it is skipped
// and logged; without, the program stops. The registers are put back after.
void RunBadCallTest(PPCContext& ctx, uint8_t* base) {
  static bool done = false;
  const char* v = std::getenv("BF_DEV_BADCALL_TEST");
  if (done || !v || *v != '1') return;
  done = true;
  PPCContext saved;
  std::memcpy(&saved, &ctx, sizeof(ctx));
  ctx.last_indirect_target = 0x7018E740;
  ctx.lr = 0x82000000;
  ctx.r3.u64 = 0x1234;
  rex::runtime::ResolveIndirectFunction(0x7018E740)(ctx, base);
  REXLOG_INFO("BF game fix test: the call through a broken pointer came back (r3 {:#x}); the server goes on", ctx.r3.u32);
  std::memcpy(&ctx, &saved, sizeof(ctx));
}

void RunGameFixTest(PPCContext& ctx, uint8_t* base) {
  RunBadCallTest(ctx, base);
  RunDeadObjectTest(ctx, base);
  static bool done = false;
  const char* v = std::getenv("BF_DEV_FIX_TEST");
  if (done || !v || *v != '1') return;
  done = true;
  PPCContext saved;
  std::memcpy(&saved, &ctx, sizeof(ctx));
  const uint32_t area = (ctx.r1.u32 - 0x1000) & ~0xFFu;  // 4 KB below the hook's caller: nothing of the thread there
  for (uint32_t a = area; a < area + 0x200; a += 4) REX_STORE_U32(a, 0);
  const uint32_t owner = area, list = area + 0x40, filter = area + 0x80, bits = area + 0x90, words = area + 0xA0,
                 object = area + 0x100, out = area + 0x180;
  REX_STORE_U32(list + 0, object);  // an object of type 0 (not in the filter)
  REX_STORE_U32(list + 4, 0);       // the empty slot
  REX_STORE_U32(list + 8, object);
  REX_STORE_U32(owner + 32, list);
  REX_STORE_U32(owner + 36, list + 12);
  REX_STORE_U32(filter + 0, bits);  // filter -> bits -> words: the bit set, all clear
  REX_STORE_U32(bits + 0, words);
  ctx.r3.u64 = owner, ctx.r4.u64 = filter, ctx.r5.u64 = out;
  ctx.r1.u64 = area - 0x100;  // the query's own frame below the test's data
  const uint32_t before = g_empty_slots.load();
  sub_8275B690(ctx, base);
  REXLOG_INFO("BF game fix test: a list with an empty slot queried without a fault ({} empty slot passed over)",
              g_empty_slots.load() - before);
  // The hash table of the 15 Nov crash: its bucket pointer 1 with 3 buckets counted, and a buffer of its own (+8 to
  // +16) of one byte, which sub_821E08D0 does not free. Without the guard the clear writes guest 0x1.
  std::memcpy(&ctx, &saved, sizeof(ctx));
  for (uint32_t a = area; a < area + 0x200; a += 4) REX_STORE_U32(a, 0);
  const uint32_t table = area;
  REX_STORE_U32(table + 36, 1);
  REX_STORE_U32(table + 40, 3);
  ctx.r3.u64 = table;
  ctx.r1.u64 = area - 0x100;
  const uint32_t bad_before = g_bad_tables.load();
  sub_821E08D0(ctx, base);
  REXLOG_INFO("BF game fix test: a hash table with its bucket pointer at 1 cleared without a fault ({} caught; now "
              "bucket pointer {:08X}, {} buckets)",
              g_bad_tables.load() - bad_before, REX_LOAD_U32(table + 36), REX_LOAD_U32(table + 40));
  std::memcpy(&ctx, &saved, sizeof(ctx));
}
#endif

}  // namespace bf1943
