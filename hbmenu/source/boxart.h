// Box art: found for each game on the console from ScreenScraper (screenscraper.fr): the box's front,
// back and side images and the game's details, kept in <data>/art/boxart/<game file name without
// extension>.png / .back / .spine and <data>/art/info/<game>.json.
//
// A game is looked up by its contents' CRC32 (cartridges) or its disc's serial (PS2), with its file name
// and size. Players need no account: the app asks as ScreenScraper's anonymous user, with the
// developer account of the build (source/ss_dev.h, not in the repository; tools/make_ss_dev.sh makes it
// from your own ScreenScraper developer ID). A build without it cannot download box art.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace boxart {

struct Game { std::string name, path; };  // file name (with extension) and its full path

struct System {
  const char* softname;   // the app's name for ScreenScraper ("SW2SNES")
  int ssSystemId;         // ScreenScraper's system: 3 NES, 4 SNES, 58 PS2
  // Disc systems: a game's serial ("SLUS-21008", from its disc) instead of a CRC32 of its contents.
  std::function<std::string(const Game&)> serialOf;
  std::string dataDir;    // the app's data folder (art goes in <dataDir>/art)
  // The bytes the CRC32 covers, from a game's file contents (e.g. without a copier header).
  std::function<void(const std::vector<uint8_t>& file, size_t& offset, size_t& length)> crcRange;
};


struct Status {
  std::atomic<bool> running{false};
  std::atomic<int> total{0}, done{0}, found{0}, missing{0};
  std::atomic<int> phase{0};  // (0: the games; no database download any more)
  std::atomic<int> gotMB{0}, sizeMB{0};
  char current[128] = "";  // the title being looked up now (phase 0)
  std::string message;  // the last outcome, for a toast (set when the run ends)
};
Status& status();

// Starts finding art for these games (those without art, or every one when `again`), in the
// background. readFile loads a game's contents (it may unpack a .zip). Returns false if already running.
bool start(const System& sys, std::vector<Game> games, bool again,
           std::function<bool(const Game&, std::vector<uint8_t>&)> readFile, int limit = 0);  // limit: at most this many games looked up (0: all)
// The app's exit: cancels a download in progress and waits for its thread (call before shutting down).
void stop();
// Where progress is logged (the app's debug log), from the scraper's thread.
void setLog(void (*fn)(const char* line));

// A game's details (<data>/art/info/<game>.json: title, region, developer, publisher, year, genre, bio,
// source) and its box's back and side images (JPEG or PNG).
std::string infoPath(const System& sys, const std::string& gameName);
std::string backPath(const System& sys, const std::string& gameName);
std::string spinePath(const System& sys, const std::string& gameName);
// Deletes the art of earlier builds (other sources) once; the app calls it at launch.
void cleanOldArt(const System& sys);

// A PS2 disc's serial ("SLUS-21008") from SYSTEM.CNF on the disc: an .iso, or a .cue / .bin (raw
// sectors); "" for others (.chd, .cso: compressed) or when not found.
std::string discSerial(const std::string& path);

// The art file for a game (may not exist yet), and whether it was looked for and not found.
std::string artPath(const System& sys, const std::string& gameName);
bool artMissing(const System& sys, const std::string& gameName);

// A game's region from its file name: "us", "eu" or "jp" (No-Intro "(USA)", "(Europe)", "(Japan)"
// and GoodTools "(U)", "(E)", "(J)" and the like); "us" when nothing says.
std::string region(const std::string& gameName);
// The TV system for the details panel: "NTSC", "PAL", "NTSC-J" or "World" from a name's region
// tags (No-Intro / Redump / GoodTools), "" when it has none.
std::string tvSystem(const std::string& gameName);
// The same from a PS2 disc serial's prefix (SLUS -> NTSC, SLES -> PAL, SLPS / SLPM -> NTSC-J).
std::string tvSystemOfSerial(const std::string& serial);

}  // namespace boxart
