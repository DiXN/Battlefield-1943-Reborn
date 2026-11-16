// The Dev clan tag's colours in the game's own text (TekRantGaming, 16 Nov 2026: "The Dev clan tag adds an effect to
// the username in launcher and in game and on the scoreboard it takes inspiration from call of duty world at wars
// custom clan tags ... like world at wars cycl clan tag"). Our pages draw it themselves (clan_tags.cpp DrawTagged);
// this does the same in everything the game draws with its own text: the kill feed, the name tags over soldiers and
// its scoreboard show "[Dev]Name" (clan_tags.cpp GameNamesTick writes it), and its letters, the tag and the name
// ("the full clan tag and user name not just the clan tag"), run through the colours.
//
// How the game draws text (its Flash runtime's text renderer, found 16 Nov 2026 from the recompiled code):
//   sub_826ABAC0 (vtable 0x82048644 [1]) packs a text run's colour into the renderer at +192 (0xAABBGGRR as one word:
//     red in the low byte) and calls sub_826AB588;
//   sub_826AB588(renderer r3, font r4, lines r5, ...) walks each line (a node: +0 next, +8 and +12 the begin and end of
//     its UTF-16 characters; a circular list whose head, r5, sits in the text command), skips 13, 10 and 189, and calls sub_826AB240 for every other character; at the end it
//     calls sub_826A5650(renderer, ctx), ctx being the word at its entry r1 + 116;
//   sub_826AB240(renderer r3, font r4, character r5, ..., bounds r9) adds one glyph's quad (no colour in the vertices;
//     r9 not 0: measuring only, nothing drawn);
//   sub_826A5650(renderer, ctx) draws the glyphs added since the last flush with the colour at renderer + 192 (a pixel
//     shader constant, sub_8269F660).
// So a run holding "[Dev]" is cut: before each glyph whose colour differs, the glyphs so far are drawn (the flush) and
// the colour at +192 set; the run's own colour is put back after it. A run without "[Dev]" is not touched.
// bf_dev_tag_colours=false turns it off.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "clan_tags.h"

REXCVAR_DECLARE(bool, server);
REXCVAR_DEFINE_BOOL(bf_dev_tag_colours, true, "BF1943/Online",
                    "The Dev clan tag's letters cycle through the colours in the game's own text (kill feed, name tags)");

REX_EXTERN(__imp__sub_826AB588);  // a text run's lines
REX_EXTERN(__imp__sub_826AB240);  // one glyph
REX_EXTERN(sub_826A5650);         // draw the glyphs added so far

namespace {

uint32_t Load32(const uint8_t* base, uint32_t a) {
  const uint8_t* p = base + a;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
uint16_t Load16(const uint8_t* base, uint32_t a) { return uint16_t(base[a] << 8 | base[a + 1]); }
void Store32(uint8_t* base, uint32_t a, uint32_t v) {
  for (int i = 0; i < 4; ++i) base[a + i] = uint8_t(v >> (24 - 8 * i));
}
bool Plausible(uint32_t a) { return a >= 0x00100000u && a < 0xFFF00000u; }

constexpr uint32_t kColour = 192;  // the renderer's text colour word

// The run being drawn now (the game draws its text on one thread; thread_local keeps a second one apart).
struct Run {
  bool active = false;
  uint32_t renderer = 0, flush_ctx = 0, colour = 0;  // the run's own colour
  uint32_t glyph = 0;                                // glyphs drawn so far
  std::vector<uint8_t> letter;                       // per glyph: 0 the run's colour, n letter n - 1 of a "[Dev]Name"
  double seconds = 0;
};
thread_local Run g_run;

// The run's characters as the glyph calls will come (13, 10 and 189 skipped), each one of a developer's whole tagged
// name marked with its place in it (1 the "[", 6 the name's first letter...): "[Dev]" and the name after it, up to
// 15 letters, digits or underscores (TekRantGaming, 16 Nov 2026: "it should be applied to the full clan tag and user
// name not just the clan tag"). False when the run holds no "[Dev]" (it is then drawn as it is).
// The lines are a circular list with its head inside the text command (r5): the first line is the head's next, and
// the walk ends back at the head (a line: +0 next, +4 prev, +8 and +12 its characters' begin and end).
bool Scan(const uint8_t* base, uint32_t head, std::vector<uint8_t>& letter) {
  // Every text the game draws comes through here each frame: nothing is allocated unless the run holds "[Dev]".
  static constexpr uint16_t kTag[5] = {'[', 'D', 'e', 'v', ']'};
  auto name_char = [](uint16_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
  };
  uint16_t last[5] = {};  // the last five characters, oldest first
  uint32_t count = 0;     // characters (as the glyph calls come)
  uint32_t marked = 0;    // the tagged name's characters marked so far (0: not in one)
  bool any = false;
  int n = 0;
  for (uint32_t line = Load32(base, head); line != head && Plausible(line) && n < 64; ++n, line = Load32(base, line)) {
    const uint32_t b = Load32(base, line + 8), e = Load32(base, line + 12);
    if (!Plausible(b) || e < b || e - b > 4096) return false;
    for (uint32_t p = b; p + 2 <= e; p += 2) {
      const uint16_t c = Load16(base, p);
      if (c == 13 || c == 10 || c == 189) continue;
      std::memmove(last, last + 1, sizeof(last) - sizeof(last[0]));
      last[4] = c;
      ++count;
      if (count >= 5 && std::memcmp(last, kTag, sizeof(kTag)) == 0) {
        if (!any) letter.clear();
        any = true;
        if (letter.size() < count) letter.resize(count, 0);
        for (uint32_t k = 0; k < 5; ++k) letter[count - 5 + k] = uint8_t(k + 1);  // [ D e v ]
        marked = 5;
      } else if (marked && name_char(c) && marked < 5 + 15) {
        if (letter.size() < count) letter.resize(count, 0);
        letter[count - 1] = uint8_t(++marked);  // the name after the tag
      } else {
        marked = 0;
      }
    }
  }
  return any;
}

double Seconds() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

REX_HOOK_RAW(sub_826AB588) {
#if defined(BF_DEV_TOOLS)
  // BF_DEV_TEXTLOG=1: the first 60 different texts the game draws through here (what reaches the hook).
  static const bool text_log = std::getenv("BF_DEV_TEXTLOG") != nullptr;
  if (text_log) {
    static std::set<std::string> seen;
    static uint32_t calls = 0;
    ++calls;
    std::string text;
    const uint32_t head = ctx.r5.u32;
    uint32_t n = 0;
    for (uint32_t line = Plausible(head) ? Load32(base, head) : 0; line != head && Plausible(line) && n < 8;
         ++n, line = Load32(base, line)) {
      const uint32_t b = Load32(base, line + 8), e = Load32(base, line + 12);
      if (!Plausible(b) || e < b || e - b > 400) break;
      for (uint32_t p = b; p + 2 <= e; p += 2) {
        const uint16_t c = Load16(base, p);
        text += c >= 32 && c < 127 ? char(c) : '?';
      }
      text += '|';
    }
    if (seen.size() < 60 && seen.insert(text).second)
      REXLOG_INFO("BF dev text: call {} renderer {:08X} colour {:08X} \"{}\"", calls, ctx.r3.u32,
                  Plausible(ctx.r3.u32) ? Load32(base, ctx.r3.u32 + kColour) : 0u, text);
  }
#endif
  if (REXCVAR_GET(server) || !REXCVAR_GET(bf_dev_tag_colours) || g_run.active) return __imp__sub_826AB588(ctx, base);
  std::vector<uint8_t> letter;
  const uint32_t renderer = ctx.r3.u32;
  if (!Plausible(renderer) || !Scan(base, ctx.r5.u32, letter)) return __imp__sub_826AB588(ctx, base);
  g_run.active = true;
  g_run.renderer = renderer;
  g_run.flush_ctx = Load32(base, ctx.r1.u32 + 116);
  g_run.colour = Load32(base, renderer + kColour);
  g_run.glyph = 0;
  g_run.letter = std::move(letter);
  g_run.seconds = Seconds();
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("BF clan tag: the Dev tag's colours in the game's text (first run: colour {:08X}, {} glyphs)",
                g_run.colour, g_run.letter.size());
  }
  __imp__sub_826AB588(ctx, base);
  Store32(base, renderer + kColour, g_run.colour);
  g_run.active = false;
}

REX_HOOK_RAW(sub_826AB240) {
  if (g_run.active && ctx.r9.u32 == 0 && ctx.r3.u32 == g_run.renderer) {
    const uint32_t i = g_run.glyph++;
    const uint8_t n = i < g_run.letter.size() ? g_run.letter[i] : 0;
    uint32_t want = g_run.colour;
    if (n) {
      // Our colour (ImGui's packing is the game's: red in the low byte) with the run's own alpha (fades).
      const uint32_t c = bf1943::clan::DevColour(n - 1, g_run.seconds);
      want = (g_run.colour & 0xFF000000u) | (c & 0x00FFFFFFu);
    }
    if (want != Load32(base, g_run.renderer + kColour)) {
      PPCContext flush = ctx;
      flush.r3.u64 = g_run.renderer;
      flush.r4.u64 = g_run.flush_ctx;
      sub_826A5650(flush, base);
      Store32(base, g_run.renderer + kColour, want);
    }
  }
  __imp__sub_826AB240(ctx, base);
}
