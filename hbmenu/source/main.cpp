// SW2NES for hbmenu (Switch 2 homebrew, kaiHEN): NES emulator front end.
// A port of the CivHomebrew app (app/sw2nes.js): the same screens, settings,
// save formats and defaults, drawn with OpenGL ES at 1920x1080.
#include <switch.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "audio.h"
#include "core.h"

// The active emulation core (chosen per game start; see Settings "Core").
static const CoreApi* C = &QUICKNES_CORE;
#include "crt.h"
#include "gfx.h"
#include "json.h"
#include <minizip/unzip.h>

#ifndef APP_VERSION
#define APP_VERSION "2.0.0"
#endif

using std::string;
using std::vector;
using gfx::Color;

/* ---------------- Constants (as in sw2nes.js) ---------------- */
// ROM folder: sdmc:/roms/nes by default (the shared roms/<system> layout that
// RetroArch users and frontends use); any folder can be chosen in Settings.
static const char* DEFAULT_ROM_DIR = "sdmc:/roms/nes";
static std::string romDir = DEFAULT_ROM_DIR;
static const char* DATA_DIR = "sdmc:/switch/sw2nes";
static const double NES_RATE = 60.0988;
// Npad button bits (the same values the JS app uses).
enum { B_A = 1, B_B = 2, B_X = 4, B_Y = 8, B_L = 64, B_R = 128, B_PLUS = 1024, B_MINUS = 2048,
       B_LEFT = 4096, B_UP = 8192, B_RIGHT = 16384, B_DOWN = 32768, B_SL = 65536, B_SU = 131072, B_SR = 262144, B_SD = 524288 };
enum { N_A = 1, N_B = 2, N_SELECT = 4, N_START = 8, N_UP = 16, N_DOWN = 32, N_LEFT = 64, N_RIGHT = 128 };

static const uint32_t C_BG = 0x0b0d12, C_PANEL = 0x151924, C_LINE = 0x2a3142, C_TEXT = 0xeef1f7, C_DIM = 0x9aa3b5, C_ACCENT = 0xe4463b, C_SEL = 0x2b3550;
static Color col(uint32_t c, float a = 1) { return gfx::rgb(c, a); }

static const vector<string> ASPECTS = {"1:1", "8:7", "4:3", "Stretch"}, SCALES = {"Integer", "Fit"}, CROPS = {"Show all", "Hide sides", "Hide all edges"};
static const vector<string> CRT_STYLES = {"Off", "Scanlines", "Soft scanlines", "Bold scanlines", "Aperture grille", "Scanlines + grille",
                                          "Slot mask", "Shadow mask", "Dot mask", "LCD matrix", "Screen door"};
static const vector<int> CRT_CHOICES = {0, 1, 2, 3, 4, 5, 9, 10};
// Picture effects: rounded corners and vignette (all black-only, default off).
static const vector<string> CORNER_RADII = {"Off", "Small", "Medium", "Large", "Extra large"};
static const float CORNER_RADIUS_FRAC[5] = {0, 0.04f, 0.07f, 0.11f, 0.16f};  // of picture height
static const vector<string> CORNER_SOFTNESS = {"Sharp", "Low", "Medium", "High"};
static const float CORNER_SOFT_FRAC[4] = {0, 0.006f, 0.015f, 0.03f};  // of picture height ("Sharp" = 1 px anti-aliasing)
static const vector<string> VIGNETTES = {"Off", "Light", "Medium", "Strong"};
static const float VIGNETTE_STRENGTH[4] = {0, 0.3f, 0.5f, 0.75f};
static const vector<string> VIGNETTE_SIZES = {"Small", "Medium", "Large"};
static const float VIGNETTE_START[3] = {0.75f, 0.5f, 0.2f};  // where darkening begins (0 centre .. 1 corner)
// GPU shaders (romfs:/shaders). "None" keeps exact pixels and the CRT styles.
static const vector<string> SHADERS = {"None", "Sharp bilinear", "Bilinear", "Scale2x", "CRT-Geom", "CRT-Lottes", "zfast CRT", "CRT-Easymode", "CRT-Aperture", "LCD grid", "CRT-Royale"};
static const vector<string> SHADER_FILES = {"none.glsl", "sharp_bilinear.glsl", "bilinear.glsl", "scale2x.glsl", "crt_geom.glsl", "crt_lottes.glsl", "zfast_crt.glsl", "crt_easymode.glsl", "crt_aperture.glsl", "lcd_grid.glsl", "crt_royale.glsl"};
// Brightness boost (AutoHDR-style lift in SDR): exponent k of 1-(1-c)^k.
static const vector<string> BOOSTS = {"Off", "Low", "Medium", "High"};
static const float BOOST_K[4] = {1.0f, 1.3f, 1.6f, 2.0f};

static const vector<string> SIGNALS = {"RGB", "Composite", "Composite + dot crawl", "S-Video"};
static const vector<string> CURVATURES = {"Off", "Light", "Medium", "Strong"};
static const float CURVATURE[4] = {0, 0.03f, 0.055f, 0.09f};
// Latency and playback options (all games).
static const vector<string> RUN_AHEADS = {"Off", "1 frame", "2 frames"};
static const vector<string> FRAME_DELAYS = {"Off", "4 ms", "8 ms"};  // 12 ms overran 60 Hz refreshes
static const int FRAME_DELAY_MS[3] = {0, 4, 8};
static const vector<string> AUDIO_BUFFERS = {"Small", "Normal", "Large"};
static const int AUDIO_TARGET_FRAMES[3] = {1920, 2880, 4320};  // queued audio: 40 / 60 / 90 ms at 48 kHz
static const vector<string> DISPLAY_MODES = {"60 Hz", "120 Hz / VRR"};
static const vector<string> FF_MODES = {"Off", "Hold ZR", "Toggle ZR"};
static const vector<string> FF_SPEEDS = {"1.5x", "2x", "3x", "4x"};
static const double FF_FACTOR[4] = {1.5, 2, 3, 4};
static const vector<string> STATS_LEVELS = {"Off", "FPS", "Full", "Full + graph"};
static const vector<string> OFF_ON = {"Off", "On"};
static const vector<string> FACE_KEYS = {"A", "B", "X", "Y"}, FACE_CHOICES = {"NES A", "NES B", "Turbo A", "Turbo B", "None"};
static const int FACE_BITS[4] = {B_A, B_B, B_X, B_Y}, FACE_TARGETS[2] = {N_A, N_B}, FACE_DEFAULT[4] = {0, 0, 1, 1};
static const vector<string> TURBO_RATES = {"10 Hz", "15 Hz", "20 Hz", "30 Hz"};
static const int TURBO_PERIOD[4] = {6, 4, 3, 2};
static const vector<string> PALETTES = {"Default", "Vivid", "Natural", "Warm", "Soft", "Greyscale"};
static const double PALETTE_PARAMS[6][5] = {{0, 0, 0, 0, 0}, {4.0, 2.05, 1.0, 0, 1}, {4.0, 1.4, 1.15, 0, 1}, {4.35, 1.6, 1.0, 0.01, 1}, {4.0, 1.2, 0.95, 0.04, 0.9}, {4.0, 0, 1.0, 0, 1}};
static const vector<string> VOLUMES = {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};
static const vector<string> FILTERS = {"Off", "Soft", "Strong"};
static const vector<string> LIST_VIEWS = {"All", "Favourites", "Recent"};
static const int RECENT_MAX = 5, SLOT_COUNT = 4;

/* ---------------- State ---------------- */
// Defaults (the author's chosen setup, 2026-11-14).
static int aspectMode = 2, scaleMode = 0, cropMode = 2, crtStyle = 0, paletteMode = 0, statsLevel = 0;
static int runAhead = 1, frameDelay = 2, audioBuffer = 0, displayMode = 1, ffMode = 1, ffSpeed = 1;
static bool rewindOn = true;
// Emulation core: QuickNES (fast, few mappers) or Nestopia UE (accurate, nearly all mappers).
static const vector<string> CORES = {"QuickNES", "Nestopia"};
static int coreSel = 1;
// Advanced options: hidden by default, leaving what a console's own emulator offers.
static bool showAdvanced = false;  // default: hidden (also after Reset all settings)
static const vector<string> ADVANCED_SETTINGS = {"Core", "Scale", "Overscan", "Signal", "Curvature", "Corner radius", "Corner softness", "Vignette", "Vignette size",
    "Brightness boost", "Stats", "Run-ahead", "Frame delay (60 Hz)", "Audio buffer", "Display", "Fast-forward speed", "Dim game in menu", "Audio filter", "Turbo speed"};
static const vector<string> ADVANCED_MENU = {"Run-ahead", "Stats", "Scale", "Overscan", "Signal", "Curvature", "Corner radius", "Corner softness", "Vignette",
    "Vignette size", "Brightness boost", "Settings for", "Save screenshot", "Cheats..."};
static int cornerRadius = 1, cornerSoftness = 1, vignette = 1, vignetteSize = 0, curvature = 0, shader = 6, signalMode = 0, boost = 0;
// Face button mapping per player (Switch A, B, X, Y -> FACE_CHOICES).
static int faceMap[4] = {0, 0, 1, 1}, faceMap2[4] = {0, 0, 1, 1};
static int remapPlayer = 0;  // Settings: whose buttons the mapping rows edit (not saved)
static int* curMap() { return remapPlayer ? faceMap2 : faceMap; }
static int turboRate = 1, volumeLevel = 10, audioFilter = 0;
static bool autoLoadState = false, pauseInMenu = false, dimInMenu = false;
// At most one frame in flight: input is read only after the previous frame
// finished on the GPU (the driver default let frames queue up: 100+ ms lag).
static const int frameQueue = 1;
static vector<string> recentGames, favourites;

struct Rom { string name, dir; };  // file name (.nes or .zip) and its folder
static vector<Rom> allRoms, roms;
static int selected = 0, top = 0, listView = 0, btnIndex = 0;
static string focusArea = "list";
static double exitArmed = 0;

struct GameVals { int aspect, scale, crop, crt, palette, faces[4], faces2p[4], turbo, volume, filter, corner, cornerSoft, vignette, vignetteSize, curve, shader, signal, boost; };
struct Cheat { string code, desc; bool on; };
struct Patch { long offset; uint8_t orig; };
struct Running {
  string name, base;
  bool battery = false, perGame = false;
  uint32_t sramHash = 0;
  double sramCheck = 0;
  GameVals globalVals;
  Json slots = Json::object();
  vector<Cheat> cheats;
  vector<Patch> patches;
};
static Running* running = nullptr;
static string mode = "browser";
static bool quitApp = false;
static int stateSlot = 1, turbo = 0, paletteApplied = -1;

// UI state
static double toastUntil = 0, hintUntil = 0;
static string toastText, messageText;
static std::function<void()> messageThen;
static int settingsIndex = 0, settingsTop = 0;

static double nowMs() { return armTicksToNs(armGetSystemTick()) / 1e6; }
static FILE* logFile = nullptr;
static void record(const char* fmt, ...) {
  if (!logFile) return;
  va_list ap; va_start(ap, fmt); vfprintf(logFile, fmt, ap); va_end(ap); fputc('\n', logFile); fflush(logFile);
}

/* ---------------- Files ---------------- */
static bool readFile(const string& path, vector<uint8_t>& out, long limit = 4 * 1024 * 1024) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n < 0 || n > limit) { fclose(f); return false; }
  out.resize(n);
  bool ok = n == 0 || fread(out.data(), 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}
static bool readText(const string& path, string& out) {
  vector<uint8_t> d;
  if (!readFile(path, d, 1024 * 1024)) return false;
  out.assign(d.begin(), d.end());
  return true;
}
// Writes via a temporary file and rename, so a crash never leaves half a save.
static bool writeFile(const string& path, const void* data, size_t n) {
  string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, n, f) == n;
  ok = fclose(f) == 0 && ok;
  if (!ok) { remove(tmp.c_str()); return false; }
  remove(path.c_str());
  return rename(tmp.c_str(), path.c_str()) == 0;
}
static bool writeText(const string& path, const string& text) { return writeFile(path, text.data(), text.size()); }
static bool exists(const string& path) { struct stat st; return stat(path.c_str(), &st) == 0; }

static string lower(string s) { for (auto& c : s) c = tolower((unsigned char)c); return s; }
static bool hasExt(const string& n, const char* ext) { return n.size() > 4 && lower(n.substr(n.size() - 4)) == ext; }
static string stripNes(const string& n) { return hasExt(n, ".nes") || hasExt(n, ".zip") ? n.substr(0, n.size() - 4) : n; }
static string sanitize(const string& name) {
  string s = stripNes(name), out;
  for (char c : s) out += (isalnum((unsigned char)c) || c == '.' || c == '_' || c == ' ' || c == '-') ? c : '_';
  if (out.size() > 80) out.resize(80);
  return out.empty() ? "game" : out;
}
static bool contains(const vector<string>& v, const string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
static int indexOf(const vector<string>& v, const string& s) { auto it = std::find(v.begin(), v.end(), s); return it == v.end() ? -1 : (int)(it - v.begin()); }

/* ---------------- Settings ---------------- */
static string SETTINGS() { return string(DATA_DIR) + "/settings.json"; }
static GameVals gameValues() {
  GameVals g = {aspectMode, scaleMode, cropMode, crtStyle, paletteMode, {faceMap[0], faceMap[1], faceMap[2], faceMap[3]}, {faceMap2[0], faceMap2[1], faceMap2[2], faceMap2[3]}, turboRate, volumeLevel, audioFilter,
                cornerRadius, cornerSoftness, vignette, vignetteSize, curvature, shader, signalMode, boost};
  return g;
}
static int pick(const Json* x, int n, int d) { return x && x->isNum() && x->n >= 0 && x->n < n ? (int)x->n : d; }
static void setGameValues(const GameVals& g) {
  aspectMode = g.aspect; scaleMode = g.scale; cropMode = g.crop; crtStyle = g.crt; paletteMode = g.palette;
  for (int i = 0; i < 4; i++) { faceMap[i] = g.faces[i]; faceMap2[i] = g.faces2p[i]; }
  turboRate = g.turbo; volumeLevel = g.volume; audioFilter = g.filter;
  cornerRadius = g.corner; cornerSoftness = g.cornerSoft; vignette = g.vignette; vignetteSize = g.vignetteSize; curvature = g.curve; shader = g.shader; signalMode = g.signal; boost = g.boost;
}
static void setGameValuesJson(const Json& v) {
  aspectMode = pick(v.get("aspect"), ASPECTS.size(), aspectMode); scaleMode = pick(v.get("scale"), SCALES.size(), scaleMode); cropMode = pick(v.get("crop"), CROPS.size(), cropMode);
  const Json* c = v.get("crt");
  (void)c;  // the CRT strip styles were replaced by shaders (2.5.0): old values are ignored
  paletteMode = pick(v.get("palette"), PALETTES.size(), paletteMode); turboRate = pick(v.get("turbo"), TURBO_RATES.size(), turboRate);
  volumeLevel = pick(v.get("volume"), VOLUMES.size(), volumeLevel); audioFilter = pick(v.get("filter"), FILTERS.size(), audioFilter);
  cornerRadius = pick(v.get("corner"), CORNER_RADII.size(), cornerRadius); cornerSoftness = pick(v.get("cornerSoft"), CORNER_SOFTNESS.size(), cornerSoftness);
  vignette = pick(v.get("vignette"), VIGNETTES.size(), vignette); vignetteSize = pick(v.get("vignetteSize"), VIGNETTE_SIZES.size(), vignetteSize);
  curvature = pick(v.get("curvature"), CURVATURES.size(), curvature);
  shader = pick(v.get("shader"), SHADERS.size(), shader);
  signalMode = pick(v.get("signal"), SIGNALS.size(), signalMode);
  boost = pick(v.get("boost"), BOOSTS.size(), boost);
  const Json* f = v.get("faces");
  if (f && f->isArr() && f->a.size() == 4) for (int i = 0; i < 4; i++) faceMap[i] = pick(&f->a[i], FACE_CHOICES.size(), FACE_DEFAULT[i]);
  const Json* f2 = v.get("facesP2");
  if (f2 && f2->isArr() && f2->a.size() == 4) for (int i = 0; i < 4; i++) faceMap2[i] = pick(&f2->a[i], FACE_CHOICES.size(), FACE_DEFAULT[i]);
}
static Json gameJson(const GameVals& g) {
  Json j = Json::object();
  j.set("aspect", Json::number(g.aspect)); j.set("scale", Json::number(g.scale)); j.set("crop", Json::number(g.crop));
  j.set("crt", Json::number(g.crt)); j.set("palette", Json::number(g.palette));
  Json f = Json::array(); for (int i = 0; i < 4; i++) f.a.push_back(Json::number(g.faces[i]));
  j.set("faces", f);
  Json f2 = Json::array(); for (int i = 0; i < 4; i++) f2.a.push_back(Json::number(g.faces2p[i]));
  j.set("facesP2", f2); j.set("turbo", Json::number(g.turbo)); j.set("volume", Json::number(g.volume)); j.set("filter", Json::number(g.filter));
  j.set("corner", Json::number(g.corner)); j.set("cornerSoft", Json::number(g.cornerSoft)); j.set("vignette", Json::number(g.vignette)); j.set("vignetteSize", Json::number(g.vignetteSize)); j.set("curvature", Json::number(g.curve)); j.set("shader", Json::number(g.shader)); j.set("signal", Json::number(g.signal)); j.set("boost", Json::number(g.boost));
  return j;
}
static Json strings(const vector<string>& v) { Json a = Json::array(); for (auto& s : v) a.a.push_back(Json::string(s)); return a; }

static Json settingsJson(const GameVals& g) {
  Json j = Json::object();
  j.set("v", Json::number(6)); j.set("crt", Json::number(g.crt)); j.set("stats", Json::boolean(statsLevel > 0)); j.set("statsLevel", Json::number(statsLevel));
  j.set("aspect", Json::number(g.aspect)); j.set("scale", Json::number(g.scale)); j.set("crop", Json::number(g.crop));
  Json f = Json::array(); for (int i = 0; i < 4; i++) f.a.push_back(Json::number(g.faces[i]));
  j.set("faces2", f);
  Json f2 = Json::array(); for (int i = 0; i < 4; i++) f2.a.push_back(Json::number(g.faces2p[i]));
  j.set("facesP2", f2); j.set("turbo", Json::number(g.turbo)); j.set("palette", Json::number(g.palette));
  j.set("volume", Json::number(g.volume)); j.set("filter", Json::number(g.filter));
  j.set("corner", Json::number(g.corner)); j.set("cornerSoft", Json::number(g.cornerSoft)); j.set("vignette", Json::number(g.vignette)); j.set("vignetteSize", Json::number(g.vignetteSize)); j.set("curvature", Json::number(g.curve)); j.set("shader", Json::number(g.shader)); j.set("signal", Json::number(g.signal)); j.set("boost", Json::number(g.boost));
  j.set("recent", strings(recentGames)); j.set("favourites", strings(favourites));
  j.set("romDir", Json::string(romDir));
  j.set("advanced", Json::boolean(showAdvanced)); j.set("core", Json::number(coreSel));
  j.set("runAhead", Json::number(runAhead)); j.set("frameDelay", Json::number(frameDelay)); j.set("audioBuffer", Json::number(audioBuffer));
  j.set("display", Json::number(displayMode)); j.set("rewind", Json::boolean(rewindOn)); j.set("ffMode", Json::number(ffMode)); j.set("ffSpeed", Json::number(ffSpeed));
  j.set("autoLoad", Json::boolean(autoLoadState)); j.set("pauseMenu", Json::boolean(pauseInMenu)); j.set("dimMenu", Json::boolean(dimInMenu));
  return j;
}
static void saveSettings() {
  GameVals g = running && running->perGame ? running->globalVals : gameValues();
  if (running && running->perGame && !writeText(running->base + ".settings.json", gameJson(gameValues()).dump())) record("Game settings save failed");
  if (!writeText(SETTINGS(), settingsJson(g).dump())) record("Settings save failed");
}
static void applySettingsJson(const Json& v) {
  Json gv = Json::object();
  if (const Json* f = v.get("faces2")) gv.set("faces", *f);
  if (const Json* f = v.get("facesP2")) gv.set("facesP2", *f);
  for (const char* k : {"turbo", "palette", "volume", "filter", "aspect", "scale", "crop", "corner", "cornerSoft", "vignette", "vignetteSize", "curvature", "shader", "signal", "boost"}) if (const Json* x = v.get(k)) gv.set(k, *x);
  setGameValuesJson(gv);
  if (const Json* r = v.get("recent")) if (r->isArr()) { recentGames.clear(); for (auto& x : r->a) if (x.isStr() && (int)recentGames.size() < RECENT_MAX) recentGames.push_back(x.s); }
  if (const Json* r = v.get("favourites")) if (r->isArr()) { favourites.clear(); for (auto& x : r->a) if (x.isStr()) favourites.push_back(x.s); }
  if (const Json* b = v.get("autoLoad")) if (b->isBool()) autoLoadState = b->b;
  if (const Json* b = v.get("pauseMenu")) if (b->isBool()) pauseInMenu = b->b;
  if (const Json* b = v.get("dimMenu")) if (b->isBool()) dimInMenu = b->b;
  statsLevel = pick(v.get("statsLevel"), STATS_LEVELS.size(), statsLevel);
  if (const Json* b = v.get("advanced")) if (b->isBool()) showAdvanced = b->b;
  if (const Json* r = v.get("romDir")) if (r->isStr() && !r->s.empty()) romDir = r->s;
  coreSel = pick(v.get("core"), CORES.size(), coreSel);
  runAhead = pick(v.get("runAhead"), RUN_AHEADS.size(), runAhead); frameDelay = pick(v.get("frameDelay"), FRAME_DELAYS.size(), frameDelay);
  audioBuffer = pick(v.get("audioBuffer"), AUDIO_BUFFERS.size(), audioBuffer); displayMode = pick(v.get("display"), DISPLAY_MODES.size(), displayMode);
  ffMode = pick(v.get("ffMode"), FF_MODES.size(), ffMode); ffSpeed = pick(v.get("ffSpeed"), FF_SPEEDS.size(), ffSpeed);
  if (const Json* b = v.get("rewind")) if (b->isBool()) rewindOn = b->b;
  // ("crt": the CRT strip styles were replaced by shaders in 2.5.0; old values are ignored.)
}
static Json defaultSettings;  // the built-in defaults, captured before the settings file is read
static bool displaySaved = false;  // the file chose a display mode (otherwise 120 Hz only where available)
static void loadSettings() {
  defaultSettings = settingsJson(gameValues());
  string t; Json v;
  if (!readText(SETTINGS(), t) || !Json::parse(t, v) || v.type != Json::OBJ) return;
  applySettingsJson(v);
  displaySaved = v.get("display") != nullptr;
  if (const Json* d = v.get("frameDelay")) if (d->isNum() && d->n >= FRAME_DELAYS.size()) frameDelay = (int)FRAME_DELAYS.size() - 1;  // 12 ms -> 8 ms cap
}
// Settings -> Reset all settings: back to the built-in defaults (recent games,
// favourites and per-game settings files are kept).
static void resetAllSettings() {
  vector<string> rec = recentGames, fav = favourites;
  applySettingsJson(defaultSettings);
  recentGames = rec; favourites = fav;
  saveSettings();
}

/* ---------------- ROM list ---------------- */
static bool naturalLess(const string& a0, const string& b0) {
  string a = lower(a0), b = lower(b0);
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
      size_t i2 = i, j2 = j;
      while (i2 < a.size() && isdigit((unsigned char)a[i2])) i2++;
      while (j2 < b.size() && isdigit((unsigned char)b[j2])) j2++;
      double x = atof(a.substr(i, i2 - i).c_str()), y = atof(b.substr(j, j2 - j).c_str());
      if (x != y) return x < y;
      i = i2; j = j2; continue;
    }
    if (a[i] != b[j]) return a[i] < b[j];
    i++; j++;
  }
  return a.size() - i < b.size() - j;
}
static void orderRoms() {
  string cur = selected < (int)roms.size() ? roms[selected].name : "";
  vector<Rom> list;
  if (listView == 1) {
    for (auto& r : allRoms) if (contains(favourites, r.name)) list.push_back(r);
    std::sort(list.begin(), list.end(), [](const Rom& a, const Rom& b) { return naturalLess(a.name, b.name); });
  } else if (listView == 2) {
    for (auto& n : recentGames) for (auto& r : allRoms) if (r.name == n) { list.push_back(r); break; }
  } else {
    list = allRoms;
    auto rank = [](const Rom& r) -> std::pair<int, int> {
      if (contains(favourites, r.name)) return {0, 0};
      int k = indexOf(recentGames, r.name);
      return k >= 0 ? std::make_pair(1, k) : std::make_pair(2, 0);
    };
    std::stable_sort(list.begin(), list.end(), [&](const Rom& a, const Rom& b) {
      auto x = rank(a), y = rank(b);
      if (x != y) return x < y;
      return naturalLess(a.name, b.name);
    });
  }
  roms = list; selected = 0; top = 0;
  if (!cur.empty()) for (size_t i = 0; i < roms.size(); i++) if (roms[i].name == cur) selected = (int)i;
}
// Time played per game (seconds, by file name) in playtime.json: counted while
// a game runs (also behind an unpaused menu), saved every 5 minutes, when a
// game is quit and when SW2NES exits.
static Json playtime = Json::object();
static double playAccumSec = 0, playSavedAt = 0;
static string PLAYTIME() { return string(DATA_DIR) + "/playtime.json"; }
static void loadPlaytime() {
  string t; Json j;
  if (readText(PLAYTIME(), t) && Json::parse(t, j) && j.type == Json::OBJ) playtime = j;
}
static double playSeconds(const string& name) { const Json* v = playtime.get(name); return v && v->isNum() ? v->n : 0; }
static string playLabel(const string& name) {
  double sec = playSeconds(name);
  if (sec <= 0) return "";
  if (sec < 3600) return "< 1 hour";
  int h = (int)(sec / 3600);
  return h == 1 ? "1 hour" : std::to_string(h) + " hours";
}
static void flushPlaytime(const string& name) {
  if (playAccumSec <= 0 || name.empty()) return;
  playtime.set(name, Json::number(round(playSeconds(name) + playAccumSec)));
  playAccumSec = 0;
  if (!writeText(PLAYTIME(), playtime.dump())) record("Play time save failed");
}
static void scan() {
  allRoms.clear();
  // One entry per game name (a .nes file wins over a .zip of the same name).
  for (const string& dir : {romDir}) {
    vector<Rom> found;
    if (DIR* d = opendir(dir.c_str())) {
      while (struct dirent* e = readdir(d)) {
        string n = e->d_name;
        if (e->d_type == DT_DIR || n[0] == '.') continue;
        if (hasExt(n, ".nes") || hasExt(n, ".zip")) found.push_back(Rom{n, dir});
      }
      closedir(d);
    }
    std::sort(found.begin(), found.end(), [](const Rom& a, const Rom& b) { return hasExt(a.name, ".nes") && !hasExt(b.name, ".nes"); });
    for (auto& r : found) {
      bool dup = false;
      for (auto& o : allRoms) if (lower(stripNes(o.name)) == lower(stripNes(r.name))) dup = true;
      if (!dup) allRoms.push_back(r);
    }
  }
  orderRoms();
  if (selected >= (int)roms.size()) selected = std::max(0, (int)roms.size() - 1);
}

/* ---------------- Palettes ---------------- */
// NES composite signal -> YIQ -> RGB for all 512 colours (incl. emphasis).
static void ntscPalette(double hue, double sat, double gamma, double bright, double contrast, uint8_t* out) {
  const double black = 0.518, white = 1.962, att = 0.746, levels[8] = {0.350, 0.518, 0.962, 1.550, 1.094, 1.506, 1.962, 1.962};
  for (int px = 0; px < 512; px++) {
    int color = px & 15, level = (px >> 4) & 3, emph = px >> 6;
    if (color > 13) level = 1;
    double lo = levels[level], hi = levels[4 + level];
    if (color == 0) lo = hi;
    if (color > 12) hi = lo;
    double y = 0, i = 0, q = 0;
    for (int ph = 0; ph < 12; ph++) {
      auto inPhase = [&](int c) { return (c + ph) % 12 < 6; };
      double sig = inPhase(color) ? hi : lo;
      if (((emph & 1) && inPhase(0)) || ((emph & 2) && inPhase(4)) || ((emph & 4) && inPhase(8))) sig *= att;
      double v = (sig - black) / (white - black);
      y += v; i += v * cos(M_PI * (ph + hue) / 6); q += v * sin(M_PI * (ph + hue) / 6);
    }
    y = y / 12 * contrast + bright; i = i / 12 * sat; q = q / 12 * sat;
    double rgb[3] = {y + 0.946882 * i + 0.623557 * q, y - 0.274788 * i - 0.635691 * q, y - 1.108545 * i + 1.709007 * q};
    for (int k = 0; k < 3; k++) out[px * 3 + k] = (uint8_t)lround(255 * pow(std::max(0.0, std::min(1.0, rgb[k])), gamma));
  }
}
static void applyEmuOptions() {
  if (!running) return;
  if (paletteApplied != paletteMode) {
    if (paletteMode > 0) {
      const double* p = PALETTE_PARAMS[paletteMode];
      ntscPalette(p[0], p[1], p[2], p[3], p[4], C->palette_buffer());
      C->use_palette(1);
    } else C->use_palette(0);
    paletteApplied = paletteMode;
    C->reconvert();  // show the new colours now (the bridge skips this before the first frame)
  }
  C->set_audio((int)lround(volumeLevel * 25.6), audioFilter);
}

/* ---------------- Picture geometry and CRT ---------------- */
static gfx::Picture* picture = nullptr;
static int W = 256, H = 224;
static int picX = 0, picY = 0, picW = 0, picH = 0, picCX = 0, picCY = 0, picCW = 0, picCH = 0;
static double picSX = 0, picSY = 0;
static string crtKey;
// Port of fitPicture(): Integer = whole-number pixel sizes closest to the
// aspect; Fit = largest exact aspect; crop offsets the image.
static void fitPicture() {
  int vw = gfx::SCREEN_W, vh = gfx::SCREEN_H;
  int cx = cropMode >= 1 ? 8 : 0, cy = cropMode >= 2 ? 8 : 0, cw = W - 2 * cx, ch = H - 2 * cy;
  double par = aspectMode == 1 ? 8.0 / 7.0 : aspectMode == 2 ? (4.0 / 3.0) * ch / cw : 1, sx, sy;
  if (scaleMode == 0 && vh >= ch && vw >= cw) {
    int maxX = vw / cw, maxY = vh / ch;
    if (aspectMode == 3) { sx = maxX; sy = maxY; }
    else { sy = maxY; sx = std::max(1, std::min(maxX, (int)lround(sy * par))); if (sx * cw > vw) sx = maxX; }
  } else if (aspectMode == 3) { sx = (double)vw / cw; sy = (double)vh / ch; }
  else { double fit = std::min(vw / (cw * par), (double)vh / ch); sy = fit; sx = fit * par; }
  int w = (int)lround(cw * sx), h = (int)lround(ch * sy);
  picX = (vw - w) / 2; picY = (vh - h) / 2; picW = w; picH = h; picSX = sx; picSY = sy;
  picCX = cx; picCY = cy; picCW = cw; picCH = ch;
}
// NTSC: the shown frame's NES colour numbers (incl. emphasis) per pixel.
static double emuFrames = 0;
static void uploadColors() {
  if (!picture || !running) return;
  gfx::pictureUploadColors(picture, C->color_frame(), (float)emuFrames);
}
static void applyCrt() {
  if (!picture) return;
  gfx::pictureSetShader(picture, shader);
  gfx::pictureSetBoost(picture, BOOST_K[boost]);
  {
    // NTSC decode uses the chosen palette's parameters (Default = QuickNES's own decode).
    const double* pp = PALETTE_PARAMS[paletteMode];
    float dec[4] = {paletteMode ? (float)pp[0] : 4.0f, paletteMode ? (float)pp[1] : 1.55f, paletteMode ? (float)pp[3] : 0.0f, paletteMode ? (float)pp[4] : 1.0f};
    gfx::pictureSetSignal(picture, signalMode, dec, paletteMode ? (float)pp[2] : 1.0f);
    if (signalMode && running) uploadColors();
  }
  gfx::pictureSetEffects(picture, picH * CORNER_RADIUS_FRAC[cornerRadius], std::max(1.0f, picH * CORNER_SOFT_FRAC[cornerSoftness]),
                         VIGNETTE_STRENGTH[vignette], VIGNETTE_START[vignetteSize], CURVATURE[curvature]);
  char key[200];
  snprintf(key, sizeof key, "%d,%d,%d,%.6f,%.6f,%d,%d", crtStyle, picW, picH, picSX, picSY, picCX, picCY);
  if (crtKey == key) return;
  crtKey = key;
  if (crtStyle <= 0) { gfx::pictureSetCrt(picture, nullptr, 0, nullptr, 0); return; }
  vector<uint8_t> rows, cols;
  crt::strips(crtStyle, picW, picH, picSX, picSY, picCX, picCY, rows, cols);
  gfx::pictureSetCrt(picture, rows.data(), picH, cols.data(), picW);
}
// Recolours the shown frame now (palette change while paused in the menu).
static void refreshPicture() { if (running && picture) { applyEmuOptions(); gfx::pictureUpload(picture, C->video()); applyCrt(); } }
static string scaleLabel(int i) {
  if (i == 0 && scaleMode == 0 && picSX == (int)picSX && picSY == (int)picSY && picSX > 0) return SCALES[0] + " " + std::to_string((int)picSX) + "x" + std::to_string((int)picSY);
  return SCALES[i];
}

/* ---------------- Toasts and messages ---------------- */
static void showToast(const string& t, double ms = 1800) { toastText = t; toastUntil = nowMs() + ms; }
static void showMessage(const string& text, std::function<void()> then) { messageText = text; messageThen = then; mode = "message"; }

/* ---------------- Timing and stats ---------------- */
static double shownTimes[601];
static int shownHead = 0, shownCount = 0;
static void noteShown(double t) { shownTimes[shownHead] = t; shownHead = (shownHead + 1) % 601; if (shownCount < 601) shownCount++; }
static void videoFps(double& avg, double& low) {
  avg = low = 0;
  if (shownCount < 3) return;
  int n = shownCount, L = 601;
  double first = shownTimes[(shownHead - n + L) % L], last = shownTimes[(shownHead - 1 + L) % L];
  int k = std::max(1, (n - 1) / 100);
  vector<double> gaps;
  for (int i = 1; i < n; i++) gaps.push_back(shownTimes[(shownHead - n + i + L) % L] - shownTimes[(shownHead - n + i - 1 + L) % L]);
  std::partial_sort(gaps.begin(), gaps.begin() + k, gaps.end(), std::greater<double>());
  double worst = 0; for (int i = 0; i < k; i++) worst += gaps[i];
  avg = (n - 1) * 1000 / (last - first); low = 1000 / (worst / k);
}
// Frame-time graph: 240 frames, 2 px each, scrolling left; 16.7 ms in the middle.
enum { GRAPH_STEP = 2, GRAPH_FRAMES = 240, GRAPH_W = GRAPH_STEP * GRAPH_FRAMES, GRAPH_H = 40 };
static const double GRAPH_MAX_MS = 33.4;
static uint32_t graphPix[GRAPH_W * GRAPH_H];
static gfx::Image* graphImage = nullptr;
static int graphPrevY = -1;
static void resetGraph() {
  graphPrevY = -1;
  int mid = GRAPH_H - (int)lround(16.7 / GRAPH_MAX_MS * GRAPH_H);
  for (int i = 0; i < GRAPH_W * GRAPH_H; i++) graphPix[i] = 0;
  for (int x = 0; x < GRAPH_W; x++) graphPix[mid * GRAPH_W + x] = 0x4dffffff;
}
static void drawGraph(double ms) {
  int S = GRAPH_STEP, mid = GRAPH_H - (int)lround(16.7 / GRAPH_MAX_MS * GRAPH_H);
  int y = GRAPH_H - 1 - (int)lround(std::min(ms, GRAPH_MAX_MS) / GRAPH_MAX_MS * (GRAPH_H - 2));
  int y0 = graphPrevY < 0 ? y : graphPrevY, lo = std::min(y0, y), hi = std::max(y0, y);
  for (int r = 0; r < GRAPH_H; r++) {
    uint32_t* row = graphPix + r * GRAPH_W;
    memmove(row, row + S, (GRAPH_W - S) * 4);
    for (int x = GRAPH_W - S; x < GRAPH_W; x++) row[x] = (r >= lo && r <= hi) ? 0xff6ee26e : r == mid ? 0x4dffffff : 0;
  }
  graphPrevY = y;
  if (graphImage) gfx::imageUpload(graphImage, graphPix);
}

static double phase[5] = {0, 0, 0, 0, 0};  // per-second sums: fence wait, frame start (display wait), emulation, drawing, swap
static int phaseN = 0, sessionLog = 0;
static double swapAcc = 0, waitAcc = 0, swapMax = 0;
static int swapN = 0;
static double lastSwap = 0, tickMs = 1000.0 / 60, audioRatio = 1, pendEma = 2, statsAt = 0, workMs = 0, startedAt = 0;
static int emuCount = 0, ratioTicks = 0;
static string statsText;
static bool comboLatch = false, frameDrawn = false;

// Rewind history: a snapshot every frame (slots 1..REWIND_CAP, 20 s), stepped
// back one per refresh (real-time speed backwards). Slot 0 is unused.
enum { REWIND_CAP = 1200, REWIND_EVERY = 1 };
static bool snapsReady = false;
static int rewindHead = 0, rewindCount = 0, rewindTick = 0;
static bool ffLatched = false, ffPrevHeld = false, rewinding = false, fastForward = false;
static double emuAcc = 0, ffAcc = 0, audioInteg = 0, loopGapMs = 16.69, loopStartPrev = 0, vrrNext = 0;
// The screen's refresh period when the loop is not paced by the game (menus):
// 120 Hz / VRR timing is used only when the screen really runs faster than 60 Hz.
static double freeTickMs = 0;
// VI2's report of the display (polled about once a second): the 120 Hz / VRR
// timing is used only while the display really is at 120 Hz or has VRR active.
static unsigned dispHz = 0;
static bool dispVrr = false, dispCan120 = false, dispKnown = false;
static double dispPolled = 0;
static void pollDisplay(double t) {
  if (dispPolled && t - dispPolled < 1000) return;
  dispPolled = t;
  unsigned hz; bool vrr, can;
  bool ok = gfx::displayInfo(hz, vrr, can);
  if (ok && (!dispKnown || hz != dispHz || vrr != dispVrr || can != dispCan120)) record("display: %u Hz, VRR %s, 120 Hz modes %s", hz, vrr ? "on" : "off", can ? "yes" : "no");
  if (ok) { dispHz = hz; dispVrr = vrr; dispCan120 = can; }
  dispKnown = ok;
}
static bool highRefresh() {
  if (displayMode != 1) return false;
  if (dispKnown) return dispHz >= 120 || dispVrr;
  return freeTickMs > 0 && freeTickMs < 12;  // fallback: measured refresh in menus
}
static void resetRewind() { rewindHead = rewindCount = rewindTick = 0; }
static void setAudioRatio(double r) { if (r != audioRatio) { audioRatio = r; C->set_audio_ratio(r); } }
static void resetClock() { shownHead = shownCount = 0; resetGraph(); lastSwap = 0; }

/* ---------------- Battery saves and save states ---------------- */
static uint32_t hashSram() {
  uint32_t h = 2166136261u; uint8_t* p = C->sram(); int n = C->sram_size();
  for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void writeSram(uint32_t h) {
  if (writeFile(running->base + ".srm", C->sram(), C->sram_size())) running->sramHash = h;
  else { record("SRAM save failed"); showToast("Could not save game data"); }
}
static void saveSram(bool force) {
  if (!running || !running->battery) return;
  uint32_t h = hashSram();
  if (!force && h == running->sramHash) return;
  writeSram(h);
}
// Save states are per core (their formats differ): QuickNES <game>.state[N], Nestopia <game>.nst.state[N].
static string statePath(int n) { return running->base + C->stateTag + (n == 1 ? ".state" : ".state" + std::to_string(n)); }
static string slotsPath() { return running->base + C->stateTag + ".slots.json"; }
static double slotTime(int n) { const Json* t = running ? running->slots.get(std::to_string(n)) : nullptr; return t && t->isNum() ? t->n : 0; }
static string slotLabel(int n) {
  double t = slotTime(n);
  if (!t) return std::to_string(n) + "  (empty)";
  time_t s = (time_t)(t / 1000), nowT = time(nullptr);
  struct tm d = *localtime(&s), today = *localtime(&nowT);
  char buf[40];
  if (d.tm_year == today.tm_year && d.tm_yday == today.tm_yday) snprintf(buf, sizeof buf, "%02d:%02d", d.tm_hour, d.tm_min);
  else snprintf(buf, sizeof buf, "%02d/%02d %02d:%02d", d.tm_mday, d.tm_mon + 1, d.tm_hour, d.tm_min);
  return std::to_string(n) + "  " + buf;
}
static void saveState() {
  long len = C->state_save();
  if (!len) { showToast("State save failed: Emulator could not create a save state."); return; }
  if (!writeFile(statePath(stateSlot), C->state_buffer(len), len)) { showToast("State save failed"); return; }
  running->slots.set(std::to_string(stateSlot), Json::number((double)time(nullptr) * 1000));
  writeText(slotsPath(), running->slots.dump());
  showToast("Saved to slot " + std::to_string(stateSlot));
}
static void loadState(bool quietIfNone, int n) {
  vector<uint8_t> d;
  if (!readFile(statePath(n), d, 1024 * 1024)) { if (!quietIfNone) showToast("Slot " + std::to_string(n) + " is empty"); return; }
  char* buf = C->state_buffer((long)d.size());
  if (!buf) { showToast("State load failed"); return; }
  memcpy(buf, d.data(), d.size());
  if (!C->state_load((long)d.size())) { showToast("State load failed"); return; }
  C->audio_reset();
  showToast("Loaded slot " + std::to_string(n));
}
static int newestSlot() {
  int best = 0; double bestT = 0;
  for (int n = 1; n <= SLOT_COUNT; n++) { double t = slotTime(n); if (t > bestT) { bestT = t; best = n; } }
  return best ? best : 1;
}

/* ---------------- Game Genie ---------------- */
static const char* GG_LETTERS = "APZLGITYEOXUKSVN";
struct GG { string code; int addr, value, compare; };
static bool ggDecode(const string& in, GG& g) {
  string code;
  for (char c : in) if (isalpha((unsigned char)c)) code += (char)toupper((unsigned char)c);
  if (code.size() != 6 && code.size() != 8) return false;
  int n[8];
  for (size_t i = 0; i < code.size(); i++) { const char* p = strchr(GG_LETTERS, code[i]); if (!p) return false; n[i] = (int)(p - GG_LETTERS); }
  g.code = code;
  g.addr = 0x8000 + (((n[3] & 7) << 12) | ((n[5] & 7) << 8) | ((n[4] & 8) << 8) | ((n[2] & 7) << 4) | ((n[1] & 8) << 4) | (n[4] & 7) | (n[3] & 8));
  if (code.size() == 6) { g.value = ((n[1] & 7) << 4) | ((n[0] & 8) << 4) | (n[0] & 7) | (n[5] & 8); g.compare = -1; }
  else {
    g.value = ((n[1] & 7) << 4) | ((n[0] & 8) << 4) | (n[0] & 7) | (n[7] & 8);
    g.compare = ((n[7] & 7) << 4) | ((n[6] & 8) << 4) | (n[6] & 7) | (n[5] & 8);
  }
  return true;
}
static int applyCheats() {
  if (!running) return 0;
  if (C->set_cheats) {  // Nestopia: its own cheat engine, codes decoded here
    vector<int> a, v, c;
    for (auto& ch : running->cheats) { GG g; if (ch.on && ggDecode(ch.code, g)) { a.push_back(g.addr); v.push_back(g.value); c.push_back(g.compare); } }
    return C->set_cheats(a.data(), v.data(), c.data(), (int)a.size());
  }
  uint8_t* prg = C->prg(); long size = C->prg_size();
  if (!prg || !size) return 0;
  for (auto& p : running->patches) prg[p.offset] = p.orig;
  running->patches.clear();
  int applied = 0;
  for (auto& c : running->cheats) {
    if (!c.on) continue;
    GG g; if (!ggDecode(c.code, g)) continue;
    vector<long> offs;
    if (g.compare < 0 && size <= 0x8000) offs.push_back((g.addr - 0x8000) % size);
    else for (long o = g.addr & 0x1fff; o < size; o += 0x2000) offs.push_back(o);
    for (long o : offs) {
      if (g.compare >= 0 && prg[o] != g.compare) continue;
      running->patches.push_back(Patch{o, prg[o]}); prg[o] = (uint8_t)g.value; applied++;
    }
  }
  return applied;
}
static void saveCheats() {
  Json a = Json::array();
  for (auto& c : running->cheats) { Json o = Json::object(); o.set("code", Json::string(c.code)); o.set("desc", Json::string(c.desc)); o.set("on", Json::boolean(c.on)); a.a.push_back(o); }
  if (!writeText(running->base + ".cheats.json", a.dump())) record("Cheats save failed");
}
static void loadCheats(const string& romName, const string& romDir) {
  running->cheats.clear(); running->patches.clear();
  string t; Json j; GG g;
  if (readText(running->base + ".cheats.json", t) && Json::parse(t, j) && j.isArr())
    for (auto& c : j.a) {
      const Json* code = c.get("code");
      if (!code || !code->isStr() || !ggDecode(code->s, g)) continue;
      const Json* d = c.get("desc"); const Json* on = c.get("on");
      running->cheats.push_back(Cheat{code->s, d && d->isStr() ? d->s : "", on && on->isBool() && on->b});
    }
  // Codes next to the ROM: nes/<game>.gg, one "CODE description" per line.
  if (readText(romDir + "/" + stripNes(romName) + ".gg", t)) {
    size_t pos = 0;
    while (pos <= t.size()) {
      size_t e = t.find('\n', pos); if (e == string::npos) e = t.size();
      string line = t.substr(pos, e - pos); pos = e + 1;
      while (!line.empty() && isspace((unsigned char)line.back())) line.pop_back();
      size_t s = 0; while (s < line.size() && isspace((unsigned char)line[s])) s++;
      size_t k = s; while (k < line.size() && isalpha((unsigned char)line[k])) k++;
      if (k - s != 6 && k - s != 8) continue;
      if (!ggDecode(line.substr(s, k - s), g)) continue;
      string desc = line.substr(k); size_t ds = desc.find_first_not_of(" \t"); desc = ds == string::npos ? "" : desc.substr(ds);
      if (desc.size() > 40) desc.resize(40);
      bool have = false; for (auto& c : running->cheats) if (c.code == g.code) have = true;
      if (!have) running->cheats.push_back(Cheat{g.code, desc, false});
    }
  }
}

/* ---------------- ROM errors ---------------- */
static const vector<int> SUPPORTED_MAPPERS = {0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 15, 19, 21, 22, 23, 24, 25, 26, 30, 32, 33, 34, 60, 66, 69, 70, 71, 73, 75, 78, 79, 85, 86, 87, 88, 89, 93, 94, 97, 113, 140, 152, 154, 156, 180, 184, 190, 193, 206, 207, 232, 240, 241, 244, 246};
static string mapperName(int m) {
  switch (m) {
    case 16: return "Bandai FCG"; case 18: return "Jaleco SS88006"; case 64: return "Tengen RAMBO-1"; case 65: return "Irem H3001";
    case 67: return "Sunsoft-3"; case 68: return "Sunsoft-4"; case 76: return "Namco 3446"; case 80: return "Taito X1-005";
    case 82: return "Taito X1-017"; case 105: return "NES-EVENT"; case 118: return "TxSROM"; case 119: return "TQROM";
    case 159: return "Bandai LZ93D50"; case 185: return "CNROM with protection"; case 228: return "Action 52";
  }
  return "";
}
// Checks a ROM before loading: iNES header and that the file holds everything
// the header promises. Returns "" when it looks complete.
static string romCheck(const vector<uint8_t>& d) {
  if (d.size() < 16 || d[0] != 0x4e || d[1] != 0x45 || d[2] != 0x53 || d[3] != 0x1a)
    return "This file is not a standard NES ROM (no iNES header).\n\nOnly .nes cartridge images are supported; Famicom Disk System (.fds) and other formats are not.";
  bool nes2 = (d[7] & 0x0c) == 0x08;
  long prgUnits = d[4] | (nes2 ? (d[9] & 0x0f) << 8 : 0), chrUnits = d[5] | (nes2 ? (d[9] & 0xf0) << 4 : 0);
  if (nes2 && ((d[9] & 0x0f) == 0x0f || (d[9] & 0xf0) == 0xf0)) return "";  // exponent sizes: leave the check to the core
  long need = 16 + ((d[6] & 4) ? 512 : 0) + prgUnits * 16384 + chrUnits * 8192;
  if (prgUnits == 0) return "This ROM's header says it has no program data, so it cannot run.\n\nThe file may be damaged or a bad dump.";
  if ((long)d.size() < need)
    return "This file is incomplete: it should be " + std::to_string((need + 1023) / 1024) + " KB but is only " + std::to_string(((long)d.size() + 1023) / 1024) +
           " KB.\n\nCopy it to the SD card again (an interrupted upload leaves a partial file).";
  return "";
}
static string romError(const vector<uint8_t>& h, const string& err) {
  string e = lower(err);
  int mapper = h.size() >= 9 ? ((h[6] >> 4) | (h[7] & 0xf0) | ((h[7] & 0x0c) == 0x08 ? (h[8] & 0x0f) << 8 : 0)) : -1;
  if (e.find("mapper") != string::npos) {
    string n = mapperName(mapper);
    string msg = "This game uses mapper " + std::to_string(mapper) + (n.empty() ? "" : " (" + n + ")") + ", which the " + C->name + " core does not support, so it cannot run.";
    if (C == &QUICKNES_CORE) msg += "\n\nThe Nestopia core supports nearly all mappers: Settings > Core (shown with advanced options).";
    return msg;
  }
  if (e.find("truncated") != string::npos || e.find("corrupt") != string::npos)
    return "This ROM file is damaged or incomplete, so it cannot run.\n\nCopy it to the SD card again, or try a different dump.";
  return "This ROM could not be loaded (" + err + ").\n\nThe file may be damaged or a bad dump.";
}

/* ---------------- Game lifecycle ---------------- */
static void quitGame();
// Reads a .nes file, or the first .nes file inside a .zip.
static bool readRom(const Rom& rom, vector<uint8_t>& data, string& err) {
  string path = rom.dir + "/" + rom.name;
  if (!hasExt(rom.name, ".zip")) { if (!readFile(path, data)) { err = "The file could not be read."; return false; } return true; }
  unzFile z = unzOpen(path.c_str());
  if (!z) { err = "This zip file could not be opened. It may be damaged."; return false; }
  bool found = false;
  for (int r = unzGoToFirstFile(z); r == UNZ_OK; r = unzGoToNextFile(z)) {
    char name[512]; unz_file_info info;
    if (unzGetCurrentFileInfo(z, &info, name, sizeof name, NULL, 0, NULL, 0) != UNZ_OK) continue;
    if (!hasExt(name, ".nes")) continue;
    if (info.uncompressed_size > 8 * 1024 * 1024) { err = "The game inside this zip is too large."; unzClose(z); return false; }
    data.resize(info.uncompressed_size);
    if (unzOpenCurrentFile(z) != UNZ_OK) break;
    int got = data.empty() ? 0 : unzReadCurrentFile(z, data.data(), (unsigned)data.size());
    unzCloseCurrentFile(z);
    found = got == (int)data.size();
    if (!found) { err = "The game inside this zip could not be unpacked. The zip may be damaged."; unzClose(z); return false; }
    break;
  }
  unzClose(z);
  if (!found) { err = "This zip file has no .nes game inside."; return false; }
  return true;
}
static void startGame(const Rom& rom) {
  C = coreSel == 1 ? &NESTOPIA_CORE : &QUICKNES_CORE;
  vector<uint8_t> data;
  string readErr;
  if (!readRom(rom, data, readErr)) { showMessage(readErr, [] { mode = "browser"; }); return; }
  if (string bad = romCheck(data); !bad.empty()) { record("ROM check %s: %s", rom.name.c_str(), bad.c_str()); showMessage(bad, [] { mode = "browser"; }); return; }
  uint8_t* p = C->rom_alloc((long)data.size());
  if (!p) { showMessage("Not enough memory to load this game.", [] { mode = "browser"; }); return; }
  memcpy(p, data.data(), data.size());
  if (const char* err = C->load()) {
    vector<uint8_t> head(data.begin(), data.begin() + std::min<size_t>(16, data.size()));
    C->unload();
    showMessage(romError(head, err), [] { mode = "browser"; });
    return;
  }
  running = new Running();
  running->name = rom.name;
  running->base = string(DATA_DIR) + "/" + sanitize(rom.name);
  running->battery = C->has_battery() != 0;
  if (running->battery) {
    vector<uint8_t> s;
    if (readFile(running->base + ".srm", s, 65536)) {
      if ((int)s.size() <= C->sram_size()) memcpy(C->sram(), s.data(), s.size());
      else record("Save file has the wrong size");
    }
    running->sramHash = hashSram();
  }
  C->power();
  snapsReady = C->snap_alloc(1 + REWIND_CAP) > 0;
  record("Core %s, battery %d (%d bytes)", C->name, running->battery ? 1 : 0, C->sram_size());
  string t; Json j;
  if (readText(slotsPath(), t) && Json::parse(t, j) && j.type == Json::OBJ) running->slots = j;
  stateSlot = autoLoadState ? newestSlot() : 1;
  running->globalVals = gameValues(); running->perGame = false;
  if (readText(running->base + ".settings.json", t) && Json::parse(t, j) && j.type == Json::OBJ) { setGameValuesJson(j); running->perGame = true; }
  recentGames.erase(std::remove(recentGames.begin(), recentGames.end(), rom.name), recentGames.end());
  recentGames.insert(recentGames.begin(), rom.name);
  if ((int)recentGames.size() > RECENT_MAX) recentGames.resize(RECENT_MAX);
  saveSettings();
  paletteApplied = -1; applyEmuOptions();
  loadCheats(rom.name, rom.dir);
  if (applyCheats()) showToast("Cheats active");
  fitPicture(); crtKey.clear(); applyCrt();
  hintUntil = nowMs() + 6000;
  if (autoLoadState) loadState(true, newestSlot());
  resetClock(); mode = "running"; comboLatch = true;
  resetRewind(); emuAcc = 0; ffLatched = false; fastForward = false;
  setAudioRatio(1); audioInteg = 0; pendEma = AUDIO_TARGET_FRAMES[audioBuffer];
  audio::prefill(AUDIO_TARGET_FRAMES[audioBuffer]); ratioTicks = 0; emuCount = 0; workMs = 0; statsAt = nowMs(); startedAt = statsAt;
  frameDrawn = false; statsText.clear(); sessionLog = 0;
  running->sramCheck = nowMs();
  playSavedAt = nowMs(); playAccumSec = 0;
  record("Started %s", rom.name.c_str());
}
static void quitGame() {
  if (!running) return;
  flushPlaytime(running->name);
  saveSram(true);
  C->unload();
  if (running->perGame) setGameValues(running->globalVals);
  delete running; running = nullptr;
  orderRoms();
  mode = "browser";
}

/* ---------------- Input ---------------- */
static int nesPad(u64 b, const int* map = faceMap) {
  const int* faceMap = map;
  int p = 0;
  bool turboOn = (turbo % TURBO_PERIOD[turboRate]) * 2 < TURBO_PERIOD[turboRate];
  for (int f = 0; f < 4; f++) if (b & FACE_BITS[f]) { int t = faceMap[f]; if (t < 2) p |= FACE_TARGETS[t]; else if (t < 4 && turboOn) p |= FACE_TARGETS[t - 2]; }
  if (b & B_PLUS) p |= N_START;
  if (b & B_MINUS) p |= N_SELECT;
  if (b & (B_UP | B_SU)) p |= N_UP;
  if (b & (B_DOWN | B_SD)) p |= N_DOWN;
  if (b & (B_LEFT | B_SL)) p |= N_LEFT;
  if (b & (B_RIGHT | B_SR)) p |= N_RIGHT;
  if ((p & 48) == 48) p &= ~48;
  if ((p & 192) == 192) p &= ~192;
  return p;
}
static u64 lastBits = 0;
static int heldDirs = 0;
static double repeatAt = 0;
struct Nav { u64 pressed; bool up, down; };
static Nav navEvents(u64 bits) {
  Nav ev; ev.pressed = bits & ~lastBits;
  double t = nowMs(); int out = 0;
  int dirs = (bits & (B_UP | B_SU) ? 1 : 0) | (bits & (B_DOWN | B_SD) ? 2 : 0);
  if (dirs != heldDirs) { heldDirs = dirs; repeatAt = t + 380; out |= dirs; }
  else if (dirs && t >= repeatAt) { repeatAt = t + 110; out |= dirs; }
  lastBits = bits;
  ev.up = out & 1; ev.down = out & 2;
  return ev;
}

/* ---------------- In-game menu ---------------- */
struct MenuItem {
  string label;
  std::function<void()> action;                  // plain action (A)
  std::function<vector<string>()> choices;       // option: choices, get, set, after
  std::function<int()> get;
  std::function<void(int)> set;
  std::function<void()> after;
  bool option() const { return (bool)choices; }
};
static vector<MenuItem> menuItems;
static string menuTitle;
static int menuIndex = 0, menuTop = 0;
static MenuItem act(const string& l, std::function<void()> f) { MenuItem m; m.label = l; m.action = f; return m; }
static MenuItem opt(const string& l, std::function<vector<string>()> c, std::function<int()> g, std::function<void(int)> s, std::function<void()> a = nullptr) {
  MenuItem m; m.label = l; m.choices = c; m.get = g; m.set = s; m.after = a; return m;
}
static void showMenuItems(const string& title, const vector<MenuItem>& items) { menuItems = items; menuTitle = title; menuIndex = 0; menuTop = 0; }
static bool captureRequested = false;
static void closeMenu() { if (running) { mode = "running"; if (pauseInMenu) resetClock(); } }
static void pictureChanged() { fitPicture(); applyCrt(); saveSettings(); }
static void setPerGame(bool on) {
  if (on == running->perGame) return;
  if (on) { running->perGame = true; saveSettings(); showToast("Settings saved for this game"); }
  else { running->perGame = false; remove((running->base + ".settings.json").c_str()); setGameValues(running->globalVals); showToast("Using settings for all games"); }
}
static int crtChoiceIndex() { for (size_t i = 0; i < CRT_CHOICES.size(); i++) if (CRT_CHOICES[i] == crtStyle) return (int)i; return 0; }
static vector<string> crtChoiceNames() { vector<string> v; for (int id : CRT_CHOICES) v.push_back(CRT_STYLES[id]); return v; }

static void openMenu();
static void openCodeEditor();
static void openCheatsMenu() {
  vector<MenuItem> items;
  for (size_t i = 0; i < running->cheats.size(); i++) {
    Cheat& c = running->cheats[i];
    items.push_back(opt(c.code + (c.desc.empty() ? "" : "  " + c.desc), [] { return OFF_ON; },
                        [i] { return running->cheats[i].on ? 1 : 0; }, [i](int v) { running->cheats[i].on = v != 0; },
                        [] { applyCheats(); saveCheats(); }));
  }
  items.push_back(act("Add code", [] { openCodeEditor(); }));
  if (!running->cheats.empty()) items.push_back(act("Remove last code", [] { running->cheats.pop_back(); applyCheats(); saveCheats(); openCheatsMenu(); }));
  items.push_back(act("Back", [] { openMenu(); }));
  showMenuItems("Cheats (Game Genie)", items);
}
static void openMenu() {
  vector<MenuItem> items = {
      act("Resume", [] { closeMenu(); }),
      act("Save state", [] { saveState(); closeMenu(); }),
      act("Load state", [] { closeMenu(); loadState(false, stateSlot); }),
      opt("Slot", [] { vector<string> a; for (int n = 1; n <= SLOT_COUNT; n++) a.push_back(slotLabel(n)); return a; },
          [] { return stateSlot - 1; }, [](int v) { stateSlot = v + 1; }),
      act("Reset", [] { C->reset(0); closeMenu(); showToast("Reset"); }),
      opt("Run-ahead", [] { return RUN_AHEADS; }, [] { return runAhead; }, [](int v) { runAhead = v; }, saveSettings),
      opt("Stats", [] { return STATS_LEVELS; }, [] { return statsLevel; }, [](int v) { statsLevel = v; }, [] { resetGraph(); saveSettings(); }),
      opt("Aspect", [] { return ASPECTS; }, [] { return aspectMode; }, [](int v) { aspectMode = v; }, pictureChanged),
      opt("Scale", [] { return vector<string>{scaleLabel(0), scaleLabel(1)}; }, [] { return scaleMode; }, [](int v) { scaleMode = v; }, pictureChanged),
      opt("Overscan", [] { return CROPS; }, [] { return cropMode; }, [](int v) { cropMode = v; }, pictureChanged),
      opt("Signal", [] { return SIGNALS; }, [] { return signalMode; }, [](int v) { signalMode = v; }, pictureChanged),
      opt("Shader", [] { return SHADERS; }, [] { return shader; }, [](int v) { shader = v; }, pictureChanged),
      opt("Brightness boost", [] { return BOOSTS; }, [] { return boost; }, [](int v) { boost = v; }, pictureChanged),
      opt("Curvature", [] { return CURVATURES; }, [] { return curvature; }, [](int v) { curvature = v; }, pictureChanged),
      opt("Corner radius", [] { return CORNER_RADII; }, [] { return cornerRadius; }, [](int v) { cornerRadius = v; }, pictureChanged),
      opt("Corner softness", [] { return CORNER_SOFTNESS; }, [] { return cornerSoftness; }, [](int v) { cornerSoftness = v; }, pictureChanged),
      opt("Vignette", [] { return VIGNETTES; }, [] { return vignette; }, [](int v) { vignette = v; }, pictureChanged),
      opt("Vignette size", [] { return VIGNETTE_SIZES; }, [] { return vignetteSize; }, [](int v) { vignetteSize = v; }, pictureChanged),
      opt("Palette", [] { return PALETTES; }, [] { return paletteMode; }, [](int v) { paletteMode = v; }, [] { refreshPicture(); saveSettings(); }),
      opt("Favourite", [] { return OFF_ON; }, [] { return contains(favourites, running->name) ? 1 : 0; },
          [](int v) { favourites.erase(std::remove(favourites.begin(), favourites.end(), running->name), favourites.end()); if (v) favourites.push_back(running->name); }, saveSettings),
      opt("Settings for", [] { return vector<string>{"All games", "This game"}; }, [] { return running->perGame ? 1 : 0; }, [](int v) { setPerGame(v != 0); },
          [] { pictureChanged(); refreshPicture(); }),
      act("Save screenshot", [] { closeMenu(); captureRequested = true; }),
      act("Cheats...", [] { openCheatsMenu(); }),
      act("Quit game", [] { quitGame(); }),
  };
  if (!showAdvanced) {
    vector<MenuItem> basic;
    for (auto& m : items) if (!contains(ADVANCED_MENU, m.label)) basic.push_back(m);
    items = basic;
  }
  showMenuItems(stripNes(running->name), items);
  mode = "menu";
  saveSram(false);
}
static void cycleMenuOption(int dir) {
  MenuItem& o = menuItems[menuIndex];
  if (!o.option()) return;
  int n = (int)o.choices().size();
  o.set((o.get() + dir + n) % n);
  if (o.after) o.after();
}

/* ---------------- Game Genie editor ---------------- */
static int ggLetters[8], ggPos = 0;
static void openCodeEditor() { for (int i = 0; i < 8; i++) ggLetters[i] = i < 6 ? 0 : -1; ggPos = 0; mode = "ggedit"; }
static string ggEditCode() { string s; for (int i = 0; i < 8; i++) if (ggLetters[i] >= 0) s += GG_LETTERS[ggLetters[i]]; return s; }
static void closeCodeEditor() { mode = "menu"; openCheatsMenu(); }
static void handleCodeEditor(const Nav& ev) {
  int minV = ggPos >= 6 ? -1 : 0, maxV = 15;
  if (ev.pressed & B_LEFT) ggPos = std::max(0, ggPos - 1);
  if (ev.pressed & B_RIGHT) ggPos = std::min(7, ggPos + 1);
  if (ev.up) { int v = ggLetters[ggPos] + 1; ggLetters[ggPos] = v > maxV ? minV : v; }
  if (ev.down) { int w = ggLetters[ggPos] - 1; ggLetters[ggPos] = w < minV ? maxV : w; }
  if (ggPos == 6 && ggLetters[6] < 0) ggLetters[7] = -1;
  if (ev.pressed & B_A) {
    GG g; string code = ggEditCode();
    if (!ggDecode(code, g)) { showToast("A code needs 6 or 8 letters"); return; }
    running->cheats.push_back(Cheat{code, "", true}); applyCheats(); saveCheats(); showToast("Code " + code + " added");
    closeCodeEditor();
  } else if (ev.pressed & B_B) closeCodeEditor();
}

/* ---------------- ROM folder picker ---------------- */
static string pickPath;
static vector<string> pickDirs;
static int pickSel = 0, pickTop = 0, pickGames = 0;
static string parentDir(const string& p) {
  size_t k = p.find_last_of('/');
  if (k == string::npos || k <= 5) return "sdmc:/";
  return p.substr(0, k);
}
static void pickLoad(const string& path) {
  pickPath = path; pickDirs.clear(); pickSel = 0; pickTop = 0; pickGames = 0;
  string open = path == "sdmc:" ? "sdmc:/" : path;
  if (DIR* d = opendir(open.c_str())) {
    while (struct dirent* e = readdir(d)) {
      string n = e->d_name;
      if (n.empty() || n[0] == '.') continue;
      if (e->d_type == DT_DIR) pickDirs.push_back(n);
      else if (hasExt(n, ".nes") || hasExt(n, ".zip")) pickGames++;
    }
    closedir(d);
  }
  std::sort(pickDirs.begin(), pickDirs.end(), [](const string& a, const string& b) { return naturalLess(a, b); });
}
static bool pickAtRoot() { return pickPath == "sdmc:/" || pickPath == "sdmc:"; }
// Rows: "Use this folder", then ".." (unless at the root), then subfolders.
static int pickRows() { return 1 + (pickAtRoot() ? 0 : 1) + (int)pickDirs.size(); }
static void openFolderPicker() {
  struct stat st;
  pickLoad(stat(romDir.c_str(), &st) == 0 ? romDir : "sdmc:/");
  mode = "folders";
}

/* ---------------- Settings screen ---------------- */
struct SettingItem { string label; std::function<vector<string>()> choices; std::function<int()> get; std::function<void(int)> set; };
static vector<SettingItem> settingsItems() {
  return {
      {"ROM folder", [] { return vector<string>{romDir.rfind("sdmc:", 0) == 0 ? romDir.substr(5) : romDir}; }, [] { return 0; }, [](int) { openFolderPicker(); }},
      {"Aspect", [] { return ASPECTS; }, [] { return aspectMode; }, [](int v) { aspectMode = v; }},
      {"Scale", [] { return SCALES; }, [] { return scaleMode; }, [](int v) { scaleMode = v; }},
      {"Overscan", [] { return CROPS; }, [] { return cropMode; }, [](int v) { cropMode = v; }},
      {"Signal", [] { return SIGNALS; }, [] { return signalMode; }, [](int v) { signalMode = v; }},
      {"Shader", [] { return SHADERS; }, [] { return shader; }, [](int v) { shader = v; }},
      {"Brightness boost", [] { return BOOSTS; }, [] { return boost; }, [](int v) { boost = v; }},
      {"Curvature", [] { return CURVATURES; }, [] { return curvature; }, [](int v) { curvature = v; }},
      {"Corner radius", [] { return CORNER_RADII; }, [] { return cornerRadius; }, [](int v) { cornerRadius = v; }},
      {"Corner softness", [] { return CORNER_SOFTNESS; }, [] { return cornerSoftness; }, [](int v) { cornerSoftness = v; }},
      {"Vignette", [] { return VIGNETTES; }, [] { return vignette; }, [](int v) { vignette = v; }},
      {"Vignette size", [] { return VIGNETTE_SIZES; }, [] { return vignetteSize; }, [](int v) { vignetteSize = v; }},
      {"Palette", [] { return PALETTES; }, [] { return paletteMode; }, [](int v) { paletteMode = v; }},
      {"Stats", [] { return STATS_LEVELS; }, [] { return statsLevel; }, [](int v) { statsLevel = v; }},
      {"Core", [] { return CORES; }, [] { return coreSel; }, [](int v) { coreSel = v; }},
      {"Run-ahead", [] { return RUN_AHEADS; }, [] { return runAhead; }, [](int v) { runAhead = v; }},
      {"Frame delay (60 Hz)", [] { return FRAME_DELAYS; }, [] { return frameDelay; }, [](int v) { frameDelay = v; }},
      {"Audio buffer", [] { return AUDIO_BUFFERS; }, [] { return audioBuffer; }, [](int v) { audioBuffer = v; }},
      {"Display", [] { return DISPLAY_MODES; }, [] { return displayMode; }, [](int v) { displayMode = v; gfx::setHighRefresh(v == 1); }},
      {"Rewind (hold ZL)", [] { return OFF_ON; }, [] { return rewindOn ? 1 : 0; }, [](int v) { rewindOn = v != 0; }},
      {"Fast-forward", [] { return FF_MODES; }, [] { return ffMode; }, [](int v) { ffMode = v; }},
      {"Fast-forward speed", [] { return FF_SPEEDS; }, [] { return ffSpeed; }, [](int v) { ffSpeed = v; }},
      {"Auto-load save state", [] { return OFF_ON; }, [] { return autoLoadState ? 1 : 0; }, [](int v) { autoLoadState = v != 0; }},
      {"Pause in menu", [] { return OFF_ON; }, [] { return pauseInMenu ? 1 : 0; }, [](int v) { pauseInMenu = v != 0; }},
      {"Dim game in menu", [] { return OFF_ON; }, [] { return dimInMenu ? 1 : 0; }, [](int v) { dimInMenu = v != 0; }},
      {"Volume", [] { return VOLUMES; }, [] { return volumeLevel; }, [](int v) { volumeLevel = v; }},
      {"Audio filter", [] { return FILTERS; }, [] { return audioFilter; }, [](int v) { audioFilter = v; }},
      {"Turbo speed", [] { return TURBO_RATES; }, [] { return turboRate; }, [](int v) { turboRate = v; }},
      {"Button mapping for", [] { return vector<string>{"Player 1", "Player 2"}; }, [] { return remapPlayer; }, [](int v) { remapPlayer = v; }},
      {"Switch A button", [] { return FACE_CHOICES; }, [] { return curMap()[0]; }, [](int v) { curMap()[0] = v; }},
      {"Switch B button", [] { return FACE_CHOICES; }, [] { return curMap()[1]; }, [](int v) { curMap()[1] = v; }},
      {"Switch X button", [] { return FACE_CHOICES; }, [] { return curMap()[2]; }, [](int v) { curMap()[2] = v; }},
      {"Switch Y button", [] { return FACE_CHOICES; }, [] { return curMap()[3]; }, [](int v) { curMap()[3] = v; }},
  };
}
static vector<SettingItem> SETTINGS_ITEMS;
static bool resetArmed = false;
static void rebuildSettings() {
  SETTINGS_ITEMS.clear();
  SETTINGS_ITEMS.push_back({"Advanced options", [] { return vector<string>{"Hidden", "Shown"}; }, [] { return showAdvanced ? 1 : 0; },
                            [](int v) { showAdvanced = v != 0; rebuildSettings(); }});
  for (auto& it : settingsItems())
    if (showAdvanced || !contains(ADVANCED_SETTINGS, it.label)) SETTINGS_ITEMS.push_back(it);
  if (showAdvanced)
    SETTINGS_ITEMS.push_back({"Reset all settings", [] { return vector<string>{"Press A", "Press A again to confirm"}; }, [] { return resetArmed ? 1 : 0; },
                              [](int v) { if (resetArmed) { resetArmed = false; resetAllSettings(); gfx::setHighRefresh(displayMode == 1); rebuildSettings(); settingsIndex = 0; settingsTop = 0; showToast("All settings reset to defaults"); } else resetArmed = true; }});
  if (settingsIndex > (int)SETTINGS_ITEMS.size()) settingsIndex = (int)SETTINGS_ITEMS.size();
}

/* ---------------- Drawing helpers ---------------- */
static const float LH = 1.15f;  // CSS "normal" line height, approximately
// Text vertically centred in a box.
static float textMid(const string& s, float x, float y, float h, float size, Color c, gfx::Font f = gfx::REGULAR) {
  return gfx::text(s, x, y + (h - size * 1.25f) / 2, size, c, f);
}
// "< value >" selector, right-aligned at xr; returns its width.
static float valueBox(const string& t, float xr, float y, float h, float size, bool active) {
  Color c = col(active ? C_TEXT : C_DIM);
  float wl = gfx::textWidth("<", size, gfx::BOLD), wt = gfx::textWidth(t, size), wr = gfx::textWidth(">", size, gfx::BOLD);
  float total = wl + 10 + wt + 10 + wr, x = xr - total;
  textMid("<", x, y, h, size, c, gfx::BOLD);
  textMid(t, x + wl + 10, y, h, size, c);
  textMid(">", x + wl + 10 + wt + 10, y, h, size, c, gfx::BOLD);
  return total;
}
static vector<string> wrap(const string& text, float width, float size, gfx::Font f = gfx::REGULAR) {
  vector<string> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t e = text.find('\n', pos); if (e == string::npos) e = text.size();
    string para = text.substr(pos, e - pos), line;
    size_t i = 0;
    // Keep leading spaces (indented lines in messages).
    while (i < para.size() && para[i] == ' ') { line += ' '; i++; }
    string lead = line;
    while (i <= para.size()) {
      size_t sp = para.find(' ', i); if (sp == string::npos) sp = para.size();
      string word = para.substr(i, sp - i);
      string next = line.size() > lead.size() ? line + " " + word : line + word;
      if (line.size() > lead.size() && gfx::textWidth(next, size, f) > width) { out.push_back(line); line = word; }
      else line = next;
      i = sp + 1;
    }
    out.push_back(line);
    pos = e + 1;
  }
  return out;
}
static void panelHeader(float x, float y, const string& title, const string& note) {
  gfx::rect(x, y + 4, 10, 26, col(C_ACCENT), 2);
  float w = textMid(title, x + 20, y, 34, 26, col(C_TEXT), gfx::BOLD);
  if (!note.empty()) textMid(note, x + 20 + w + 12, y + 3, 34, 16, col(C_DIM));
}
static void scrollbar(float x, float y, float h, int total, int vis, int topIndex) {
  if (total <= vis) return;
  gfx::rect(x, y, 8, h, col(C_LINE), 4);
  float th = std::max(24.0f, roundf(h * vis / total));
  gfx::rect(x, y + roundf((h - th) * topIndex / (total - vis)), 8, th, col(C_DIM), 4);
}

/* ---------------- Screens ---------------- */
static const vector<string> BROWSER_BUTTONS = {"Settings", "Refresh", "Exit"};
static const char* BROWSER_KEYS[] = {"Y", "X", "B"};  // gamepad shortcut for each button
static const int ROW_H = 40;
static const float LIST_HEAD = 26;  // column headings above the game list
static int maxRows() { return std::max(3, (int)((gfx::UI_H * 0.9 - 36 - 38 - 44 - 12 - 30 - 14 - LIST_HEAD) / ROW_H)); }

static void drawBrowser() {
  const float winW = 960, padX = 20, padY = 18, innerW = winW - 2 * padX;
  int VIS = std::max(1, std::min((int)roms.size(), maxRows()));
  if (selected < top) top = selected;
  if (selected >= top + VIS) top = selected - VIS + 1;
  if (top > std::max(0, (int)roms.size() - VIS)) top = std::max(0, (int)roms.size() - VIS);
  bool scroll = (int)roms.size() > VIS;
  string empty;
  if (roms.empty()) {
    if (listView == 1) empty = "No favourites yet.\n\nWhile playing, open the menu (L + R) and set Favourite to On.";
    else if (listView == 2) empty = "No recently played games yet.";
    else empty = "No games found in\n\n    " + romDir + "\n\nCopy .nes (or zipped) games there, for example over FTP with OunceShell, then choose Refresh. The folder can be changed in Settings.";
  }
  float listInnerW = innerW - (scroll ? 16 : 0);
  vector<string> emptyLines = empty.empty() ? vector<string>() : wrap(empty, listInnerW - 32, 17);
  float listH = roms.empty() ? emptyLines.size() * 17 * 1.45f + 20 + 12 : VIS * ROW_H + 12;
  float head = roms.empty() ? 0 : LIST_HEAD;
  float winH = padY * 2 + 34 + 12 + 36 + 10 + head + listH + 12 + 40;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, "SW2NES", roms.empty() ? "" : std::to_string(roms.size()) + (roms.size() == 1 ? " game" : " games"));
  cy += 34 + 12;
  // Tabs
  float tx = cx;
  for (size_t i = 0; i < LIST_VIEWS.size(); i++) {
    bool on = (int)i == listView, focus = focusArea == "tabs" && on;
    float w = gfx::textWidth(LIST_VIEWS[i], 17) + 32;
    gfx::rect(tx, cy, w, 34, focus ? col(C_SEL) : on ? col(C_LINE) : Color{0, 0, 0, 0}, 16, 1, col(focus ? C_ACCENT : C_LINE));
    textMid(LIST_VIEWS[i], tx + 16, cy, 34, 17, col(on ? C_TEXT : C_DIM));
    tx += w + 8;
  }
  cy += 36 + 10;
  // Column headings
  // Columns from the right: Time played, Recent, Favourite (centred tags).
  const float TIME_W = 150, REC_W = 100, FAV_W = 110;
  const float colR = cx + listInnerW - 18, recC = colR - TIME_W - REC_W / 2, favC = recC - REC_W / 2 - FAV_W / 2;
  if (!roms.empty()) {
    gfx::text("Game", cx + 22, cy + 1, 14, col(C_DIM), gfx::BOLD);
    float hw = gfx::textWidth("Time played", 14, gfx::BOLD);
    gfx::text("Time played", colR - hw, cy + 1, 14, col(C_DIM), gfx::BOLD);
    gfx::text("Recent", recC - gfx::textWidth("Recent", 14, gfx::BOLD) / 2, cy + 1, 14, col(C_DIM), gfx::BOLD);
    gfx::text("Favourite", favC - gfx::textWidth("Favourite", 14, gfx::BOLD) / 2, cy + 1, 14, col(C_DIM), gfx::BOLD);
    cy += head;
  }
  // List
  gfx::rect(cx, cy, listInnerW, listH, col(C_BG), 10, 1, col(C_LINE));
  if (roms.empty()) {
    float ly = cy + 16;
    for (auto& l : emptyLines) { gfx::text(l, cx + 16, ly, 17, col(C_TEXT)); ly += 17 * 1.45f; }
  } else {
    gfx::clip((int)cx, (int)cy, (int)listInnerW, (int)listH);
    for (int i = 0; i < VIS && top + i < (int)roms.size(); i++) {
      const Rom& r = roms[top + i];
      float ry = cy + 6 + i * ROW_H, rx = cx + 6, rw = listInnerW - 12;
      bool active = top + i == selected && focusArea == "list";
      if (active) { gfx::rect(rx, ry, rw, ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, ROW_H, col(C_ACCENT), 2); }
      bool fav = contains(favourites, r.name), rec = contains(recentGames, r.name);
      string played = playLabel(r.name);
      if (!played.empty()) { float pw = gfx::textWidth(played, 16); textMid(played, rx + rw - 12 - pw, ry, ROW_H, 16, col(C_DIM)); }
      if (fav) {
        float tw = gfx::textWidth("Favourite", 14) + 16;
        gfx::rect(favC - tw / 2, ry + 9, tw, 22, gfx::rgb(0xf2c94c), 4);
        textMid("Favourite", favC - tw / 2 + 8, ry + 9, 22, 14, gfx::rgb(0x1a1300));
      }
      if (rec) {
        float tw = gfx::textWidth("Recent", 14) + 16;
        gfx::rect(recC - tw / 2, ry + 9, tw, 22, Color{0, 0, 0, 0}, 4, 1, col(C_LINE));
        textMid("Recent", recC - tw / 2 + 8, ry + 9, 22, 14, col(C_DIM));
      }
      textMid(gfx::ellipsize(stripNes(r.name), favC - FAV_W / 2 - rx - 28, 20), rx + 16, ry, ROW_H, 20, col(C_TEXT));
    }
    gfx::noClip();
    scrollbar(cx + listInnerW + 8, cy, listH, (int)roms.size(), VIS, top);
  }
  cy += listH + 12;
  // Buttons
  float bw = (innerW - 30) / 3;
  for (int i = 0; i < 3; i++) {
    bool on = focusArea == "buttons" && i == btnIndex;
    float bx = cx + 5 + i * (bw + 10);
    string label = i == 2 && exitArmed ? "Confirm exit" : BROWSER_BUTTONS[i];
    gfx::rect(bx, cy, bw, 40, col(on ? C_SEL : C_BG), 8, 1, col(on ? C_ACCENT : C_LINE));
    float tw = gfx::textWidth(label, 18), kw = 24, lx = bx + (bw - tw - kw - 8) / 2;
    gfx::rect(lx, cy + 8, kw, kw, col(C_LINE), kw / 2);  // shortcut button badge
    float kt = gfx::textWidth(BROWSER_KEYS[i], 15);
    textMid(BROWSER_KEYS[i], lx + (kw - kt) / 2, cy + 8, kw, 15, col(C_TEXT));
    textMid(label, lx + kw + 8, cy, 40, 18, col(i == 2 && exitArmed ? C_ACCENT : C_TEXT));
  }
  gfx::text("v" APP_VERSION, x + winW - padX - gfx::textWidth("v" APP_VERSION, 14), y + padY + 10, 14, col(C_DIM));
}

static const int SET_ROW_H = 34;
static void drawFolderPicker() {
  const float winW = 960, padX = 20, padY = 18, rowH = 34;
  int total = pickRows();
  int vis = std::max(5, std::min(total, (int)((gfx::UI_H * 0.9 - 36 - 50 - 40 - 14) / rowH)));
  if (pickSel < pickTop) pickTop = pickSel;
  if (pickSel >= pickTop + vis) pickTop = pickSel - vis + 1;
  bool scroll = total > vis;
  float listW = winW - 2 * padX - (scroll ? 16 : 0), listH = vis * rowH + 12;
  float winH = padY * 2 + 34 + 12 + 30 + listH;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, "ROM folder", "A: open   B: cancel");
  cy += 34 + 12;
  string shown = pickPath.rfind("sdmc:", 0) == 0 ? pickPath.substr(5) : pickPath;
  if (shown.empty()) shown = "/";
  string note = std::to_string(pickGames) + (pickGames == 1 ? " game here" : " games here");
  textMid(gfx::ellipsize(shown, winW - 2 * padX - 200, 18, gfx::MONO), cx + 4, cy, 26, 18, col(C_TEXT), gfx::MONO);
  textMid(note, cx + winW - 2 * padX - gfx::textWidth(note, 16) - 4, cy, 26, 16, col(C_DIM));
  cy += 30;
  gfx::rect(cx, cy, listW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int k = 0; k < vis && pickTop + k < total; k++) {
    int i = pickTop + k;
    bool active = i == pickSel;
    float rx = cx + 6, ry = cy + 6 + k * rowH, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, rowH, col(C_SEL), 6); gfx::rect(rx, ry, 4, rowH, col(C_ACCENT), 2); }
    string label; Color c = col(C_TEXT);
    int up = pickAtRoot() ? 0 : 1;
    if (i == 0) { label = "Use this folder"; c = gfx::rgb(0x6ee26e); }
    else if (up && i == 1) label = ".. (up one level)";
    else label = pickDirs[i - 1 - up] + "/";
    textMid(gfx::ellipsize(label, rw - 32, 18), rx + 16, ry, rowH, 18, c, i == 0 ? gfx::BOLD : gfx::REGULAR);
  }
  scrollbar(cx + listW + 8, cy, listH, total, vis, pickTop);
}
static void drawSettings() {
  const float winW = 960, padX = 20, padY = 18, innerW = winW - 2 * padX;
  int n = (int)SETTINGS_ITEMS.size(), total = n + 1;
  int vis = std::max(5, std::min(total, (int)((gfx::UI_H * 0.9 - 36 - 50 - 14) / SET_ROW_H)));
  if (settingsIndex < settingsTop) settingsTop = settingsIndex;
  if (settingsIndex >= settingsTop + vis) settingsTop = settingsIndex - vis + 1;
  bool scroll = total > vis;
  float listW = innerW - (scroll ? 16 : 0), listH = vis * SET_ROW_H + 12;
  float winH = padY * 2 + 34 + 12 + listH;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, "Settings", "for all games");
  cy += 34 + 12;
  gfx::rect(cx, cy, listW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int k = 0; k < vis; k++) {
    int i = settingsTop + k;
    if (i >= total) break;
    bool active = i == settingsIndex;
    float rx = cx + 6, ry = cy + 6 + k * SET_ROW_H, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, SET_ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, SET_ROW_H, col(C_ACCENT), 2); }
    if (i < n) {
      textMid(SETTINGS_ITEMS[i].label, rx + 16, ry, SET_ROW_H, 18, col(C_TEXT));
      vector<string> ch = SETTINGS_ITEMS[i].choices();
      valueBox(ch[SETTINGS_ITEMS[i].get()], rx + rw - 12, ry, SET_ROW_H, 18, active);
    } else textMid("Back", rx + 16, ry, SET_ROW_H, 18, col(C_TEXT));
  }
  scrollbar(cx + listW + 8, cy, listH, total, vis, settingsTop);
}

static void drawStatsBar() {
  if (statsLevel <= 0 || !running) return;
  string t = statsText.empty() ? " " : statsText;
  float tw = gfx::textWidth(t, 18, gfx::MONO), gw = statsLevel == 3 ? GRAPH_W + 12 : 0;
  float h = std::max(18 * LH, statsLevel == 3 ? (float)GRAPH_H : 0) + 8;
  gfx::rect(16, 12, tw + gw + 20, h, gfx::Color{0, 0, 0, 0.6f}, 6);
  textMid(t, 26, 12, h, 18, gfx::rgb(0x9dfc9d), gfx::MONO);
  if (statsLevel == 3 && graphImage) gfx::imageDraw(graphImage, 26 + tw + 12, 12 + (h - GRAPH_H) / 2, GRAPH_W, GRAPH_H);
}

static void drawGame(bool dim) {
  if (running) gfx::pictureDraw(picture, picX, picY, picW, picH, picCX, picCY, picCW, picCH);
  drawStatsBar();
  if (hintUntil && !dim) {
    float w = gfx::textWidth("L + R: Menu", 16);
    gfx::text("L + R: Menu", gfx::UI_W - 16 - w, gfx::UI_H - 12 - 20, 16, gfx::rgb(0x8891a4));
  }
}

static void drawMenu() {
  if (dimInMenu) gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f});
  const float panelW = 460, pad = 26, rowH = 21 * LH + 16, gap = 3, headH = 26 * LH + 14;
  // As many rows as fit in 90% of the screen; the rest scroll.
  int n = (int)menuItems.size();
  int vis = std::max(4, std::min(n, (int)((gfx::UI_H * 0.9f - pad * 2 - headH) / (rowH + gap))));
  if (menuIndex < menuTop) menuTop = menuIndex;
  if (menuIndex >= menuTop + vis) menuTop = menuIndex - vis + 1;
  if (menuTop > n - vis) menuTop = std::max(0, n - vis);
  bool scroll = n > vis;
  float listH = vis * (rowH + gap);
  float h = pad * 2 + headH + listH;
  float x = (gfx::UI_W - panelW) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, panelW, h, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + pad, cy = y + pad, w = panelW - 2 * pad - (scroll ? 14 : 0);
  gfx::text(gfx::ellipsize(menuTitle, panelW - 2 * pad, 26, gfx::BOLD), cx, cy - 2, 26, col(C_TEXT), gfx::BOLD);
  cy += headH;
  for (int k = 0; k < vis && menuTop + k < n; k++) {
    int i = menuTop + k;
    bool active = i == menuIndex;
    float ry = cy + gap + k * (rowH + gap);
    if (active) { gfx::rect(cx, ry, w, rowH, col(C_SEL), 8); gfx::rect(cx, ry, 4, rowH, col(C_ACCENT), 2); }
    const MenuItem& m = menuItems[i];
    float vw = 0;
    if (m.option()) { vector<string> ch = m.choices(); int v = m.get(); vw = valueBox(ch[std::max(0, std::min((int)ch.size() - 1, v))], cx + w - 14, ry, rowH, 18, active) + 16; }
    textMid(gfx::ellipsize(m.label, w - 32 - vw, 21), cx + 18, ry, rowH, 21, col(C_TEXT));
  }
  if (scroll) scrollbar(cx + w + 6, cy + gap, listH - gap, n, vis, menuTop);
}

static void drawCodeEditor() {
  gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f});
  GG g; bool ok = ggDecode(ggEditCode(), g);
  char info[80];
  if (ok) {
    if (g.compare >= 0) snprintf(info, sizeof info, "$%04X = %02X if %02X", g.addr, g.value, g.compare);
    else snprintf(info, sizeof info, "$%04X = %02X", g.addr, g.value);
  } else snprintf(info, sizeof info, "Codes have 6 or 8 letters");
  string help = "Left/Right: move   Up/Down: letter   A: save   B: cancel";
  float cellsW = 8 * 52, w = std::max(cellsW, gfx::textWidth(help, 16)) + 56, h = 24 + 24 * LH + 16 + 54 + 14 + 2 * 16 * LH + 24;
  float x = (gfx::UI_W - w) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, w, h, col(C_PANEL), 14, 1, col(C_LINE));
  string title = "Enter Game Genie code";
  gfx::text(title, x + (w - gfx::textWidth(title, 24, gfx::BOLD)) / 2, y + 22, 24, col(C_TEXT), gfx::BOLD);
  float cy = y + 24 + 24 * LH + 16, cx = x + (w - cellsW) / 2;
  for (int i = 0; i < 8; i++) {
    float bx = cx + i * 52 + 4;
    gfx::rect(bx, cy, 44, 54, col(C_BG), 6, 2, col(i == ggPos ? C_ACCENT : C_LINE));
    string s(1, ggLetters[i] >= 0 ? GG_LETTERS[ggLetters[i]] : '-');
    textMid(s, bx + (44 - gfx::textWidth(s, 30, gfx::MONO)) / 2, cy, 54, 30, col(ggLetters[i] >= 0 ? C_TEXT : C_DIM), gfx::MONO);
  }
  cy += 54 + 14;
  gfx::text(info, x + (w - gfx::textWidth(info, 16)) / 2, cy, 16, col(C_DIM));
  gfx::text(help, x + (w - gfx::textWidth(help, 16)) / 2, cy + 16 * LH, 16, col(C_DIM));
}

static void drawMessage() {
  gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.75f});
  vector<string> lines = wrap(messageText, 700, 22);
  float lh = 22 * 1.45f, w = 760, h = 30 * 2 + lines.size() * lh + 18 + 18 * LH;
  float x = (gfx::UI_W - w) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, w, h, col(C_PANEL), 14, 1, col(C_LINE));
  float cy = y + 30;
  for (auto& l : lines) { textMid(l, x + 30, cy, lh, 22, col(C_TEXT)); cy += lh; }
  gfx::text("Press A or B to continue", x + 30, cy + 18 - 2, 18, col(C_DIM));
}

static void drawToast() {
  if (!toastUntil) return;
  float w = gfx::textWidth(toastText, 20) + 36, h = 20 * LH + 20;
  float x = roundf((gfx::UI_W - w) / 2), y = gfx::UI_H - 28 - h;
  gfx::rect(x, y, w, h, gfx::rgb(0x0a0c12, 0.85f), 8);
  textMid(toastText, x + 18, y, h, 20, col(C_TEXT));
}

/* ---------------- Main loop ---------------- */

// One real (shown) NES frame with input p/p2, run-ahead applied: the real frame
// runs without drawing and keeps its audio; then `ahead` frames run from a
// snapshot with the same input, the last one drawn, audio discarded, and the
// snapshot is restored. The picture shows the game `ahead` frames early.
// One real (shown) NES frame. With run-ahead, the real frame runs without
// drawing (its audio kept) and a second emulator instance runs `ahead` frames
// from it with the same input; the picture comes from that instance.
static bool emulateShown(int p, int p2, int ahead, bool keepAudio) {
  if (!keepAudio) C->set_audio_discard(1);
  bool ok = C->frame(p, p2, ahead > 0 ? 0 : 1) >= 0;
  C->set_audio_discard(0);
  int n = C->audio_take();
  if (n && keepAudio) audio::push(C->audio(), n);
  if (ok && ahead > 0 && C->runahead(p, p2, ahead) < 0) { C->reconvert(); }
  return ok;
}

static void runFrame(u64 bits, u64 bits2, bool behindMenu) {
  double t0 = nowMs();
  if (!behindMenu) {
    bool combo = (bits & (B_L | B_R)) == (u64)(B_L | B_R);
    if (combo && !comboLatch) { comboLatch = true; lastBits = bits; openMenu(); return; }
    if (!combo) comboLatch = false;
  }
  // Fast-forward (ZR hold or toggle) and rewind (hold ZL).
  bool zr = !behindMenu && (bits & HidNpadButton_ZR), zl = !behindMenu && (bits & HidNpadButton_ZL);
  if (ffMode == 2 && zr && !ffPrevHeld) ffLatched = !ffLatched;
  ffPrevHeld = zr;
  if (ffMode != 2) ffLatched = false;
  bool wasFast = fastForward;
  fastForward = ffMode == 1 ? zr : ffMode == 2 ? ffLatched : false;
  if (wasFast != fastForward && !fastForward) emuAcc = 0;
  bool wasRewinding = rewinding;
  rewinding = rewindOn && zl && snapsReady;
  if (wasRewinding && !rewinding) C->audio_reset();  // resume sound cleanly from the rewound point

  if (rewinding) {
    // Step back one snapshot per refresh (REWIND_EVERY frames), showing it.
    if (rewindCount > 0) {
      rewindHead = (rewindHead - 1 + REWIND_CAP) % REWIND_CAP; rewindCount--;
      C->snap_load(1 + rewindHead);
      C->set_audio_discard(1);
      C->frame(0, 0, 1);
      C->audio_take();
      C->set_audio_discard(0);
      C->snap_load(1 + rewindHead);  // resume exactly from the snapshot
      gfx::pictureUpload(picture, C->video());
      if (signalMode) uploadColors();
      frameDrawn = true;
    }
    workMs += nowMs() - t0;
    return;
  }

  // How many NES frames this refresh: one at 60 Hz (locked to the display, audio
  // stretched by the rate difference); by the clock at 120 Hz / VRR.
  // At 120 Hz / VRR the display does not tick at the NES rate, so emulation
  // follows the clock: the exact time since the previous refresh (no
  // smoothing; with VRR long and short refreshes alternate and a smoothed
  // average ran games too fast).
  // 120 Hz / VRR: the main loop already waits for each NES frame's time slot
  // (see vrrWait), so exactly one frame per loop. A 60 Hz display that is
  // not ticking near 60 Hz falls back to following the clock.
  int due = 1;
  bool hz120 = !highRefresh() && (tickMs < 15.5 || tickMs > 18);
  if (hz120) {
    emuAcc += std::min(loopGapMs, 50.0) * NES_RATE / 1000;
    due = (int)emuAcc; emuAcc -= due;
    if (due > 3) { due = 3; emuAcc = 0; }
  }
  if (fastForward) {
    ffAcc += FF_FACTOR[ffSpeed] * (hz120 ? std::min(loopGapMs, 50.0) * NES_RATE / 1000 : 1);  // per shown frame
    due = (int)ffAcc; ffAcc -= due;
  }
  if (due <= 0) { workMs += nowMs() - t0; return; }

  int p = 0, p2 = 0;
  for (int i = 0; i < due; i++) {
    turbo++;
    p = nesPad(bits); p2 = behindMenu ? 0 : nesPad(bits2, faceMap2);
    bool last = i == due - 1;
    bool ok;
    if (last) ok = emulateShown(p, p2, fastForward ? 0 : runAhead, !fastForward);
    else { C->set_audio_discard(1); ok = C->frame(p, p2, 0) >= 0; C->audio_take(); C->set_audio_discard(0); }
    if (!ok) { quitGame(); showMessage("Something went wrong:\nEmulation stopped.", [] { mode = "browser"; }); return; }
    emuCount++;
    // Rewind history: a snapshot every REWIND_EVERY frames.
    if (rewindOn && snapsReady && ++rewindTick >= REWIND_EVERY) {
      rewindTick = 0;
      C->snap_save(1 + rewindHead);
      rewindHead = (rewindHead + 1) % REWIND_CAP;
      if (rewindCount < REWIND_CAP) rewindCount++;
    }
  }
  {
    // Audio rate control (every frame): the queued amount, measured to the
    // sample and smoothed, is steered to the target by a PI loop around the
    // base ratio (60 Hz: display/NES rate difference; 120 Hz: 1, emulation
    // follows the clock). Corrections stay within +-0.5%, inaudible as pitch.
    double target = AUDIO_TARGET_FRAMES[audioBuffer];
    pendEma += (audio::queuedFrames() - pendEma) * 0.05;
    double err = (target - pendEma) / target;
    audioInteg = std::max(-0.004, std::min(0.004, audioInteg + err * 0.00002));
    double base = (hz120 || highRefresh()) ? 1.0 : NES_RATE / std::max(59.5, std::min(60.5, 1000.0 / tickMs));
    double r = base * (1 + std::max(-0.005, std::min(0.005, err * 0.004 + audioInteg)));
    if (++ratioTicks >= 4) { ratioTicks = 0; setAudioRatio(round(r * 1e5) / 1e5); }
    // Ran dry (a long stall): refill with a little silence instead of crackling.
    if (audio::queuedFrames() < 200) { audio::prefill((int)target / 2); pendEma = target / 2; }
  }
  gfx::pictureUpload(picture, C->video());
  emuFrames += 1;
  if (signalMode) uploadColors();
  frameDrawn = true;
  double t1 = nowMs();
  workMs += t1 - t0;
  // Battery save: every 4 s, write when the save RAM changed.
  if (running && running->battery && t1 - running->sramCheck >= 4000) { running->sramCheck = t1; saveSram(false); }
}

static string dispLabel() {
  char b[48];
  if (dispKnown) snprintf(b, sizeof b, "%u Hz%s", dispHz, dispVrr ? " VRR" : "");
  else snprintf(b, sizeof b, "%.0f Hz", 1000.0 / tickMs);
  return b;
}
// CPU load of the core this thread runs on, from the kernel's idle-tick
// counter (all work on that core, not only ours); -1 if unavailable.
static u64 idleLast = 0, tickLast = 0;
static double coreLoad() {
  u64 idle = 0, tick = armGetSystemTick();
  if (R_FAILED(svcGetInfo(&idle, InfoType_IdleTickCount, INVALID_HANDLE, svcGetCurrentProcessorNumber()))) return -1;
  double load = -1;
  if (tickLast && tick > tickLast && idle >= idleLast) load = 1.0 - (double)(idle - idleLast) / (double)(tick - tickLast);
  idleLast = idle; tickLast = tick;
  return load < 0 ? -1 : std::min(1.0, load);
}
static void updateStats(double t) {
  if (!running || t - statsAt < 1000) return;
  double secs = (t - statsAt) / 1000, avg, low;
  videoFps(avg, low);
  char buf[200];
  if (statsLevel == 1) snprintf(buf, sizeof buf, "%.1f fps", avg);
  else {
    double cpu = coreLoad(), gpu = gfx::gpuMs();
    char cpuS[24], gpuS[24];
    if (cpu >= 0) snprintf(cpuS, sizeof cpuS, "%d%%", (int)lround(cpu * 100)); else snprintf(cpuS, sizeof cpuS, "%d%%", (int)lround(100 * workMs / (t - statsAt)));
    if (gpu >= 0) snprintf(gpuS, sizeof gpuS, "%d%% (%.1f ms)", (int)lround(100 * gpu / tickMs), gpu); else snprintf(gpuS, sizeof gpuS, "n/a");
    snprintf(buf, sizeof buf, "Emu %.1f fps   Video avg %.1f fps   1%% low %.1f fps   CPU %s   GPU %s   %s", emuCount / secs, avg, low, cpuS, gpuS, dispLabel().c_str());
  }
  if (sessionLog < 120) { sessionLog++; record("t%d %s | wait %.2f block %.2f emu %.2f draw %.2f swap %.2f ms, audio q %.0f ms ratio %.4f", sessionLog, buf,
                                               phase[0] / phaseN, phase[1] / phaseN, phase[2] / phaseN, phase[3] / phaseN, phase[4] / phaseN, pendEma / 48.0, audioRatio); }
  for (double& x : phase) x = 0; phaseN = 0;
  swapAcc = waitAcc = swapMax = 0; swapN = 0;
  statsText = buf;
  emuCount = 0; workMs = 0; statsAt = t;
}

static void handleUi(u64 bits) {
  Nav ev = navEvents(bits);
  u64 pressed = ev.pressed;
  double t = nowMs();
  if (mode == "browser") {
    // L/ZL and R/ZR cycle the All/Favourites/Recent tabs from anywhere.
    int tabStep = ((pressed & (B_R | HidNpadButton_ZR)) ? 1 : 0) - ((pressed & (B_L | HidNpadButton_ZL)) ? 1 : 0);
    if (tabStep) {
      int n = (int)LIST_VIEWS.size();
      listView = (listView + tabStep + n) % n; roms.clear(); orderRoms(); selected = 0;
      if (focusArea == "list" && roms.empty()) focusArea = "tabs";
    }
    // Y / X: the Settings and Refresh buttons (B is Exit, below).
    if (pressed & B_Y) { exitArmed = 0; mode = "settings"; settingsIndex = 0; settingsTop = 0; return; }
    if (pressed & B_X) { exitArmed = 0; scan(); showToast("List refreshed"); return; }
    if (exitArmed && (t > exitArmed || (pressed & ~(u64)(B_A | B_B)) || ev.up || ev.down || focusArea != "buttons" || btnIndex != 2)) exitArmed = 0;
    int VIS = std::max(1, std::min((int)roms.size(), maxRows()));
    if (focusArea == "tabs") {
      if (pressed & B_LEFT) { int v = std::max(0, listView - 1); if (v != listView) { listView = v; roms.clear(); orderRoms(); } }
      if (pressed & B_RIGHT) { int v = std::min((int)LIST_VIEWS.size() - 1, listView + 1); if (v != listView) { listView = v; roms.clear(); orderRoms(); } }
      if (ev.down) { focusArea = roms.empty() ? "buttons" : "list"; selected = 0; }
    } else if (focusArea == "list" && !roms.empty()) {
      if (ev.up) { if (selected == 0) focusArea = "tabs"; else selected--; }
      if (ev.down) { if (selected == (int)roms.size() - 1) focusArea = "buttons"; else selected++; }
      if (pressed & B_LEFT) selected = std::max(0, selected - VIS);
      if (pressed & B_RIGHT) selected = std::min((int)roms.size() - 1, selected + VIS);
      if ((pressed & B_A) && selected < (int)roms.size()) { startGame(roms[selected]); return; }
    } else {
      if (ev.up) { focusArea = roms.empty() ? "tabs" : "list"; if (!roms.empty()) selected = (int)roms.size() - 1; }
      if (pressed & B_LEFT) btnIndex = std::max(0, btnIndex - 1);
      if (pressed & B_RIGHT) btnIndex = std::min(2, btnIndex + 1);
      if (pressed & B_A) {
        if (btnIndex == 0) { mode = "settings"; settingsIndex = 0; settingsTop = 0; }
        else if (btnIndex == 1) { scan(); showToast("List refreshed"); }
        else if (exitArmed) quitApp = true;
        else exitArmed = t + 3000;
        return;
      }
    }
    if (pressed & B_B) { if (exitArmed) quitApp = true; else { focusArea = "buttons"; btnIndex = 2; exitArmed = t + 3000; } }
    if (roms.empty() && focusArea == "list") focusArea = "buttons";
  } else if (mode == "folders") {
    int total = pickRows(), up = pickAtRoot() ? 0 : 1;
    if (ev.up) pickSel = (pickSel - 1 + total) % total;
    if (ev.down) pickSel = (pickSel + 1) % total;
    if (pressed & B_A) {
      if (pickSel == 0) {
        romDir = pickAtRoot() ? "sdmc:" : pickPath;
        saveSettings(); scan(); mode = "settings";
        showToast(std::to_string(allRoms.size()) + (allRoms.size() == 1 ? " game in " : " games in ") + (romDir.size() > 5 ? romDir.substr(5) : string("/")));
      } else if (up && pickSel == 1) pickLoad(parentDir(pickPath));
      else pickLoad((pickAtRoot() ? string("sdmc:") : pickPath) + "/" + pickDirs[pickSel - 1 - up]);
    } else if (pressed & B_B) mode = "settings";
  } else if (mode == "settings") {
    int nItems = (int)SETTINGS_ITEMS.size();
    if (ev.up || ev.down || (pressed & B_B)) resetArmed = false;
    if (ev.up) settingsIndex = (settingsIndex - 1 + nItems + 1) % (nItems + 1);
    if (ev.down) settingsIndex = (settingsIndex + 1) % (nItems + 1);
    if (settingsIndex < nItems) {
      SettingItem it = SETTINGS_ITEMS[settingsIndex];  // copy: the advanced toggle rebuilds the list
      int n = (int)it.choices().size();
      if (pressed & B_LEFT) it.set((it.get() - 1 + n) % n);
      else if (pressed & (B_RIGHT | B_A)) it.set((it.get() + 1) % n);
    } else if (pressed & B_A) { saveSettings(); mode = "browser"; return; }
    if (pressed & B_B) { saveSettings(); mode = "browser"; }
  } else if (mode == "menu") {
    bool lr = (bits & (B_L | B_R)) == (u64)(B_L | B_R);
    if (lr && !comboLatch) { comboLatch = true; closeMenu(); return; }
    if (!lr) comboLatch = false;
    int n = (int)menuItems.size();
    if (ev.up) menuIndex = (menuIndex - 1 + n) % n;
    if (ev.down) menuIndex = (menuIndex + 1) % n;
    MenuItem item = menuItems[menuIndex];  // copy: actions may replace the list
    if (item.option()) {
      if (pressed & B_LEFT) cycleMenuOption(-1);
      if (pressed & (B_RIGHT | B_A)) cycleMenuOption(1);
    } else if (pressed & B_A) item.action();
    if ((pressed & B_B) && mode == "menu") closeMenu();
  } else if (mode == "ggedit") {
    handleCodeEditor(ev);
  } else if (mode == "message") {
    if (pressed & (B_A | B_B)) { mode = "browser"; auto then = messageThen; messageThen = nullptr; if (then) then(); }
  }
}

int main(int argc, char** argv) {
  romfsInit();
  mkdir("sdmc:/switch", 0777); mkdir(DATA_DIR, 0777);
  // First launch only (no settings file yet): create the default ROM folder.
  // Later launches leave the SD card alone, so people who keep ROMs elsewhere
  // (and removed roms/nes) don't get it back.
  bool firstLaunch = !exists(SETTINGS());
  if (firstLaunch) { mkdir("sdmc:/roms", 0777); mkdir(DEFAULT_ROM_DIR, 0777); saveSettings(); }
  logFile = fopen((string(DATA_DIR) + "/log.txt").c_str(), "w");
  record("SW2NES %s", APP_VERSION);

  string error;
  if (!gfx::init(error)) { record("gfx: %s", error.c_str()); if (logFile) fclose(logFile); romfsExit(); return 1; }
  string audioError;
  bool sound = audio::init(audioError);
  if (!sound) record("audio: %s", audioError.c_str());

  padConfigureInput(2, HidNpadStyleSet_NpadStandard);
  PadState pad1, pad2;
  padInitialize(&pad1, HidNpadIdType_No1, HidNpadIdType_Handheld);
  padInitialize(&pad2, HidNpadIdType_No2);

  W = C->width(); H = C->height();
  picture = gfx::pictureCreate(W, H);
  {
    string slog;
    int n = gfx::loadShaders(SHADER_FILES, slog);
    record("gpu timer: %s", gfx::gpuTimerAvailable() ? "yes" : "no");
  record("shaders: %d/%d compiled%s%s", n, (int)SHADER_FILES.size(), slog.empty() ? "" : " - ", slog.c_str());
  }
  graphImage = gfx::imageCreate(GRAPH_W, GRAPH_H);
  resetGraph();
  // (built after loading settings: the advanced toggle decides which are shown)

  loadSettings();
  loadPlaytime();
  rebuildSettings();
  scan();
  if (!sound) showToast("Audio unavailable", 4000);
  padUpdate(&pad1);
  lastBits = padGetButtons(&pad1);  // ignore the A press that launched the app

  gfx::setHighRefresh(displayMode == 1);

  while (appletMainLoop() && !quitApp) {
    // Order for low latency: wait for the previous frame, start this frame
    // (the display wait happens here), optional frame delay, THEN read input
    // and emulate, so the picture shows input read as late as possible.
    double ta = nowMs();
    if (frameQueue == 1) gfx::waitPrevious();
    double tb = nowMs();
    loopGapMs = loopStartPrev ? tb - loopStartPrev : 16.69;  // time since the previous refresh
    loopStartPrev = tb;
    bool gameView = running && (mode == "running" || mode == "menu" || mode == "ggedit");
    gfx::beginFrame(gameView ? gfx::Color{0, 0, 0, 1} : col(C_BG));
    double tc = nowMs();
    // 120 Hz / VRR: present once per NES frame (60.0988 Hz) on a clock, so a
    // VRR screen refreshes exactly with the game (even frame times, no audio
    // stretch); input is read right after the wait (late, like frame delay).
    if (highRefresh() && running && (mode == "running" || (mode == "menu" && !pauseInMenu))) {
      double period = 1000.0 / NES_RATE, tnow = nowMs();
      if (!vrrNext || tnow - vrrNext > period * 2) vrrNext = tnow;  // start or resync after a stall
      while (nowMs() < vrrNext - 0.25) svcSleepThread(250000);
      vrrNext += period;
    } else vrrNext = 0;
    if (mode == "running" && !highRefresh() && FRAME_DELAY_MS[frameDelay] && tickMs > 12) {
      double until = tc + FRAME_DELAY_MS[frameDelay];
      while (nowMs() < until) svcSleepThread(200000);
    }
    padUpdate(&pad1); padUpdate(&pad2);
    u64 bits = padGetButtons(&pad1), bits2 = padGetButtons(&pad2);
    frameDrawn = false;
    if (mode == "running") runFrame(bits, bits2, false);
    else {
      if (mode == "menu" && running && !pauseInMenu) runFrame(0, 0, true);
      handleUi(bits);
    }
    double t = nowMs();
    if (toastUntil && t > toastUntil) toastUntil = 0;
    if (hintUntil && t > hintUntil) hintUntil = 0;
    if (gameView != (running && (mode == "running" || mode == "menu" || mode == "ggedit"))) gfx::beginFrame(running ? gfx::Color{0, 0, 0, 1} : col(C_BG));
    if (running && (mode == "running" || mode == "menu" || mode == "ggedit")) {
      drawGame(mode != "running");
      if (mode == "menu") drawMenu();
      if (mode == "ggedit") drawCodeEditor();
    } else if (mode == "settings") drawSettings();
    else if (mode == "folders") drawFolderPicker();
    else drawBrowser();
    if (mode == "message") drawMessage();
    if (running && mode == "running" && (rewinding || fastForward)) {
      string tag = rewinding ? (rewindCount ? "<< Rewind" : "<< Rewind (start)") : ">> " + FF_SPEEDS[ffSpeed];
      float w = gfx::textWidth(tag, 22, gfx::BOLD) + 28;
      gfx::rect(gfx::UI_W - 16 - w, 12, w, 40, gfx::Color{0, 0, 0, 0.6f}, 8);
      textMid(tag, gfx::UI_W - 16 - w + 14, 12, 40, 22, col(C_TEXT), gfx::BOLD);
    }
    if (captureRequested && mode == "running") {
      captureRequested = false;
      char name[64]; time_t tt = time(nullptr); struct tm lt = *localtime(&tt);
      snprintf(name, sizeof name, "%04d%02d%02d-%02d%02d%02d.png", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
      string dir = string(DATA_DIR) + "/screenshots";
      mkdir(dir.c_str(), 0777);
      bool ok = gfx::screenshot((dir + "/" + name).c_str());
      record("screenshot %s: %s", name, ok ? "saved" : "failed");
      showToast(ok ? string("Screenshot saved: ") + name : "Screenshot failed");
    }
    drawToast();
    double td = nowMs();
    gfx::endFrame(frameQueue);
    double te = nowMs();
    phase[0] += tb - ta; phase[1] += tc - tb; phase[2] += t - tc; phase[3] += td - t; phase[4] += te - td; phaseN++;

    double after = nowMs();
    double lastGapForFree = lastSwap ? after - lastSwap : 0;
    if (lastSwap) {
      double gap = after - lastSwap;
      if (gap > 5 && gap < 21) tickMs += (gap - tickMs) * 0.01;
      if (frameDrawn) { noteShown(after); if (statsLevel == 3 && shownCount >= 2) drawGraph(gap); }
    } else if (frameDrawn) noteShown(after);
    lastSwap = after;
    if (!vrrNext && lastGapForFree > 3 && lastGapForFree < 40) freeTickMs = freeTickMs ? freeTickMs + (lastGapForFree - freeTickMs) * 0.05 : lastGapForFree;
    pollDisplay(after);
    if (running && (mode == "running" || (mode == "menu" && !pauseInMenu))) playAccumSec += std::min(loopGapMs, 100.0) / 1000.0;
    if (running && after - playSavedAt >= 300000) { playSavedAt = after; flushPlaytime(running->name); }
    swapAcc += gfx::swapMs; waitAcc += gfx::waitMs; swapN++; if (gfx::swapMs > swapMax) swapMax = gfx::swapMs;
    updateStats(after);
  }

  if (running) quitGame();
  saveSettings();
  gfx::imageDestroy(graphImage);
  gfx::pictureDestroy(picture);
  audio::shutdown();
  gfx::shutdown();
  record("exit");
  if (logFile) fclose(logFile);
  romfsExit();
  return 0;
}
