// The news window's feed and pictures: see news.h and docs/NEWS.md.

#include "news.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <variant>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "http.h"
#include "picture_decode.h"

// The feed: news.json on the public repository's news branch (docs/NEWS.md). Another address (http://127.0.0.1:...
// for tests) or a file on this PC (a feed being written, its pictures beside it) can be named instead.
REXCVAR_DEFINE_STRING(bf_news_feed,
                      "https://raw.githubusercontent.com/TekRantGaming/Battlefield-1943-Reborn/news/news.json", "BF1943",
                      "Where the main menu's news comes from (an https address, or a news.json file on this PC)");

namespace bf1943::news {
namespace fs = std::filesystem;

namespace {

// ---- a small JSON reader (the feed is written by hand: a mistake is reported, never crashes)
struct Value;
using Object = std::vector<std::pair<std::string, Value>>;
using Array = std::vector<Value>;
struct Value {
  std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> v;
  const Value* Get(const char* key) const {
    if (auto* o = std::get_if<std::shared_ptr<Object>>(&v))
      for (const auto& [k, val] : **o)
        if (k == key) return &val;
    return nullptr;
  }
  const Array* AsArray() const {
    auto* a = std::get_if<std::shared_ptr<Array>>(&v);
    return a ? a->get() : nullptr;
  }
  bool IsObject() const { return std::holds_alternative<std::shared_ptr<Object>>(v); }
};

struct Reader {
  const std::string& s;
  size_t at = 0;
  std::string error;
  int depth = 0;

  void Space() {
    while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;
  }
  bool Fail(const char* what) {
    if (error.empty()) {
      size_t line = 1;
      for (size_t i = 0; i < at && i < s.size(); ++i) line += s[i] == '\n';
      error = std::string(what) + " at line " + std::to_string(line);
    }
    return false;
  }
  static void Utf8(std::string& out, uint32_t c) {
    if (c < 0x80) out += char(c);
    else if (c < 0x800) out += char(0xC0 | (c >> 6)), out += char(0x80 | (c & 0x3F));
    else if (c < 0x10000) out += char(0xE0 | (c >> 12)), out += char(0x80 | ((c >> 6) & 0x3F)), out += char(0x80 | (c & 0x3F));
    else
      out += char(0xF0 | (c >> 18)), out += char(0x80 | ((c >> 12) & 0x3F)), out += char(0x80 | ((c >> 6) & 0x3F)),
          out += char(0x80 | (c & 0x3F));
  }
  bool Hex4(uint32_t& c) {
    if (at + 4 > s.size()) return false;
    c = 0;
    for (int i = 0; i < 4; ++i) {
      const char h = s[at++];
      c <<= 4;
      if (h >= '0' && h <= '9') c |= uint32_t(h - '0');
      else if (h >= 'a' && h <= 'f') c |= uint32_t(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') c |= uint32_t(h - 'A' + 10);
      else return false;
    }
    return true;
  }
  bool String(std::string& out) {
    ++at;  // the quote
    while (at < s.size() && s[at] != '"') {
      const char c = s[at++];
      if (c != '\\') {
        out += c;
        continue;
      }
      if (at >= s.size()) break;
      const char e = s[at++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += ' '; break;
        case 'r': break;
        case 'b': case 'f': break;
        case 'u': {
          uint32_t c1 = 0;
          if (!Hex4(c1)) return Fail("a bad \\u escape");
          if (c1 >= 0xD800 && c1 < 0xDC00 && at + 6 <= s.size() && s[at] == '\\' && s[at + 1] == 'u') {
            at += 2;
            uint32_t c2 = 0;
            if (!Hex4(c2)) return Fail("a bad \\u escape");
            c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
          }
          Utf8(out, c1);
          break;
        }
        default: out += e; break;  // \" \\ \/
      }
    }
    if (at >= s.size()) return Fail("a string without its closing quote");
    ++at;
    return true;
  }
  bool Parse(Value& out) {
    if (++depth > 32) return Fail("too deep");
    Space();
    if (at >= s.size()) return Fail("the file ends early");
    const char c = s[at];
    bool ok = true;
    if (c == '{') {
      auto o = std::make_shared<Object>();
      ++at;
      Space();
      if (at < s.size() && s[at] == '}') ++at;
      else
        for (;;) {
          Space();
          if (at >= s.size() || s[at] != '"') { ok = Fail("a name in quotes expected"); break; }
          std::string key;
          if (!String(key)) { ok = false; break; }
          Space();
          if (at >= s.size() || s[at] != ':') { ok = Fail("a colon expected"); break; }
          ++at;
          Value v;
          if (!Parse(v)) { ok = false; break; }
          o->emplace_back(std::move(key), std::move(v));
          Space();
          if (at < s.size() && s[at] == ',') { ++at; continue; }
          if (at < s.size() && s[at] == '}') { ++at; break; }
          ok = Fail("a comma or } expected (a missing comma?)");
          break;
        }
      out.v = o;
    } else if (c == '[') {
      auto a = std::make_shared<Array>();
      ++at;
      Space();
      if (at < s.size() && s[at] == ']') ++at;
      else
        for (;;) {
          Value v;
          if (!Parse(v)) { ok = false; break; }
          a->push_back(std::move(v));
          Space();
          if (at < s.size() && s[at] == ',') { ++at; continue; }
          if (at < s.size() && s[at] == ']') { ++at; break; }
          ok = Fail("a comma or ] expected (a missing comma?)");
          break;
        }
      out.v = a;
    } else if (c == '"') {
      std::string str;
      ok = String(str);
      out.v = std::move(str);
    } else if (s.compare(at, 4, "true") == 0) {
      at += 4, out.v = true;
    } else if (s.compare(at, 5, "false") == 0) {
      at += 5, out.v = false;
    } else if (s.compare(at, 4, "null") == 0) {
      at += 4, out.v = nullptr;
    } else if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      const size_t from = at;
      while (at < s.size() && (std::isdigit(static_cast<unsigned char>(s[at])) || std::strchr("+-.eE", s[at]))) ++at;
      out.v = std::atof(s.substr(from, at - from).c_str());
    } else {
      ok = Fail("an unexpected character");
    }
    --depth;
    return ok;
  }
};

std::string Str(const Value* v) {
  if (!v) return {};
  if (auto* s = std::get_if<std::string>(&v->v)) return *s;
  if (auto* d = std::get_if<double>(&v->v)) return std::to_string(int64_t(*d));
  if (auto* b = std::get_if<bool>(&v->v)) return *b ? "true" : "false";
  return {};
}
int Int(const Value* v) {
  if (!v) return 0;
  if (auto* d = std::get_if<double>(&v->v)) return int(*d);
  if (auto* s = std::get_if<std::string>(&v->v)) return std::atoi(s->c_str());
  return 0;
}
bool Bool(const Value* v) {
  if (!v) return false;
  if (auto* b = std::get_if<bool>(&v->v)) return *b;
  return Str(v) == "true" || Int(v) != 0;
}

std::string Trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}
bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string Host(const std::string& url) {
  size_t at = StartsWith(url, "https://") ? 8 : StartsWith(url, "http://") ? 7 : std::string::npos;
  if (at == std::string::npos) return {};
  const size_t end = url.find_first_of("/?#:", at);
  return Lower(url.substr(at, end == std::string::npos ? std::string::npos : end - at));
}
bool IsDate(const std::string& s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
  for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
  return true;
}
bool LocalTest(const std::string& url) {
  const std::string h = Host(url);
  return StartsWith(url, "http://") && (h == "127.0.0.1" || h == "localhost");
}
// Pictures come from GitHub's and YouTube's picture hosts only (and this PC's test feeds).
bool PictureHostAllowed(const std::string& url) {
  if (LocalTest(url)) return true;
  if (!StartsWith(url, "https://")) return false;
  static constexpr const char* kHosts[] = {"raw.githubusercontent.com", "github.com", "user-images.githubusercontent.com",
                                           "objects.githubusercontent.com", "i.ytimg.com", "img.youtube.com"};
  const std::string h = Host(url);
  for (const char* k : kHosts)
    if (h == k) return true;
  return false;
}
bool LinkAllowed(const std::string& url) { return StartsWith(url, "https://") || LocalTest(url); }

uint64_t Fnv(const std::string& s) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
  return h;
}

// ---- the worker's state
std::mutex g_mutex;
std::shared_ptr<const Feed> g_fresh, g_cached;
std::atomic<bool> g_started{false}, g_done{false};
fs::path g_feed_dir;  // a feed read from a file: its folder (its pictures may be beside it)

bool ReadFile(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}
bool WriteWhole(const fs::path& p, const std::vector<uint8_t>& bytes) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  const fs::path tmp = fs::path(p).concat(".part");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!out) return false;
  }
  fs::rename(tmp, p, ec);  // whole, never half written under the menu's reader
  return !ec;
}

// The pictures of a feed, each once (a picture's address never changes what it shows: a new one gets a new address);
// pictures no feed names any more are removed.
void FetchPictures(const fs::path& dir, const Feed& feed) {
  std::set<std::string> keep;
  int got = 0, failed = 0;
  for (const Item& it : feed.items) {
    const std::string url = PictureUrl(it);
    if (url.empty()) continue;
    const fs::path file = PictureFile(dir.parent_path(), it);
    keep.insert(file.filename().string());
    std::error_code ec;
    if (fs::exists(file, ec)) continue;
    std::vector<uint8_t> bytes;
    bool ok = false;
    if (!g_feed_dir.empty() && url.find("://") == std::string::npos) {
      ok = ReadFile(g_feed_dir / fs::u8path(url), bytes);  // a preview's picture beside its file
    } else {
      ok = HttpGet(url, bytes, nullptr, nullptr, 15000);
    }
    int w = 0, h = 0;
    ok = ok && !bytes.empty() && bytes.size() <= (8u << 20) && !DecodePicture(bytes.data(), bytes.size(), w, h).empty() && w > 0;
    if (ok && WriteWhole(file, bytes)) {
      ++got;
      REXLOG_INFO("BF news: picture {} ({}x{})", url, w, h);
    } else {
      ++failed;
      REXLOG_WARN("BF news: picture {} could not be fetched or read", url);
    }
  }
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir, ec)) {
    const std::string name = e.path().filename().string();
    if (StartsWith(name, "pic_") && !keep.count(name)) fs::remove(e.path(), ec);
  }
  REXLOG_INFO("BF news: pictures: {} fetched, {} failed, {} in the feed", got, failed, keep.size());
}

}  // namespace

std::string Today() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[16];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
  return buf;
}

bool ValidYouTubeId(const std::string& id) {
  if (id.size() != 11) return false;
  for (char c : id)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
  return true;
}
std::string WatchUrl(const std::string& id) { return "https://www.youtube.com/watch?v=" + id; }

static const char* const kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
std::string ShortDate(const std::string& iso) {
  if (!IsDate(iso)) return {};
  const int m = std::atoi(iso.substr(5, 2).c_str()), d = std::atoi(iso.substr(8, 2).c_str());
  if (m < 1 || m > 12) return {};
  return std::to_string(d) + " " + kMonths[m - 1];
}
std::string DayLabel(const std::string& iso) {
  if (!IsDate(iso)) return {};
  std::tm tm{};
  tm.tm_year = std::atoi(iso.substr(0, 4).c_str()) - 1900, tm.tm_mon = std::atoi(iso.substr(5, 2).c_str()) - 1;
  tm.tm_mday = std::atoi(iso.substr(8, 2).c_str()), tm.tm_hour = 12;
  std::mktime(&tm);  // fills in the weekday
  static const char* const kDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  return std::string(kDays[std::clamp(tm.tm_wday, 0, 6)]) + " " + std::to_string(tm.tm_mday);
}

std::string PictureUrl(const Item& item) {
  if (!item.image.empty()) return item.image;
  if (!item.youtube.empty()) return "https://i.ytimg.com/vi/" + item.youtube + "/hqdefault.jpg";
  return {};
}
fs::path PictureFile(const fs::path& cache_dir, const Item& item) {
  const std::string url = PictureUrl(item);
  char name[32];
  std::snprintf(name, sizeof(name), "pic_%016llx.img", static_cast<unsigned long long>(Fnv(url)));
  return cache_dir / "news" / name;
}

bool Parse(const std::string& json, Feed& out, std::string& error, const std::string& today) {
  out = {};
  Reader r{json};
  Value root;
  if (!r.Parse(root)) {
    error = r.error;
    return false;
  }
  if (!root.IsObject()) {
    error = "the file is not one { } object";
    return false;
  }
  std::string notes;
  auto note = [&](const std::string& n) { notes += (notes.empty() ? "" : "; ") + n; };
  out.edition = Trim(Str(root.Get("edition")));
  out.status = Trim(Str(root.Get("status")));
  std::set<std::string> ids;
  if (const Array* items = root.Get("items") ? root.Get("items")->AsArray() : nullptr) {
    for (const Value& v : *items) {
      Item it;
      it.title = Trim(Str(v.Get("title")));
      if (it.title.empty()) {
        note("an item without a title left out");
        continue;
      }
      it.id = Trim(Str(v.Get("id")));
      if (it.id.empty()) it.id = it.title;
      it.kind = Lower(Trim(Str(v.Get("kind"))));
      it.kicker = Trim(Str(v.Get("kicker")));
      it.text = Trim(Str(v.Get("text")));
      it.date = Trim(Str(v.Get("date")));
      it.image = Trim(Str(v.Get("image")));
      it.youtube = Trim(Str(v.Get("youtube")));
      it.link = Trim(Str(v.Get("link")));
      it.button = Trim(Str(v.Get("button")));
      it.priority = Int(v.Get("priority"));
      it.lead = Bool(v.Get("lead"));
      const std::string from = Trim(Str(v.Get("from"))), until = Trim(Str(v.Get("until")));
      if (!it.date.empty() && !IsDate(it.date)) note("\"" + it.title + "\": date not YYYY-MM-DD"), it.date.clear();
      if (IsDate(from) && today < from) continue;    // not yet
      if (IsDate(until) && today > until) continue;  // over
      // A full YouTube address works too: its id is taken from it.
      if (!it.youtube.empty() && !ValidYouTubeId(it.youtube)) {
        std::string id;
        for (const char* key : {"v=", "youtu.be/", "/embed/", "/shorts/", "/live/"})
          if (const size_t at = it.youtube.find(key); at != std::string::npos && id.empty()) id = it.youtube.substr(at + std::strlen(key), 11);
        if (ValidYouTubeId(id)) it.youtube = id;
        else note("\"" + it.title + "\": youtube is not a video id"), it.youtube.clear();
      }
      const bool local_picture = !it.image.empty() && it.image.find("://") == std::string::npos && !g_feed_dir.empty();
      if (!it.image.empty() && !local_picture && !PictureHostAllowed(it.image))
        note("\"" + it.title + "\": image not on GitHub or YouTube, left out"), it.image.clear();
      if (!it.link.empty() && !LinkAllowed(it.link)) note("\"" + it.title + "\": link is not https, left out"), it.link.clear();
      if (!ids.insert(it.id).second) it.id += "#" + std::to_string(ids.size());
      out.items.push_back(std::move(it));
    }
  }
  if (const Array* up = root.Get("upcoming") ? root.Get("upcoming")->AsArray() : nullptr) {
    for (const Value& v : *up) {
      Upcoming u;
      u.date = Trim(Str(v.Get("date"))), u.label = Trim(Str(v.Get("label"))), u.text = Trim(Str(v.Get("text")));
      if (u.text.empty()) continue;
      if (!u.date.empty() && !IsDate(u.date)) note("upcoming \"" + u.text + "\": date not YYYY-MM-DD"), u.date.clear();
      if (IsDate(u.date) && u.date < today) continue;  // past
      if (u.label.empty()) u.label = u.date.empty() ? "SOON" : DayLabel(u.date);
      out.upcoming.push_back(std::move(u));
    }
  }
  // Lead first, then priority, then the newest; the upcoming soonest first (the undated ones last, as written).
  std::stable_sort(out.items.begin(), out.items.end(), [](const Item& a, const Item& b) {
    if (a.lead != b.lead) return a.lead;
    if (a.priority != b.priority) return a.priority > b.priority;
    return a.date > b.date;
  });
  std::stable_sort(out.upcoming.begin(), out.upcoming.end(), [](const Upcoming& a, const Upcoming& b) {
    if (a.date.empty() != b.date.empty()) return b.date.empty();
    return a.date < b.date;
  });
  if (out.items.size() > 16) out.items.resize(16), note("only the first 16 items are shown");
  if (out.upcoming.size() > 6) out.upcoming.resize(6);
  error = notes;
  return true;
}

void Start(const fs::path& cache_dir) {
  if (g_started.exchange(true)) return;
  const fs::path dir = cache_dir / "news";
  // The cached feed at once (the News row's, offline): read here, it is small.
  {
    std::vector<uint8_t> bytes;
    Feed f;
    std::string error;
    if (ReadFile(dir / "feed.json", bytes) && Parse(std::string(bytes.begin(), bytes.end()), f, error, Today())) {
      std::lock_guard lock(g_mutex);
      g_cached = std::make_shared<Feed>(std::move(f));
    }
  }
  const std::string feed = REXCVAR_GET(bf_news_feed);
  std::thread([dir, feed] {
    const auto began = std::chrono::steady_clock::now();
    std::vector<uint8_t> bytes;
    bool got = false;
    if (feed.empty()) {
      REXLOG_INFO("BF news: no feed (bf_news_feed is empty)");
    } else if (feed.find("://") == std::string::npos) {
      g_feed_dir = fs::u8path(feed).parent_path();
      got = ReadFile(fs::u8path(feed), bytes);
    } else {
      got = HttpGet(feed, bytes, nullptr, nullptr, 8000) && bytes.size() <= (1u << 20);
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
    Feed f;
    std::string error;
    if (!got) {
      REXLOG_INFO("BF news: no feed from {} ({} ms)", feed, ms);
    } else if (!Parse(std::string(bytes.begin(), bytes.end()), f, error, Today())) {
      REXLOG_WARN("BF news: the feed from {} does not read: {} (the menu shows no news)", feed, error);
    } else {
      if (!error.empty()) REXLOG_WARN("BF news: the feed's notes: {}", error);
      REXLOG_INFO("BF news: feed from {}: {} items, {} coming up ({} ms)", feed, f.items.size(), f.upcoming.size(), ms);
      WriteWhole(dir / "feed.json", bytes);
      auto shared = std::make_shared<Feed>(std::move(f));
      {
        std::lock_guard lock(g_mutex);
        g_fresh = shared;
      }
      g_done = true;
      FetchPictures(dir, *shared);
      return;
    }
    g_done = true;
  }).detach();
}

std::shared_ptr<const Feed> Fresh() {
  std::lock_guard lock(g_mutex);
  return g_fresh;
}
std::shared_ptr<const Feed> Cached() {
  std::lock_guard lock(g_mutex);
  return g_cached;
}
bool Done() { return g_done.load(); }

}  // namespace bf1943::news
