// Our profanity filter (text_filter.h): chat words shown as stars, soldier names refused, and the everyday words and
// names it must leave alone (a war game's chat: assault, class, pass, cockpit, torpedo, therapist).
// Build (no game, no SDK): clang++ -std=c++20 -I bf/src bf/tests/text_filter_tests.cpp -o text_filter_tests
#include "text_filter.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using bf1943::text_filter::Censor;
using bf1943::text_filter::NameAllowed;
using bf1943::text_filter::CheckTag;
using bf1943::text_filter::TagProblem;
using bf1943::text_filter::TagProblemCode;

namespace {
int failures = 0, checks = 0;
void Chat(const std::string& typed, const std::string& shown) {
  ++checks;
  const std::string got = Censor(typed);
  if (got != shown) {
    std::printf("FAIL chat \"%s\": shown \"%s\", want \"%s\"\n", typed.c_str(), got.c_str(), shown.c_str());
    ++failures;
  }
}
void Name(const std::string& name, bool allowed) {
  ++checks;
  if (NameAllowed(name) != allowed) {
    std::printf("FAIL name \"%s\": %s, want %s\n", name.c_str(), allowed ? "refused" : "allowed",
                allowed ? "allowed" : "refused");
    ++failures;
  }
}
// A clan tag (and the name it goes with): the problem code expected ("" when it may be worn).
void Tag(const std::string& tag, const std::string& want, const std::string& name = "") {
  ++checks;
  const std::string got = TagProblemCode(CheckTag(tag, name));
  if (got != want) {
    std::printf("FAIL tag \"%s\" with \"%s\": \"%s\", want \"%s\"\n", tag.c_str(), name.c_str(), got.c_str(),
                want.c_str());
    ++failures;
  }
}
}  // namespace

int main(int argc, char** argv) {
  // --tags: each line of stdin "tag<TAB>name" answered with its problem code (tools/test_clan_tags.py compares the
  // backend's Python rules, server/text_filter.py, with these).
  if (argc > 1 && !std::strcmp(argv[1], "--tags")) {
    std::string line;
    while (std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const size_t tab = line.find('\t');
      const std::string tag = line.substr(0, tab), name = tab == std::string::npos ? "" : line.substr(tab + 1);
      std::printf("%s\t%s\n", line.c_str(), TagProblemCode(CheckTag(tag, name)));
    }
    return 0;
  }
  // Chat: the word as stars, the rest as typed.
  Chat("you fucking idiot", "you ******* idiot");
  Chat("this is bullshit!", "this is ********!");
  Chat("FUCK", "****");
  Chat("fuuuuck that", "******* that");
  Chat("sh1t happens", "**** happens");
  Chat("$hit", "****");
  Chat("kick ass", "kick ***");
  Chat("ASS!!", "***!!");
  Chat("a55 and 4ss", "*** and ***");
  Chat("f u c k you", "* * * * you");
  Chat("f.u.c.k", "*.*.*.*");
  Chat("what a b1tch", "what a *****");
  Chat("motherfucker", "************");
  Chat("'fuck'", "'****'");
  Chat("caf\xC3\xA9 shit", "caf\xC3\xA9 ****");  // other languages' letters stay as typed
  Chat("tits", "****");
  Chat("cumming in hot", "******* in hot");
  // Chat: left as typed.
  Chat("assault the flag", "assault the flag");
  Chat("nice class, pass the ammo", "nice class, pass the ammo");
  Chat("assassin on the ridge", "assassin on the ridge");
  Chat("the cockpit is hit", "the cockpit is hit");
  Chat("torpedo the carrier", "torpedo the carrier");
  Chat("grass and bass", "grass and bass");
  Chat("my therapist says hi", "my therapist says hi");
  Chat("Scunthorpe United", "Scunthorpe United");
  Chat("455 points, 50 cal", "455 points, 50 cal");
  Chat("hello world", "hello world");
  Chat("I am at A", "I am at A");
  Chat("niger delta", "niger delta");
  Chat("japan and the nazis", "japan and the nazis");  // hate words are for names, not a war game's chat
  Chat("", "");
  Chat("   ", "   ");
  Chat("good game!", "good game!");

  // Names: refused.
  Name("FuckYou", false);
  Name("xXShitXx", false);
  Name("Big_Ass", false);
  Name("BigAss", false);
  Name("Ass69", false);
  Name("B1tch", false);
  Name("F_U_C_K", false);
  Name("Fuuuck", false);
  Name("A_S_S", false);
  Name("Dick7", false);
  Name("Cum_Lord", false);
  Name("TheRapist", false);
  Name("Hitler88", false);
  Name("xXNaziXx", false);
  Name("KKKiller", false);
  Name("Sniper1488", false);
  Name("Big Ass", false);
  // Names: allowed.
  Name("testboi", true);
  Name("Soldier1234", true);
  Name("AssaultKing", true);
  Name("Classy_Sniper", true);
  Name("CockpitAce", true);
  Name("Torpedo_Joe", true);
  Name("Therapist", true);
  Name("Scunthorpe", true);
  Name("WakeFox42", true);
  Name("Assassin", true);
  Name("Nazir", true);
  Name("Dickens", true);
  Name("Cumberland", true);
  Name("Titan", true);
  Name("Japan", true);
  Name("Hitman", true);
  Name("PacificHawk10", true);
  Name("Grape", true);

  // Clan tags: worn.
  Tag("", "");
  Tag("ABC", "");
  Tag("Wake", "");
  Tag("UK", "");
  Tag("Fuzz", "");
  Tag("Boss", "");
  Tag("Zulu", "");
  Tag("1943", "");
  Tag("Iwo", "");
  Tag("Tide", "");
  Tag("x7", "");
  Tag("Fox", "", "Hound");
  // Length and characters.
  Tag("A", "length");
  Tag("ABCDE", "length");
  Tag("A-B", "chars");
  Tag("A B", "chars");
  Tag("[A]", "chars");
  Tag("D.V", "chars");
  // The developers' tag and its lookalikes.
  Tag("Dev", "reserved");
  Tag("DEV", "reserved");
  Tag("dev", "reserved");
  Tag("Devs", "reserved");
  Tag("D3V", "reserved");
  Tag("D3vs", "reserved");
  Tag("xDevx", "length");  // five: too long before it is read
  Tag("xDvx", "reserved");
  Tag("DDEV", "reserved");
  Tag("Deev", "reserved");
  Tag("Dvz", "reserved");
  Tag("Devz", "reserved");
  Tag("Dev1", "reserved");
  Tag("Adm", "reserved");
  Tag("Admn", "reserved");
  Tag("Mod", "reserved");
  Tag("M0DS", "reserved");
  Tag("EA", "reserved");
  Tag("DICE", "reserved");
  // Profanity and slurs (his example first).
  Tag("NOG", "profanity");
  Tag("N0G", "profanity");
  Tag("nogg", "profanity");
  Tag("NIG", "profanity");
  Tag("N1G", "profanity");
  Tag("NlG", "profanity");
  Tag("NGGA", "profanity");
  Tag("FAG", "profanity");
  Tag("FGT", "profanity");
  Tag("KKK", "profanity");
  Tag("FU", "profanity");
  Tag("FUK", "profanity");
  Tag("Fck", "profanity");
  Tag("STFU", "profanity");
  Tag("SS", "profanity");
  Tag("xSSx", "profanity");
  Tag("88", "profanity");
  Tag("1488", "profanity");
  Tag("H88", "profanity");
  Tag("ASS", "profanity");
  Tag("A55", "profanity");
  Tag("Cunt", "profanity");
  Tag("Cum", "profanity");
  Tag("Nazi", "profanity");
  Tag("N4Z1", "profanity");
  Tag("Kys", "profanity");
  // The tag and the name together.
  Tag("FU", "profanity", "Bob");
  Tag("Bull", "with_name", "shit");
  Tag("Big", "with_name", "Ass");
  Tag("Shi", "with_name", "tHead");
  Tag("Shi", "", "Fox");

  std::printf("%d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
