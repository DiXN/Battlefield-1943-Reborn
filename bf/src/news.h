// The news window's content (TekRantGaming, 15 Nov 2026: "a news window that opens on the main menu at every launch,
// like the news screens other games use for their shops and battle passes ... the current state of the servers,
// upcoming things and our own news, with text, images and YouTube videos"; his pick: N1 "Dispatch Board",
// runs/ui-designs/build_news.py). docs/NEWS.md has the feed's format and how to post.
//
// The feed is one JSON file in the public GitHub repository, on its own branch (news), read from
// raw.githubusercontent.com: posting is a commit there, no client update and no server address in the game. A worker
// fetches it once a run (bf_news_feed names another one: a test address on this PC or a file to preview); a feed that
// reads is kept in the user cache (cache/news/feed.json) for the News row when the internet is away. The pictures
// (an item's image, else its video's YouTube thumbnail) follow into cache/news/, each once, from GitHub's and
// YouTube's picture hosts only. Nothing here blocks: the menu asks Get() each frame.

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bf1943::news {

struct Item {
  std::string id;        // unique, for "new since your last visit" (the title when the feed gives none)
  std::string kind;      // "news", "video", "update", "event": the default kicker's word
  std::string kicker;    // the small red line over the title ("Patch notes · 0.4.7"); empty: from kind and date
  std::string title;
  std::string text;
  std::string date;      // YYYY-MM-DD
  std::string image;     // a picture's https address (GitHub or YouTube hosts)
  std::string youtube;   // an 11 character YouTube video id
  std::string link;      // an https address A opens (the video's page when empty and there is a video)
  std::string button;    // the link button's label ("READ THE NOTES"); empty: WATCH ON YOUTUBE or OPEN
  int priority = 0;      // higher first
  bool lead = false;     // the lead story (else the first item after sorting)
};

struct Upcoming {
  std::string date;   // YYYY-MM-DD (shown as TUE 17; gone once past), or empty
  std::string label;  // shown instead of the date ("DEC", "SOON")
  std::string text;
};

struct Feed {
  std::string edition;  // the header's line ("15 Nov 2026 edition"); empty: the newest item's date
  std::string status;   // a line under the servers ("US Central: maintenance 02:00 UTC")
  std::vector<Item> items;          // shown ones only (from/until), sorted: lead first, then priority, then date
  std::vector<Upcoming> upcoming;   // future ones only, soonest first
};

// Reads a feed (the JSON in docs/NEWS.md). False with `error` set when it is not one; items without a title, and
// fields with addresses or ids the game will not use, are left out (each said in `error`'s log lines).
bool Parse(const std::string& json, Feed& out, std::string& error, const std::string& today);

// The worker, once a run (later calls do nothing).
void Start(const std::filesystem::path& cache_dir);
// The feed fetched this run (null until it arrives, or when it did not), and the last cached one (from an earlier
// run; null when there is none).
std::shared_ptr<const Feed> Fresh();
std::shared_ptr<const Feed> Cached();
// True once the worker has its answer (a feed or none).
bool Done();

// A picture's file in the cache (drawn once it is there), for an item: its image, else its video's thumbnail.
std::filesystem::path PictureFile(const std::filesystem::path& cache_dir, const Item& item);
std::string PictureUrl(const Item& item);

bool ValidYouTubeId(const std::string& id);
std::string WatchUrl(const std::string& youtube_id);
// "2026-11-15" as "15 NOV"; "TUE 17" for the coming up list. Empty when it is not a date.
std::string ShortDate(const std::string& iso);
std::string DayLabel(const std::string& iso);
// Today's date (UTC) as YYYY-MM-DD.
std::string Today();

}  // namespace bf1943::news
