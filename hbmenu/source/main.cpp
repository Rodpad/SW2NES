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
#include <map>
#include <set>
#include <string>
#include <vector>

#include "audio.h"
#include "core.h"

// The active emulation core (chosen per game start; see Settings "Core").
static const CoreApi* C = &QUICKNES_CORE;
#include "crt.h"
#include "gfx.h"
#include "json.h"
#include "boxart.h"
extern "C" unsigned char* stbi_load(const char* filename, int* x, int* y, int* comp, int req_comp);
extern "C" void stbi_image_free(void* retval_from_stbi_load);
extern "C" int stbi_info(const char* filename, int* x, int* y, int* comp);
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

// Colours: set by the theme (Settings -> App -> Theme).
static uint32_t C_BG = 0x0b0d12, C_PANEL = 0x151924, C_LINE = 0x2a3142, C_TEXT = 0xeef1f7, C_DIM = 0x9aa3b5, C_ACCENT = 0xe4463b, C_SEL = 0x2b3550;
struct Theme { const char* name; uint32_t bg, panel, line, text, dim, accent, sel; uint32_t faceA = 0, faceB = 0, faceX = 0, faceY = 0; float border = 1.6f; bool italic = false; uint32_t key = 0, keyInk = 0; bool faceInk = false; bool square = false; };  // square: square corners  // key / keyInk: every button icon's fill and letters (0: the usual); faceInk: the face colours are the letters' (on key)  // border: how thick its borders are (1.6: all themes); italic: slanted text  // face: button-icon colours (0: the usual)
static const Theme THEMES[] = {
    {"Dark", 0x0b0d12, 0x151924, 0x2a3142, 0xeef1f7, 0x9aa3b5, 0xe4463b, 0x2b3550},
    {"Light", 0xe6e9ef, 0xf7f8fa, 0xc5ccd8, 0x1b1f27, 0x566072, 0xd2382c, 0xd9e0ec},
    {"Black", 0x000000, 0x0a0a0c, 0x26262c, 0xf2f2f4, 0x9a9aa3, 0xe4463b, 0x1c1c22},
    {"Blue", 0x0a1020, 0x101a30, 0x22345a, 0xeaf0ff, 0x9fb0d6, 0x3d8bff, 0x1c2d52},
    {"Purple", 0x110b1c, 0x1b1229, 0x372652, 0xf1ecfa, 0xb2a4cf, 0xa66bff, 0x2e2148},
    {"Red", 0x160a0b, 0x221113, 0x47242a, 0xfaeeee, 0xd0a5a8, 0xff4d4d, 0x3d1c21},
    {"Green", 0x08130d, 0x0f1f15, 0x21402c, 0xeaf8ef, 0x9fcab0, 0x3ccf6e, 0x173a24},
    {"Orange", 0x140e08, 0x21170d, 0x47331d, 0xfcf3ea, 0xd3b79c, 0xff8c2a, 0x3d2a16},
    // Retro Red: the 16-bit retro themes' greys, with the 8-bit pad's red A and B buttons as the accent
    {"Retro Red", 0x1f1e23, 0x34333a, 0x000000, 0xffffff, 0xdcdbe2, 0xff8a7e, 0x4d2b2d, 0xc8242b, 0xc8242b, 0xc8242b, 0xc8242b, 3.0f, false, 0, 0, false, true}};
static const int THEME_COUNT = (int)(sizeof THEMES / sizeof THEMES[0]);
// The saved theme: by name ("themeName"); settings from before that hold a position in the list all the apps
// shared then (Dark .. Orange, Retro Purple ("Retro" before), Retro Colour, Retro Red, Retro Station / Retro Blue). A
// retro theme this app does not have (each console has its own): this app's first retro theme.
static int themeFromSettings(const Json* name, const Json* index) {
  static const char* OLD[] = {"Dark", "Light", "Black", "Blue", "Purple", "Red", "Green", "Orange", "Retro Purple", "Retro Colour", "Retro Red", "Retro Blue"};
  string want;
  if (name && name->isStr()) want = name->s;
  else if (index && index->isNum() && index->n >= 0 && index->n < (int)(sizeof OLD / sizeof OLD[0])) want = OLD[(int)index->n];
  if (want == "Retro" || want == "Retro Station") want = want == "Retro" ? "Retro Purple" : "Retro Blue";
  if (want.empty()) return 0;
  for (int i = 0; i < THEME_COUNT; i++) if (want == THEMES[i].name) return i;
  if (want.rfind("Retro", 0) == 0) for (int i = 0; i < THEME_COUNT; i++) if (string(THEMES[i].name).rfind("Retro", 0) == 0) return i;
  return 0;
}
static int themeIndex = 0;
static uint32_t C_FACE[4] = {0, 0, 0, 0};  // A, B, X, Y icon colours of the theme (0: the usual)
static uint32_t C_KEY = 0, C_KEYINK = 0; static bool C_FACEINK = false;  // the theme's button icon fill and letters (0: the usual)
static float lumOf(uint32_t c) { return (0.299f * ((c >> 16) & 255) + 0.587f * ((c >> 8) & 255) + 0.114f * (c & 255)) / 255; }
// A button icon's fill and letter colours: fi 0-3 the A, B, X, Y face buttons, -1 any other (L, R, +, -, the sticks).
static gfx::Color keyFillOf(int fi, bool dim = false) {
  const uint32_t f = fi >= 0 && C_FACE[fi] && !C_FACEINK ? C_FACE[fi] : C_KEY;
  if (f) return gfx::rgb(f, dim ? 0.45f : 1.0f);
  return gfx::rgb(dim ? C_LINE : C_DIM);
}
static gfx::Color keyInkOf(int fi) {
  if (fi >= 0 && C_FACE[fi]) {
    if (C_FACEINK) return gfx::rgb(C_FACE[fi]);
    return lumOf(C_FACE[fi]) > 0.6f ? gfx::rgb(0x1e1b2a) : gfx::rgb(0xffffff);  // dark letters on a light icon, white on a dark one
  }
  return gfx::rgb(C_KEYINK ? C_KEYINK : C_PANEL);
}
static void applyTheme(int i) {
  themeIndex = i >= 0 && i < THEME_COUNT ? i : 0;
  const Theme& t = THEMES[themeIndex];
  gfx::borderScale = t.border; gfx::italic = t.italic; gfx::squareCorners = t.square;
  C_FACE[0] = t.faceA; C_FACE[1] = t.faceB; C_FACE[2] = t.faceX; C_FACE[3] = t.faceY;
  C_KEY = t.key; C_KEYINK = t.keyInk; C_FACEINK = t.faceInk;
  C_BG = t.bg; C_PANEL = t.panel; C_LINE = t.line; C_TEXT = t.text; C_DIM = t.dim; C_ACCENT = t.accent; C_SEL = t.sel;
}
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
static const int TURBO_FIRST = 2;  // FACE_CHOICES: the first turbo choice (old settings)
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
    "Brightness boost", "Stats", "Debug logging", "Run-ahead", "Frame delay (60 Hz)", "Audio buffer", "Display", "Fast-forward speed", "Dim game in menu", "Audio filter", "Turbo speed"};
static const vector<string> ADVANCED_MENU = {"Run-ahead", "Stats", "Scale", "Overscan", "Signal", "Curvature", "Corner radius", "Corner softness", "Vignette",
    "Vignette size", "Brightness boost", "Settings for", "Save screenshot", "Cheats..."};
static int cornerRadius = 1, cornerSoftness = 1, vignette = 1, vignetteSize = 0, curvature = 0, shader = 6, signalMode = 0, boost = 0;
// Face button mapping per player (Switch A, B, X, Y -> FACE_CHOICES).
static int faceMap[4] = {0, 0, 1, 1}, faceMap2[4] = {0, 0, 1, 1};
static int remapPlayer = 0;  // Settings: whose buttons the mapping rows edit (not saved)
static int* curMap() { return remapPlayer ? faceMap2 : faceMap; }

// Button mapping, per player: for each NES button (PAD_BUTTONS order) the Switch input that presses
// it (bit: 0-15 buttons, 16-23 stick directions, 24-27 the Joy-Cons' SL / SR), or -1 for none. The
// remap screen (Settings -> Sound & controls, or a game's own from the in-game Controls tab) sets it.
enum { NPAD = 12 };
struct PadButton { const char* name; int target; int def; bool turbo; };
static const PadButton PAD_BUTTONS[NPAD] = {
    {"A", N_A, 1, false}, {"A (second button)", N_A, 2, false}, {"B", N_B, 3, false}, {"B (second button)", N_B, 0, false},
    {"Start", N_START, 10, false}, {"Select", N_SELECT, 11, false},
    {"D-pad up", N_UP, 13, false}, {"D-pad down", N_DOWN, 15, false}, {"D-pad left", N_LEFT, 12, false}, {"D-pad right", N_RIGHT, 14, false},
    {"Turbo A", N_A, -1, true}, {"Turbo B", N_B, -1, true}};
// Standard layout: Switch B and X press NES A, Y and A press NES B, + Start, - Select.
// A single Joy-Con held sideways, on the standard layout: the same by position (bottom and top A,
// right and left B), its + or - Start, SL Select and SR Start, the stick as the D-pad.
static const int JOY_LEFT_MAP[NPAD] = {12, 14, 15, 13, 11, 24, 18, 16, 17, 19, -1, -1};
static const int JOY_RIGHT_MAP[NPAD] = {0, 3, 2, 1, 10, 26, 20, 22, 23, 21, -1, -1};
static const int JOY_LEFT_START2 = 25, JOY_RIGHT_START2 = 27;  // SR: Start as well
static int padMap[2][NPAD];
struct GamePadMap { int m[2][NPAD]; };
static std::map<string, GamePadMap> gamePadMaps;  // games with their own layout
static void resetPadMap(int player) { for (int i = 0; i < NPAD; i++) padMap[player][i] = PAD_BUTTONS[i].def; }
static bool padMapIsDefault(int player) { for (int i = 0; i < NPAD; i++) if (padMap[player][i] != PAD_BUTTONS[i].def) return false; return true; }
static const u64 MAPPABLE = 0xFFFFFFull | (0xFull << 24);
static const char* switchButtonName(int bit) {
  static const char* N[28] = {"A", "B", "X", "Y", "Left stick press", "Right stick press", "L", "R", "ZL", "ZR", "+", "-",
                              "D-pad left", "D-pad up", "D-pad right", "D-pad down",
                              "Left stick left", "Left stick up", "Left stick right", "Left stick down",
                              "Right stick left", "Right stick up", "Right stick right", "Right stick down",
                              "SL (left Joy-Con)", "SR (left Joy-Con)", "SL (right Joy-Con)", "SR (right Joy-Con)"};
  return bit >= 0 && bit < 28 ? N[bit] : "";
}
// Debug logging (Settings -> App, advanced): off by default, so normal play writes no log.
static bool debugLog = false;
// Touch always works (taps and drags on every screen). "UI size" (Settings -> App): Compact, or
// Normal with taller rows and larger row text (easier to tap).
static bool gameTitles = true;  // the game list shows downloaded titles instead of file names (Settings -> App -> Game names)
static bool gridView = true;  // the game list as a grid of box covers (Settings -> App -> Game list view); without the setting: grid if online
static bool artAsked = false;  // asked once whether to download box art for games without it
static int gridCols = 1, gridRowsShown = 1;
// The grid scrolls smoothly (pixels, not rows): a drag follows the finger and a flick glides on; the D-pad and
// paging slide it to the highlighted game.
static float gridScroll = 0, gridScrollTo = 0, gridFlingV = 0, gridDragV = 0;
static bool gridDragging = false;  // the grid's columns as last drawn (the D-pad moves by them)
static bool detailsPanel = true;  // the game list's box art and description panel (Settings -> App)
static bool touchControls = true;  // UI size: Normal (the default) or Compact  // the larger spacing
// The in-game on-screen Menu button (Settings -> App): 0 when the game is touched (3 s), 1 always, 2 hidden.
static int menuButtonMode = 0;
static const vector<string> MENU_BUTTON_MODES = {"When touched", "Always", "Hidden"};
static int ROW_H = 72;  // 72 at UI size Normal (the default), 40 at Compact
// Touch controls make everything to tap larger: tab rows, buttons, the scroll bar.
static const float BTN_RADIUS = 12;  // buttons and tabs: corners like the windows' (14), not pills
static float tabH() { return 48; }                          // a tab (one size, touch or not)
static float tabRowH() { return tabH() + 2; }               // the tab bar's line
static float btnH() { return 56; }                          // a button (one size, touch or not)
// Row text: larger at UI size Normal, filling the taller rows.
static float rowText() { return touchControls ? 24 : 20; }   // a row's label and value
static float rowSmall() { return touchControls ? 19 : 16; }  // secondary text in a row (time played, dates)
static float headText() { return touchControls ? 16 : 14; }  // column headings
static float barW() { return touchControls ? 14 : 8; }      // the scroll bar
// A drag scrolls the page (as on a phone), leaving the selection where it is; the next button
// press brings the view back to the selection.
static bool touchScrolled = false;
struct Hit { float x, y, w, h; std::function<void()> tap; std::function<void(int)> drag; std::function<void(float, float)> move; };  // drag: rows moved (+ down); move: follows the finger (UI units per frame)
static std::vector<Hit> hits;  // registered while drawing; the next frame's touches use them
static void addHit(float x, float y, float w, float h, std::function<void()> tap, std::function<void(int)> drag = nullptr) {
  hits.push_back(Hit{x, y, w, h, std::move(tap), std::move(drag), nullptr});
}
static void addMoveHit(float x, float y, float w, float h, std::function<void(float, float)> move) {
  hits.push_back(Hit{x, y, w, h, nullptr, nullptr, std::move(move)});
}
// The finger now (UI units): what it is on lights up while held; a tap acts on release.
static bool touchIsDown = false, touchDragging = false;
static float touchX = 0, touchY = 0, touchStartX = 0, touchStartY = 0;
static bool touchHolding(float x, float y, float w, float h) {
  auto in = [&](float px, float py) { return px >= x && px < x + w && py >= y && py < y + h; };
  return touchIsDown && !touchDragging && in(touchX, touchY) && in(touchStartX, touchStartY);
}
static u64 touchPress = 0;
static bool tapThisFrame = false;  // a tap was let go this frame (its target was already lit while held)  // buttons a tap presses for one frame (read by the menus)
static bool touchUp = false, touchDown = false;  // a tap's one-step move
static int turboRate = 1, volumeLevel = 10, audioFilter = 0;
static bool autoLoadState = false, pauseInMenu = false, dimInMenu = false;
// At most one frame in flight: input is read only after the previous frame
// finished on the GPU (the driver default let frames queue up: 100+ ms lag).
static const int frameQueue = 1;
static vector<string> recentGames, favourites, playHistory;
static string searchQuery;  // game list filter (+ / Search), not saved
static Json lastPlayedDates = Json::object();

enum RomSort { DefaultOrder, NameAsc, NameDesc, PlayedAsc, PlayedDesc, AddedAsc, AddedDesc };
static const vector<string> ROM_SORT_LABELS = {
    "Default order", "Name (A-Z)", "Name (Z-A)", "Last played (oldest first)",
    "Last played (newest first)", "Date added (oldest first)", "Date added (newest first)"
};
static const vector<int> DEFAULT_ROM_SORT_BY_VIEW = {NameAsc, NameAsc, PlayedDesc};
static vector<int> romSortByView = DEFAULT_ROM_SORT_BY_VIEW;

struct Rom { string name, dir; u64 added = 0; };  // file name (.nes or .zip) and its folder
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
static std::function<void()> messageNo;  // a question (showQuestion): B or its second button
static bool messageAsk = false;
static string messageYesLabel, messageNoLabel;
static int settingsIndex = 0, settingsTop = 0;

static double nowMs() { return armTicksToNs(armGetSystemTick()) / 1e6; }
static FILE* logFile = nullptr;
static void openLog() {
  if (logFile) return;
  remove((string(DATA_DIR) + "/log.prev.txt").c_str());
  rename((string(DATA_DIR) + "/log.txt").c_str(), (string(DATA_DIR) + "/log.prev.txt").c_str());
  logFile = fopen((string(DATA_DIR) + "/log.txt").c_str(), "w");
}
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
static bool isRomExt(const string& n) { return hasExt(n, ".nes"); }
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
  Json sorts = Json::array();
  for (int sort : romSortByView) sorts.a.push_back(Json::number(sort));
  j.set("romSortByView", sorts); j.set("playHistory", strings(playHistory));
  j.set("lastPlayedDates", lastPlayedDates);
  j.set("romDir", Json::string(romDir));
  j.set("advanced", Json::boolean(showAdvanced)); j.set("debugLog", Json::boolean(debugLog)); j.set("touch", Json::boolean(touchControls)); j.set("menuButton", Json::number(menuButtonMode)); j.set("detailsPanel", Json::boolean(detailsPanel)); j.set("gridView", Json::boolean(gridView)); j.set("artAsked", Json::boolean(artAsked)); j.set("gameTitles", Json::boolean(gameTitles)); j.set("theme", Json::number(themeIndex)); j.set("themeName", Json::string(THEMES[themeIndex].name));
  for (int pl = 0; pl < 2; pl++) { Json m = Json::array(); for (int i = 0; i < NPAD; i++) m.a.push_back(Json::number(padMap[pl][i])); j.set("padMap" + std::to_string(pl + 1), m); }
  {
    Json g = Json::object();
    for (auto& kv : gamePadMaps) { Json a = Json::array(); for (int pl = 0; pl < 2; pl++) { Json m = Json::array(); for (int i = 0; i < NPAD; i++) m.a.push_back(Json::number(kv.second.m[pl][i])); a.a.push_back(m); } g.set(kv.first, a); }
    j.set("gamePadMaps", g);
  } j.set("core", Json::number(coreSel));
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
  romSortByView = DEFAULT_ROM_SORT_BY_VIEW;
  if (const Json* sorts = v.get("romSortByView"); sorts && sorts->isArr()) {
    for (size_t i = 0; i < std::min(sorts->a.size(), romSortByView.size()); i++)
      romSortByView[i] = pick(&sorts->a[i], ROM_SORT_LABELS.size(), romSortByView[i]);
  }
  playHistory = recentGames;  // seed from the existing Recent tab on upgrade
  if (const Json* h = v.get("playHistory"); h && h->isArr()) {
    playHistory.clear();
    for (const auto& entry : h->a) if (entry.isStr()) playHistory.push_back(entry.s);
  }
  if (const Json* b = v.get("autoLoad")) if (b->isBool()) autoLoadState = b->b;
  if (const Json* b = v.get("pauseMenu")) if (b->isBool()) pauseInMenu = b->b;
  if (const Json* b = v.get("dimMenu")) if (b->isBool()) dimInMenu = b->b;
  statsLevel = pick(v.get("statsLevel"), STATS_LEVELS.size(), statsLevel);
  if (const Json* b = v.get("advanced")) if (b->isBool()) showAdvanced = b->b;
  // Button layouts. Settings from before the remap screen had "faces2" / "facesP2": Switch A, B, X,
  // Y, L, R each pressing a button or its turbo; those become the new layout once.
  static const int FACE_SWITCH_BIT[4] = {0, 1, 2, 3};
  for (int pl = 0; pl < 2; pl++) {
    resetPadMap(pl);
    if (const Json* m = v.get("padMap" + std::to_string(pl + 1))) {
      if (m->isArr() && m->a.size() == NPAD)
        for (int i = 0; i < NPAD; i++) if (m->a[i].isNum()) { const int b = (int)m->a[i].n; padMap[pl][i] = (b >= 0 && b < 28 && (MAPPABLE >> b & 1)) ? b : -1; }
    } else if (const Json* f = v.get(pl ? "facesP2" : "faces2"); f && f->isArr() && [&] { for (size_t k = 0; k < f->a.size() && k < FACE_KEYS.size(); k++) if (!f->a[k].isNum() || (int)f->a[k].n != FACE_DEFAULT[k]) return true; return false; }() && f->a.size() == 4) {
      for (int i = 0; i < NPAD; i++) for (int k = 0; k < 4; k++) if (PAD_BUTTONS[i].def == FACE_SWITCH_BIT[k]) padMap[pl][i] = -1;
      for (int k = 0; k < 4; k++) {
        const int t = f->a[k].isNum() ? (int)f->a[k].n : k;
        for (int i = 0; i < NPAD; i++) {
          const bool turbo = t >= TURBO_FIRST && t < (int)FACE_CHOICES.size() - 1, plain = t >= 0 && t < TURBO_FIRST;  // (the last choice: None)
          const int target = plain ? FACE_TARGETS[t] : turbo ? FACE_TARGETS[t - TURBO_FIRST] : -1;
          if (target >= 0 && PAD_BUTTONS[i].target == target && PAD_BUTTONS[i].turbo == turbo && padMap[pl][i] < 0) { padMap[pl][i] = FACE_SWITCH_BIT[k]; break; }
        }
      }
    }
  }
  gamePadMaps.clear();
  if (const Json* g = v.get("gamePadMaps")) if (g->type == Json::OBJ)
    for (auto& kv : g->o) if (kv.second.isArr() && kv.second.a.size() == 2) {
      GamePadMap gm; bool ok = true;
      for (int pl = 0; pl < 2 && ok; pl++) {
        const Json& m = kv.second.a[pl]; if (!m.isArr() || m.a.size() != NPAD) { ok = false; break; }
        for (int i = 0; i < NPAD; i++) { const int b = m.a[i].isNum() ? (int)m.a[i].n : -1; gm.m[pl][i] = (b >= 0 && b < 28 && (MAPPABLE >> b & 1)) ? b : -1; }
      }
      if (ok) gamePadMaps[kv.first] = gm;
    }
  if (const Json* d = v.get("debugLog")) if (d->isBool()) debugLog = d->b;
  if (const Json* d = v.get("touch")) if (d->isBool()) touchControls = d->b;
  menuButtonMode = pick(v.get("menuButton"), MENU_BUTTON_MODES.size(), menuButtonMode);
  if (const Json* d = v.get("detailsPanel")) if (d->isBool()) detailsPanel = d->b;
  if (const Json* d = v.get("gridView")) if (d->isBool()) gridView = d->b;
  if (const Json* d = v.get("artAsked")) if (d->isBool()) artAsked = d->b;
  if (const Json* d = v.get("gameTitles")) if (d->isBool()) gameTitles = d->b;
  applyTheme(themeFromSettings(v.get("themeName"), v.get("theme")));
  ROW_H = touchControls ? 72 : 40;
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
// Whether the console is online now (the system's own connection status; no request is made).
static bool internetConnected() {
  if (R_FAILED(nifmInitialize(NifmServiceType_User))) return false;
  NifmInternetConnectionType type; u32 strength = 0; NifmInternetConnectionStatus status;
  const bool ok = R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) && status == NifmInternetConnectionStatus_Connected;
  nifmExit();
  return ok;
}
static void loadSettings() {
  for (int pl = 0; pl < 2; pl++) resetPadMap(pl);  // the standard layout, before the defaults are captured
  defaultSettings = settingsJson(gameValues());
  string t; Json v;
  // the game list view, when not set yet: the grid of covers if the console is online (box art can be
  // downloaded), else the list (offline players would only see empty covers)
  if (!readText(SETTINGS(), t) || !Json::parse(t, v) || v.type != Json::OBJ) { gridView = internetConnected(); return; }
  applySettingsJson(v);
  if (!v.get("gridView")) gridView = internetConnected();
  if (const Json* dates = v.get("lastPlayedDates"); dates && dates->type == Json::OBJ)
    lastPlayedDates = *dates;
  displaySaved = v.get("display") != nullptr;
  if (const Json* d = v.get("frameDelay")) if (d->isNum() && d->n >= FRAME_DELAYS.size()) frameDelay = (int)FRAME_DELAYS.size() - 1;  // 12 ms -> 8 ms cap
}
// Settings -> Reset all settings: back to the built-in defaults (recent games,
// favourites and per-game settings files are kept).
static void orderRoms();
static void resetAllSettings() {
  vector<string> rec = recentGames, fav = favourites, history = playHistory;
  applySettingsJson(defaultSettings);
  recentGames = rec; favourites = fav; playHistory = history;
  orderRoms();
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
static string displayName(const string& name);  // the title or the file name, as the list shows it
static u64 romAddedTimeCached(const string& path);
static void orderRoms() {
  const int romSort = romSortByView[listView];
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
  if (romSort != DefaultOrder) {
    std::map<string, size_t> playedRank;
    if (romSort == PlayedAsc || romSort == PlayedDesc) {
      for (size_t i = 0; i < playHistory.size(); i++) playedRank.emplace(playHistory[i], i);
    }
    const auto rank = [&](const Rom& r) {
      auto it = playedRank.find(r.name);
      return it == playedRank.end() ? playHistory.size() : it->second;
    };
    if (romSort == AddedAsc || romSort == AddedDesc) for (Rom& r : list) r.added = romAddedTimeCached(r.dir + "/" + r.name);  // (read only for this sort)
    std::stable_sort(list.begin(), list.end(), [&](const Rom& a, const Rom& b) {
      if (romSort == PlayedAsc || romSort == PlayedDesc) {
        const size_t x = rank(a), y = rank(b);
        if ((x == playHistory.size()) != (y == playHistory.size())) return x < y;
        if (x != y) return romSort == PlayedDesc ? x < y : x > y;
      } else if (romSort == AddedAsc || romSort == AddedDesc) {
        if ((a.added == 0) != (b.added == 0)) return a.added != 0;
        if (a.added != b.added) return romSort == AddedAsc ? a.added < b.added : a.added > b.added;
      }
      const string an = displayName(a.name), bn = displayName(b.name);
      return romSort == NameDesc ? naturalLess(bn, an) : naturalLess(an, bn);
    });
  }
  if (!searchQuery.empty()) {
    const string q = lower(searchQuery);
    vector<Rom> kept;
    for (auto& r : list) if (lower(displayName(r.name)).find(q) != string::npos || lower(stripNes(r.name)).find(q) != string::npos) kept.push_back(r);  // the title or the file name
    list = kept;
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
static string dateFormat = "%Y-%m-%d";
static std::map<string, string> lastPlayedLabels;
// The console language's short date order (romfs:/date-patterns.tsv, generated from CLDR by
// tools/make_date_patterns.cpp), falling back from e.g. "en-GB" to "en".
static void loadDateFormat() {
  u64 code = 0;
  if (R_FAILED(setInitialize())) return;
  const Result rc = setGetSystemLanguage(&code);
  setExit();
  if (R_FAILED(rc)) return;
  char language[sizeof code + 1]{};
  memcpy(language, &code, sizeof code);
  string patterns;
  if (!readText("romfs:/date-patterns.tsv", patterns)) return;
  patterns.insert(0, 1, '\n');
  for (string locale = language; !locale.empty();) {
    const string key = "\n" + locale + "\t";
    const size_t match = patterns.find(key);
    if (match != string::npos) {
      const size_t start = match + key.size();
      dateFormat = patterns.substr(start, patterns.find('\n', start) - start);
      return;
    }
    const size_t parent = locale.rfind('-');
    if (parent == string::npos) break;
    locale.resize(parent);
  }
}
// A last-played time (seconds since 1970) as a date in the console's time zone and language.
static string formatLastPlayedDate(const Json* saved) {
  if (!saved || !saved->isNum() || saved->n <= 0 || saved->n >= 253402300800.0) return "";
  struct tm date{};
  TimeCalendarTime local{};
  TimeCalendarAdditionalInfo info{};
  if (R_SUCCEEDED(timeToCalendarTimeWithMyRule((u64)saved->n, &local, &info))) {
    date.tm_year = local.year - 1900;
    date.tm_mon = local.month - 1;
    date.tm_mday = local.day;
  } else {
    const time_t when = (time_t)saved->n;
    if (!gmtime_r(&when, &date)) return "";
  }
  char label[64];
  return strftime(label, sizeof label, dateFormat.c_str(), &date) ? label : "";
}
static const string& lastPlayedLabel(const string& name) {
  auto [it, inserted] = lastPlayedLabels.try_emplace(name);
  if (inserted) it->second = formatLastPlayedDate(lastPlayedDates.get(name));
  return it->second;
}
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
static u64 romAddedTime(const string& path) {
  FsFileSystem* fs = nullptr;
  char translated[FS_MAX_PATH];
  FsTimeStampRaw stamp{};
  if (fsdevTranslatePath(path.c_str(), &fs, translated) != 0 ||
      R_FAILED(fsFsGetFileTimeStampRaw(fs, translated, &stamp)) || !stamp.is_valid) return 0;
  return stamp.created;
}
// Each file's added time, read once (a file's time stamp is a slow call on a big folder).
static std::map<string, u64> addedTimes;
static u64 romAddedTimeCached(const string& path) {
  auto it = addedTimes.find(path);
  if (it == addedTimes.end()) it = addedTimes.emplace(path, romAddedTime(path)).first;
  return it->second;
}
// Games in dir and its subfolders (hidden folders skipped; at most 6 levels; from the SD card's
// root the app and system folders are left out).
static void scanGames(const string& dir, int depth, vector<Rom>& found) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  vector<string> subdirs;
  while (struct dirent* e = readdir(d)) {
    string n = e->d_name;
    if (n.empty() || n[0] == '.') continue;
    bool isDir = e->d_type == DT_DIR;
    if (e->d_type == DT_UNKNOWN) { struct stat st; isDir = stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
    if (isDir) subdirs.push_back(n);
    else if (isRomExt(n) || hasExt(n, ".zip")) found.push_back(Rom{n, dir});  // (its added time: read only when sorting by it)
  }
  closedir(d);
  if (depth >= 6) return;
  const bool root = dir == "sdmc:" || dir == "sdmc:/";
  std::sort(subdirs.begin(), subdirs.end(), [](const string& a, const string& b) { return naturalLess(a, b); });
  for (const string& n : subdirs) {
    if (root && (lower(n) == "switch" || lower(n) == "atmosphere")) continue;
    scanGames((root ? string("sdmc:") : dir) + "/" + n, depth + 1, found);
  }
}
static void showToast(const string& t, double ms = 1800);
// The game list's footer buttons (Play, Favourite, Search, Settings, Refresh, Exit): one flashes in
// the accent colour when used -- by its button, by moving to it and pressing A, or by a tap -- and
// its action runs once the flash has shown.
static int footFlash = -1, footPending = -1;  // 0-5 the footer buttons, 6 the search field, 7 sort
static int headIndex = -1;  // the header row: -1 the current tab, then (tabs = 3) 3 the search field, 4 its clear button (while searching), 5 sort
static double footFlashUntil = 0, footActAt = 0;
// A button press flashes first, then acts; a tap acts at once (it was lit while the finger was on it).
static void pressFooter(int i) { footFlash = footPending = i; footFlashUntil = nowMs() + 140; footActAt = tapThisFrame ? 0 : footFlashUntil; }
// ---- Box art (boxart.h): found on the console, shown on a 3D box beside the game list ----
static boxart::System artSystem() {
  boxart::System s;
  s.softname = "SW2NES";
  s.ssSystemId = 3;  // ScreenScraper's system ID
  s.dataDir = DATA_DIR;
  // ScreenScraper's NES checksums leave out the 16-byte iNES header: with it the lookup misses and the server
  // falls back to a search by name (about 15 s a game instead of about 2)
  s.crcRange = [](const std::vector<uint8_t>& f, size_t& off, size_t& len) { if (f.size() > 16 && f[0] == 'N' && f[1] == 'E' && f[2] == 'S' && f[3] == 0x1A) { off = 16; len = f.size() - 16; } };
  return s;
}
// Downloaded titles, by file name ("" when a game has none), read once from its details file.
// All of them are kept in art/titles.json too, so a start reads one file instead of one a game (each a search of a
// folder of hundreds of files). Rebuilt after a download run.
static std::map<string, string> titleCache;
static bool titleIndexLoaded = false, titleIndexDirty = false;
static string titleIndexPath() { return string(DATA_DIR) + "/art/titles.json"; }
// The details folder's file names, read once (one pass over the folder, not a search of it a game).
static std::set<string> infoFiles; static bool infoFilesRead = false;
static bool hasInfoFile(const string& name) {
  const string p = boxart::infoPath(artSystem(), name);
  const size_t slash = p.rfind('/');
  if (!infoFilesRead) {
    infoFilesRead = true; infoFiles.clear();
    if (DIR* d = opendir(p.substr(0, slash).c_str())) { while (struct dirent* e = readdir(d)) infoFiles.insert(e->d_name); closedir(d); }
  }
  return infoFiles.count(p.substr(slash + 1)) > 0;
}
static void loadTitleIndex() {
  titleIndexLoaded = true;
  string t; Json j;
  if (readText(titleIndexPath(), t) && Json::parse(t, j) && j.type == Json::OBJ)
    for (auto& kv : j.o) if (kv.second.isStr()) titleCache.emplace(kv.first, kv.second.s);
}
static void saveTitleIndex() {
  titleIndexDirty = false;
  Json j = Json::object();
  for (auto& kv : titleCache) j.o.emplace_back(kv.first, Json::string(kv.second));
  writeText(titleIndexPath(), j.dump());
}
// After a download run: titles read again from the details files (and the index written anew).
static void resetTitles() { titleCache.clear(); titleIndexLoaded = true; infoFilesRead = false; remove(titleIndexPath().c_str()); }
static string displayName(const string& name) {
  if (gameTitles) {
    if (!titleIndexLoaded) loadTitleIndex();
    auto it = titleCache.find(name);
    if (it == titleCache.end()) {
      string t, title; Json j;
      if (hasInfoFile(name) && readText(boxart::infoPath(artSystem(), name), t) && Json::parse(t, j) && j.type == Json::OBJ)
        if (const Json* v = j.get("title")) if (v->isStr()) title = v->s;
      it = titleCache.emplace(name, title).first;
      titleIndexDirty = true;
    }
    if (!it->second.empty()) return it->second;
  }
  return stripNes(name);
}
static bool readRom(const Rom& rom, vector<uint8_t>& data, string& err);
// The box art run's progress: "3 / 10 · Sparkster"; the database step as a percentage.
static string boxArtProgress() {
  auto& b = boxart::status();
  if (b.phase == 1 || b.phase == 2) {
    const int pc = b.phase == 2 ? 100 : b.sizeMB > 0 ? std::min(99, b.gotMB * 100 / b.sizeMB) : 0;
    return "Preparing the game database: " + std::to_string(pc) + "%";
  }
  string t = std::to_string(std::min(b.total.load(), b.done.load() + 1)) + " / " + std::to_string(b.total.load());
  if (b.current[0]) t += string("  \u00b7  ") + b.current;  // the title being looked up
  return t;
}
static int boxArtRow = 0;  // which download row started the run (its row shows the progress): 0 missing, 1 all again, 2 test
static void startBoxArt(bool again, int limit = 0) {
  boxArtRow = limit ? 2 : again ? 1 : 0;
  vector<boxart::Game> games;
  for (auto& r : allRoms) games.push_back(boxart::Game{r.name, r.dir + "/" + r.name});
  std::stable_sort(games.begin(), games.end(), [](const boxart::Game& a, const boxart::Game& b) { return naturalLess(stripNes(a.name), stripNes(b.name)); });  // A-Z
  std::map<string, string> dirOf; for (auto& r : allRoms) dirOf[r.name] = r.dir;
  auto read = [dirOf](const boxart::Game& g, std::vector<uint8_t>& out) { auto it = dirOf.find(g.name); string err; return it != dirOf.end() && readRom(Rom{g.name, it->second}, out, err); };
  static bool logSet = false;
  if (!logSet) { boxart::setLog([](const char* l) { record("%s", l); }); logSet = true; }
  if (boxart::start(artSystem(), games, again, read, limit)) showToast("Looking for box art...", 2500);
  else if (!boxart::status().message.empty()) showToast(boxart::status().message, 4000);
}
struct ArtTex {
  gfx::Image* img = nullptr; gfx::Image* back = nullptr; gfx::Image* spineImg = nullptr;  // box front, back, spine
  gfx::Image* snap = nullptr;  // a gameplay screenshot
  Color spine{0.2f, 0.2f, 0.25f, 1}; bool tried = false;
  Color sideColor{0, 0, 0, 1};  // the edges without an image: black, or the side image's average colour
  float sideRatio = 0;  // the side image's short edge against its long one (the box's depth against its length), 0 without one
  bool hasInfo = false; string title, line, line2, bio, source;  // details (boxart::infoPath): publisher; year and genre
};
static std::map<string, ArtTex> artCache;
// The grid's covers: the front image only, a few loaded each frame (no stall on a first view), at most
// COVER_MAX kept (the longest unseen goes).
static const float GRID_ASPECT = 0.72f;  // a cover slot's width against its height (NES boxes are portrait)
struct Cover { gfx::Image* img = nullptr; bool tried = false; double used = 0; };  // tried: decoded (img, or none: no art)
static std::map<string, Cover> coverCache;
static const size_t COVER_MAX = 200;  // (the screen, and two pages above and below it, several times over)
// New art downloaded: a cover is loaded again (all of them, or only those still without one), the old image kept
// on screen until its new one is in, so nothing flashes.
static void staleCovers(bool all) { for (auto& kv : coverCache) if (all || !kv.second.img) kv.second.tried = false; }
// The details panel's textures are freed with the cache (earlier builds lost them each time new art arrived).
static void clearArtCache() {
  for (auto& kv : artCache) for (gfx::Image* im : {kv.second.img, kv.second.back, kv.second.spineImg, kv.second.snap}) if (im) gfx::imageDestroy(im);
  artCache.clear();
}
// An image file (JPEG or PNG) as a texture; px gets its pixels (RGBA) when asked.
// An image of one flat colour (a placeholder some art sources have, e.g. a plain green box back): no art.
static bool plainImage(const vector<uint32_t>& px) {
  if (px.size() < 64) return true;
  double sum[3] = {0, 0, 0}, sq[3] = {0, 0, 0}; int n = 0;
  for (size_t i = 0; i < px.size(); i += px.size() / 997 + 1, n++)
    for (int k = 0; k < 3; k++) { const double v = (px[i] >> (8 * k)) & 255; sum[k] += v; sq[k] += v * v; }
  double var = 0;
  for (int k = 0; k < 3; k++) var += sq[k] / n - (sum[k] / n) * (sum[k] / n);
  return var < 3 * 6 * 6;  // under about 6 levels of spread in each channel
}
static gfx::Image* loadArtImage(const string& path, vector<uint32_t>* px = nullptr, int* w = nullptr, int* h = nullptr) {
  int iw = 0, ih = 0, n = 0;
  // a damaged file: refused before decoding when its size is absurd (downloads are at most 640 px)
  if (!stbi_info(path.c_str(), &iw, &ih, &n) || iw <= 0 || ih <= 0 || iw > 4096 || ih > 4096) return nullptr;
  unsigned char* d = stbi_load(path.c_str(), &iw, &ih, &n, 4);
  if (!d) return nullptr;
  gfx::Image* im = gfx::imageCreateSmooth(iw, ih, (const uint32_t*)d);
  if (px) px->assign((const uint32_t*)d, (const uint32_t*)d + (size_t)iw * ih);
  if (w) *w = iw;
  if (h) *h = ih;
  stbi_image_free(d);
  return im;
}
// Covers are decoded (JPEG / PNG) on their own thread, the ones on screen first, then a page above and below;
// the main thread only makes textures of them (a few a frame), so paging fast does not stall or show empty slots
// for long.
struct CoverJob { string name, path; };
struct CoverDone { string name; int w = 0, h = 0; unsigned char* px = nullptr; };
static Mutex coverMx; static CondVar coverCv;
static vector<CoverJob> coverWanted;  // what to decode next, most wanted first (set again every frame)
static vector<CoverDone> coverDone;   // decoded, waiting for their textures
static std::set<string> coverBusy;    // being decoded now
static const int COVER_THREADS = 3;
static Thread coverThreads[COVER_THREADS]; static int coverThreadCount = 0; static bool coverThreadUp = false, coverStopping = false;
static vector<string> coverWantNow;   // this frame's wanted covers, in order
static void coverMain(void*) {
  for (;;) {
    mutexLock(&coverMx);
    while (!coverStopping && coverWanted.empty()) condvarWait(&coverCv, &coverMx);
    if (coverStopping) { mutexUnlock(&coverMx); return; }
    const CoverJob job = coverWanted.front();
    coverWanted.erase(coverWanted.begin());
    coverBusy.insert(job.name);
    mutexUnlock(&coverMx);
    CoverDone d; d.name = job.name;
    int iw = 0, ih = 0, n = 0;
    if (stbi_info(job.path.c_str(), &iw, &ih, &n) && iw > 0 && ih > 0 && iw <= 4096 && ih <= 4096) d.px = stbi_load(job.path.c_str(), &d.w, &d.h, &n, 4);
    mutexLock(&coverMx);
    coverDone.push_back(d); coverBusy.erase(job.name);
    mutexUnlock(&coverMx);
  }
}
static void coverLoaderStart() {
  if (coverThreadUp) return;
  mutexInit(&coverMx); condvarInit(&coverCv); coverStopping = false;
  // on the process's other cores (not the menu's own), one each where there are enough
  u64 mask = 0; svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
  const int own = (int)svcGetCurrentProcessorNumber();
  vector<int> cores;
  for (int c = 0; c < 64; c++) if ((mask >> c) & 1 && c != own) cores.push_back(c);
  if (cores.empty()) cores.push_back(-2);
  for (int i = 0; i < COVER_THREADS; i++) {
    Thread& th = coverThreads[coverThreadCount];
    if (R_SUCCEEDED(threadCreate(&th, coverMain, nullptr, nullptr, 256 * 1024, 0x2E, cores[i % cores.size()])) && R_SUCCEEDED(threadStart(&th))) coverThreadCount++;
  }
  coverThreadUp = coverThreadCount > 0;
}
// The app's exit: the thread ends before the program does (hbloader runs the next one in this process).
static void coverLoaderStop() {
  if (!coverThreadUp) return;
  mutexLock(&coverMx); coverStopping = true; coverWanted.clear(); condvarWakeAll(&coverCv); mutexUnlock(&coverMx);
  for (int i = 0; i < coverThreadCount; i++) { threadWaitForExit(&coverThreads[i]); threadClose(&coverThreads[i]); }
  coverThreadCount = 0; coverThreadUp = false;
  for (CoverDone& d : coverDone) if (d.px) stbi_image_free(d.px);
  coverDone.clear();
}
// A cover wanted now (drawn, or a page away): its texture if made; asked for if not decoded yet.
// The favourite mark: a yellow five-pointed star with a soft dark edge, drawn once into an image (4 x 4 samples
// a pixel, so its edges are smooth); made again if the graphics were (SW2PS2 after a Vulkan game).
static gfx::Image* starImg = nullptr;
static gfx::Image* starImage() {
  if (starImg) return starImg;
  const int S = 96;
  vector<uint32_t> px((size_t)S * S, 0);
  auto inStar = [](float x, float y, float cx, float cy, float R) {  // even-odd test against the star's outline
    float vx[10], vy[10];
    for (int k = 0; k < 10; k++) { const float a = (-90.0f + k * 36.0f) * 3.14159265f / 180, r = (k & 1) ? R * 0.42f : R; vx[k] = cx + r * cosf(a); vy[k] = cy + r * sinf(a); }
    bool in = false;
    for (int i = 0, j = 9; i < 10; j = i++)
      if (((vy[i] > y) != (vy[j] > y)) && (x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i])) in = !in;
    return in;
  };
  const float cx = S / 2.0f, cy = S / 2.0f + S * 0.04f, R = S * 0.5f;
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    int edge = 0, fill = 0;
    for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
      const float fx = x + (sx + 0.5f) / 4, fy = y + (sy + 0.5f) / 4;
      if (inStar(fx, fy, cx, cy, R * 0.86f)) fill++;
      else if (inStar(fx, fy, cx, cy, R)) edge++;
    }
    const float f = fill / 16.0f, e = edge / 16.0f;
    // yellow (f2c94c) over a dark edge (1a1300 at 60%)
    const float a = f + e * 0.6f;
    if (a <= 0) continue;
    const float r = (0xf2 * f + 0x1a * e * 0.6f) / a, g = (0xc9 * f + 0x13 * e * 0.6f) / a, b = (0x4c * f + 0x00 * e * 0.6f) / a;
    px[(size_t)y * S + x] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)(a * 255) << 24);
  }
  starImg = gfx::imageCreateSmooth(S, S, px.data());
  return starImg;
}
static gfx::Image* coverFor(const string& name) {
  Cover& c = coverCache[name];
  c.used = nowMs();
  if (!c.tried) coverWantNow.push_back(name);
  return c.img;
}
static bool coverMissing(const string& name) { auto it = coverCache.find(name); return it != coverCache.end() && it->second.tried && !it->second.img; }
// Once a frame: textures for what was decoded (a few), the wanted list handed over, the cache kept in size.
static void coverFrame() {
  if (!coverThreadUp) coverLoaderStart();
  vector<CoverDone> got;
  mutexLock(&coverMx);
  const size_t take = std::min<size_t>(6, coverDone.size());
  got.assign(coverDone.begin(), coverDone.begin() + take);
  coverDone.erase(coverDone.begin(), coverDone.begin() + take);
  coverWanted.clear();
  std::set<string> queued;
  for (const string& n : coverWantNow)
    if (!queued.count(n) && !coverBusy.count(n)) { queued.insert(n); coverWanted.push_back(CoverJob{n, boxart::artPath(artSystem(), n)}); }
  if (!coverWanted.empty()) condvarWakeAll(&coverCv);
  mutexUnlock(&coverMx);
  coverWantNow.clear();
  for (CoverDone& d : got) {  // (a reload: the old image stays on screen until now)
    auto it = coverCache.find(d.name);
    if (it == coverCache.end()) { if (d.px) stbi_image_free(d.px); continue; }
    Cover& c = it->second;
    gfx::Image* fresh = d.px ? gfx::imageCreateSmooth(d.w, d.h, (const uint32_t*)d.px) : nullptr;
    if (d.px) stbi_image_free(d.px);
    if (c.img) gfx::imageDestroy(c.img);
    c.img = fresh; c.tried = true;
  }
  while (coverCache.size() > COVER_MAX) {  // the longest unseen go
    auto old = coverCache.begin();
    for (auto it = coverCache.begin(); it != coverCache.end(); ++it) if (it->second.used < old->second.used) old = it;
    if (old->second.img) gfx::imageDestroy(old->second.img);
    coverCache.erase(old);
  }
}
static ArtTex& artFor(const string& name) {
  ArtTex& a = artCache[name];
  if (a.tried) return a;
  a.tried = true;
  {  // details
    string t; Json j;
    if (readText(boxart::infoPath(artSystem(), name), t) && Json::parse(t, j) && j.type == Json::OBJ) {
      auto str = [&](const char* k) { const Json* v = j.get(k); return v && v->isStr() ? v->s : string(); };
      a.hasInfo = true; a.title = str("title"); a.bio = str("bio"); a.source = str("source");
      a.line = !str("publisher").empty() ? str("publisher") : str("developer");
      string tv = str("region"); if (tv.empty()) tv = boxart::tvSystem(name);  // (details downloaded before the region was saved: the file name's)
      for (const string& part : {str("year"), str("genre"), tv}) if (!part.empty()) a.line2 += (a.line2.empty() ? "" : "  \u00b7  ") + part;
    }
  }
  vector<uint32_t> px; int w = 0, h = 0;
  a.img = loadArtImage(boxart::artPath(artSystem(), name), &px, &w, &h);
  if (!a.img) { record("art: %s: no art", name.c_str()); return a; }
  {  // the back: not a flat placeholder
    vector<uint32_t> bpx;
    a.back = loadArtImage(boxart::backPath(artSystem(), name), &bpx);
    if (a.back && plainImage(bpx)) { gfx::imageDestroy(a.back); a.back = nullptr; }
  }
  {  // the side: its proportions give the box's depth, its average colour the two plain edges
    vector<uint32_t> spx; int sw = 0, sh = 0;
    a.spineImg = loadArtImage(boxart::spinePath(artSystem(), name), &spx, &sw, &sh);
    if (a.spineImg && plainImage(spx)) { gfx::imageDestroy(a.spineImg); a.spineImg = nullptr; }  // (a flat placeholder)
    if (a.spineImg && sw > 0 && sh > 0) {
      a.sideRatio = (float)std::min(sw, sh) / std::max(sw, sh);
      double r = 0, g = 0, b = 0;
      for (uint32_t q : spx) { r += q & 255; g += (q >> 8) & 255; b += (q >> 16) & 255; }
      const double n = (double)spx.size() * 255;
      a.sideColor = Color{(float)(r / n), (float)(g / n), (float)(b / n), 1};
    }
  }
  record("art: %s: %dx%d front%s%s", name.c_str(), w, h, a.back ? ", back" : "", a.spineImg ? ", spine" : "");
  // the top and bottom: the front's left edge, averaged and darkened
  double r = 0, g = 0, b = 0; const int edge = std::max(1, w / 30);
  for (int y = 0; y < h; y++) for (int x = 0; x < edge; x++) { const uint32_t p = px[y * w + x]; r += p & 255; g += (p >> 8) & 255; b += (p >> 16) & 255; }
  const double n = (double)h * edge * 255;
  a.spine = Color{(float)(r / n * 0.8), (float)(g / n * 0.8), (float)(b / n * 0.8), 1};
  return a;
}
// A box's depth against its front's height, by region (real boxes measured roughly).
static float boxDepthRatio(const string& region) { return region == "jp" ? 0.18f : 0.24f; }
static void openSearch() {
  SwkbdConfig kbd;
  if (R_FAILED(swkbdCreate(&kbd, 0))) { showToast("The keyboard could not open"); return; }
  swkbdConfigMakePresetDefault(&kbd);
  swkbdConfigSetHeaderText(&kbd, "Search games");
  swkbdConfigSetGuideText(&kbd, "Part of a game's name (empty: show all)");
  swkbdConfigSetInitialText(&kbd, searchQuery.c_str());
  swkbdConfigSetStringLenMax(&kbd, 40);
  char out[128] = "";
  const Result rc = swkbdShow(&kbd, out, sizeof out);
  swkbdClose(&kbd);
  if (R_FAILED(rc)) return;  // cancelled
  searchQuery = out;
  orderRoms();
  showToast(searchQuery.empty() ? "Showing all games" : std::to_string(roms.size()) + (roms.size() == 1 ? " game matches" : " games match"));
}
static void toggleFavourite(const string& name) {
  const bool was = contains(favourites, name);
  favourites.erase(std::remove(favourites.begin(), favourites.end(), name), favourites.end());
  if (!was) favourites.push_back(name);
  saveSettings();
  showToast(was ? "Removed from Favourites" : "Added to Favourites");
  if (listView == 1) orderRoms();
}
static bool artCheckPending = false;  // after a scan: see whether to offer the box art download (maybeAskArt)
static void scan() {
  allRoms.clear();
  // One entry per game name (a ROM file wins over a .zip of the same name; the first found wins
  // over a same-named file deeper in the tree).
  for (const string& dir : {romDir}) {
    vector<Rom> found;
    scanGames(dir, 0, found);
    std::stable_sort(found.begin(), found.end(), [](const Rom& a, const Rom& b) { return isRomExt(a.name) && !isRomExt(b.name); });
    std::set<string> seen;
    for (auto& r : found) if (seen.insert(lower(stripNes(r.name))).second) allRoms.push_back(r);
  }
  orderRoms();
  if (selected >= (int)roms.size()) selected = std::max(0, (int)roms.size() - 1);
  artCheckPending = true;
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
static void showToast(const string& t, double ms) { toastText = t; toastUntil = nowMs() + ms; }
static void showMessage(const string& text, std::function<void()> then) { messageText = text; messageThen = then; messageNo = nullptr; messageAsk = false; mode = "message"; }
static void showQuestion(const string& text, const string& yes, const string& no, std::function<void()> onYes, std::function<void()> onNo) {
  messageText = text; messageThen = onYes; messageNo = onNo; messageAsk = true; messageYesLabel = yes; messageNoLabel = no; mode = "message";
}

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
  playHistory.erase(std::remove(playHistory.begin(), playHistory.end(), rom.name), playHistory.end());
  playHistory.insert(playHistory.begin(), rom.name);
  if (const time_t playedAt = time(nullptr); playedAt > 0) {
    lastPlayedDates.set(rom.name, Json::number((double)playedAt));
    lastPlayedLabels.erase(rom.name);
  }
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
static PadState* padStates[2];  // players 1-2
// An input's name as the player holds the controller: a single Joy-Con held sideways names its four
// buttons and stick directions by where they are now (its upright names would point elsewhere).
static string inputName(int bit, u32 style) {
  if (style == HidNpadStyleTag_NpadJoyLeft) {
    switch (bit) {
      case 15: return "Right button"; case 12: return "Bottom button"; case 14: return "Top button"; case 13: return "Left button";
      case 19: return "Stick right"; case 17: return "Stick left"; case 16: return "Stick down"; case 18: return "Stick up";
      case 4: return "Stick press"; case 24: return "SL"; case 25: return "SR";
    }
  } else if (style == HidNpadStyleTag_NpadJoyRight) {
    switch (bit) {
      case 2: return "Right button"; case 0: return "Bottom button"; case 3: return "Top button"; case 1: return "Left button";
      case 21: return "Stick right"; case 23: return "Stick left"; case 20: return "Stick up"; case 22: return "Stick down";
      case 5: return "Stick press"; case 26: return "SL"; case 27: return "SR";
    }
  }
  return switchButtonName(bit);
}
// A player's controller as the console's pad: the player's layout (this game's own if it has one;
// a single Joy-Con on the standard layout gets the Joy-Con one), turbo buttons pulsing at the turbo
// rate. On a two-handed controller the left stick is also the D-pad.
static int nesPad(int player, u64 b) {
  const int* map = padMap[player];
  bool custom = !padMapIsDefault(player);
  if (running) { auto g = gamePadMaps.find(running->name); if (g != gamePadMaps.end()) { map = g->second.m[player]; custom = true; } }
  const u32 st = padStates[player] ? padGetStyleSet(padStates[player]) : 0;
  const bool single = st == HidNpadStyleTag_NpadJoyLeft || st == HidNpadStyleTag_NpadJoyRight;
  if (!custom && st == HidNpadStyleTag_NpadJoyLeft) map = JOY_LEFT_MAP;
  else if (!custom && st == HidNpadStyleTag_NpadJoyRight) map = JOY_RIGHT_MAP;
  int p = 0;
  const bool turboOn = (turbo % TURBO_PERIOD[turboRate]) * 2 < TURBO_PERIOD[turboRate];
  for (int i = 0; i < NPAD; i++) {
    const int bit = map[i];
    if (bit < 0 || !(b & (1ull << bit))) continue;
    if (!PAD_BUTTONS[i].turbo || turboOn) p |= PAD_BUTTONS[i].target;
  }
  if (!custom && single && (b & (1ull << (st == HidNpadStyleTag_NpadJoyLeft ? JOY_LEFT_START2 : JOY_RIGHT_START2)))) p |= N_START;
  if (!single) {
    if (b & B_SU) p |= N_UP;
    if (b & B_SD) p |= N_DOWN;
    if (b & B_SL) p |= N_LEFT;
    if (b & B_SR) p |= N_RIGHT;
  }
  if ((p & (N_UP | N_DOWN)) == (N_UP | N_DOWN)) p &= ~(N_UP | N_DOWN);
  if ((p & (N_LEFT | N_RIGHT)) == (N_LEFT | N_RIGHT)) p &= ~(N_LEFT | N_RIGHT);
  return p;
}
// In-game menu: L + R with a stick pressed in (either stick).
static const u64 MENU_COMBO = HidNpadButton_L | HidNpadButton_R | HidNpadButton_StickL | HidNpadButton_StickR;
// The menu combo, or on a single Joy-Con held sideways: SL + SR + its stick press.
static bool menuCombo(u64 bits) {
  const u64 left = HidNpadButton_LeftSL | HidNpadButton_LeftSR | HidNpadButton_StickL, right = HidNpadButton_RightSL | HidNpadButton_RightSR | HidNpadButton_StickR;
  const u64 lr = HidNpadButton_L | HidNpadButton_R;
  return ((bits & lr) == lr && (bits & (HidNpadButton_StickL | HidNpadButton_StickR))) || (bits & left) == left || (bits & right) == right;
}
// A controller's buttons as the menus read them. A single Joy-Con held sideways gets the layout by
// position: its four buttons are A (right), B (bottom), X (top), Y (left), its stick moves the
// cursor, SL / SR are L / R (tabs); + / - as they are.
static u64 uiBits(PadState* p, u64 raw) {
  const u32 st = padGetStyleSet(p);
  if (st == HidNpadStyleTag_NpadJoyLeft) {
    u64 b = raw & ~(u64)(HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right | B_SL | B_SU | B_SR | B_SD);
    if (raw & HidNpadButton_StickLDown) b |= B_SR;
    if (raw & HidNpadButton_StickLUp) b |= B_SL;
    if (raw & HidNpadButton_StickLLeft) b |= B_SD;
    if (raw & HidNpadButton_StickLRight) b |= B_SU;
    if (raw & HidNpadButton_Down) b |= B_A;
    if (raw & HidNpadButton_Left) b |= B_B;
    if (raw & HidNpadButton_Right) b |= B_X;
    if (raw & HidNpadButton_Up) b |= B_Y;
    if (raw & HidNpadButton_LeftSL) b |= B_L;
    if (raw & HidNpadButton_LeftSR) b |= B_R;
    return b;
  }
  if (st == HidNpadStyleTag_NpadJoyRight) {
    u64 b = raw & ~(u64)(HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y);
    if (raw & HidNpadButton_X) b |= B_A;
    if (raw & HidNpadButton_A) b |= B_B;
    if (raw & HidNpadButton_Y) b |= B_X;
    if (raw & HidNpadButton_B) b |= B_Y;
    if (raw & HidNpadButton_RightSL) b |= B_L;
    if (raw & HidNpadButton_RightSR) b |= B_R;
    if (raw & HidNpadButton_StickRUp) b |= B_SR;
    if (raw & HidNpadButton_StickRDown) b |= B_SL;
    if (raw & HidNpadButton_StickRLeft) b |= B_SU;
    if (raw & HidNpadButton_StickRRight) b |= B_SD;
    return b;
  }
  return raw;
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
static bool settingsOnBack = false, menuOnBack = false;  // the cursor is on the tab row's Back button
static bool menuHasTabs = false;  // the main in-game menu: its categories show as tabs (L / R)
static const vector<string> MENU_CATS = {"Game", "Emulation", "Picture", "Controls"};
static int menuCat = 0;  // menuIndex -1: the cursor is on the category tabs
static void showMenuItems(const string& title, const vector<MenuItem>& items) { menuItems = items; menuTitle = title; menuIndex = 0; menuTop = 0; menuHasTabs = false; }
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
static void rebuildSettings();
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
static void openRemap(int player, bool forGame);
static void pairControllers();
// The main in-game menu's rows for one category (adv: with the advanced ones).
static vector<MenuItem> menuItemsFor(int cat, bool adv) {
  vector<MenuItem> items = {
      act("Resume", [] { closeMenu(); }),
      act("Save state", [] { saveState(); closeMenu(); }),
      act("Load state", [] { closeMenu(); loadState(false, stateSlot); }),
      opt("Slot", [] { vector<string> a; for (int n = 1; n <= SLOT_COUNT; n++) a.push_back(slotLabel(n)); return a; },
          [] { return stateSlot - 1; }, [](int v) { stateSlot = v + 1; }),
      act("Reset", [] { C->reset(0); closeMenu(); showToast("Reset"); }),
      act("Quit game", [] { quitGame(); }),
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
      opt("Settings for", [] { return vector<string>{"All games", "This game"}; }, [] { return running->perGame ? 1 : 0; }, [](int v) { setPerGame(v != 0); },
          [] { pictureChanged(); refreshPicture(); }),
      act("Save screenshot", [] { closeMenu(); captureRequested = true; }),
      act("Cheats...", [] { openCheatsMenu(); }),
      opt("Rewind (hold ZL)", [] { return OFF_ON; }, [] { return rewindOn ? 1 : 0; }, [](int v) { rewindOn = v != 0; }, saveSettings),
      opt("Fast-forward", [] { return FF_MODES; }, [] { return ffMode; }, [](int v) { ffMode = v; }, saveSettings),
      opt("Turbo speed", [] { return TURBO_RATES; }, [] { return turboRate; }, [](int v) { turboRate = v; }, saveSettings),
      act("Pair controllers", [] { pairControllers(); }),
      act("Player 1 buttons", [] { openRemap(0, true); }),
      act("Player 2 buttons", [] { openRemap(1, true); }),
      act("Use Settings buttons", [] { if (gamePadMaps.erase(running->name)) { saveSettings(); showToast("This game uses the Settings buttons again", 2500); openMenu(); } }),
      act("Reset buttons to standard", [] {  // a second A within 4 s confirms
        static double armedUntil = 0;
        if (nowMs() > armedUntil) { armedUntil = nowMs() + 4000; showToast("Press {A} again to put both players' buttons back to standard", 4000); return; }
        armedUntil = 0; gamePadMaps.erase(running->name); for (int pl = 0; pl < 2; pl++) resetPadMap(pl); saveSettings();
        showToast("Both players' buttons are back to standard (single Joy-Cons: their own default)", 3500); openMenu();
      }),
      opt("Advanced options", [] { return vector<string>{"Hidden", "Shown"}; }, [] { return showAdvanced ? 1 : 0; }, [](int v) { showAdvanced = v != 0; },
          [] {  // the menu's own rows change: rebuild it, the cursor staying on this row
            saveSettings(); rebuildSettings();
            openMenu();
            for (int i = 0; i < (int)menuItems.size(); i++) if (menuItems[i].label == "Advanced options") menuIndex = i;
          }),
  };
  vector<MenuItem> kept;
  static const vector<string> EMU = {"Run-ahead", "Rewind (hold ZL)", "Fast-forward", "Turbo speed", "Stats"};
  static const vector<string> PIC = {"Aspect", "Scale", "Overscan", "Signal", "Shader", "Brightness boost", "Curvature", "Corner radius", "Corner softness",
                                     "Vignette", "Vignette size", "Settings for", "Palette"};
  static const vector<string> CTL = {"Pair controllers", "Player 1 buttons", "Player 2 buttons", "Use Settings buttons", "Reset buttons to standard"};
  auto catOf = [](const string& l) { return contains(EMU, l) ? 1 : contains(PIC, l) ? 2 : contains(CTL, l) ? 3 : 0; };
  const bool ownLayout = running && gamePadMaps.count(running->name);
  for (auto& m : items) if ((m.label != "Use Settings buttons" || ownLayout) && (adv || !contains(ADVANCED_MENU, m.label)) && catOf(m.label) == cat) kept.push_back(m);
  return kept;
}
static int menuFixedRows() {
  static int rows = 0;
  if (!rows) for (int c = 0; c < (int)MENU_CATS.size(); c++) rows = std::max(rows, (int)menuItemsFor(c, true).size());
  return rows;
}
static void openMenu() {
  showMenuItems(stripNes(running->name), menuItemsFor(menuCat, showAdvanced));
  menuHasTabs = true; menuOnBack = false;
  mode = "menu";
  saveSram(false);
}
static void cycleMenuOption(int dir) {
  MenuItem& o = menuItems[menuIndex];
  if (!o.option()) return;
  int n = (int)o.choices().size();
  o.set((o.get() + dir + n) % n);
  auto after = o.after;  // a copy: it may rebuild the menu (Advanced options), freeing o
  if (after) after();
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

/* ---------------- Button remapping ---------------- */
static const double REMAP_WAIT_MS = 4000;
static int remapWho = 0, remapStep = 0, remapPick = 0, remapNew[NPAD];
static bool remapForGame = false;  // opened from the in-game menu: saves this game's own layout, returns to the menu
static double remapDeadline = 0;
static bool remapWaitRelease = true;
static void openRemap(int player, bool forGame) {
  remapForGame = forGame && running;
  remapWho = player; remapStep = 0; remapPick = 0; remapWaitRelease = true;
  for (int i = 0; i < NPAD; i++) remapNew[i] = -1;
  remapDeadline = nowMs() + REMAP_WAIT_MS;
  mode = "remap";
}
static bool padConnected(int player) { return padStates[player] && padIsConnected(padStates[player]); }
static void pairControllers() {
  HidLaControllerSupportArg arg; hidLaCreateControllerSupportArg(&arg);
  arg.hdr.player_count_min = 0; arg.hdr.player_count_max = 2;
  HidLaControllerSupportResultInfo info{};
  Result rc = hidLaShowControllerSupport(&info, &arg), rc2 = 0;
  const bool triedSystem = R_FAILED(rc);
  if (triedSystem) rc2 = hidLaShowControllerSupportForSystem(&info, &arg, false);
  record("controllers: support applet rc 0x%x, system variant rc 0x%x, players %d", rc, rc2, (int)info.player_count);
  char msg[160];
  if (R_SUCCEEDED(rc) || (triedSystem && R_SUCCEEDED(rc2))) snprintf(msg, sizeof msg, "Controllers updated");
  else snprintf(msg, sizeof msg, "The controller screen could not open (0x%x / 0x%x)", rc, rc2);
  showToast(msg, 6000);
}
static bool resetMapArmed = false;
static bool settingsByA = false;  // the row's action came from A (not left/right): for rows that do something

/* ---------------- Settings screen ---------------- */
struct SettingItem { string label; std::function<vector<string>()> choices; std::function<int()> get; std::function<void(int)> set; };
static vector<SettingItem> settingsItems() {
  return {
      {"ROM folder", [] { return vector<string>{romDir.rfind("sdmc:", 0) == 0 ? romDir.substr(5) : romDir}; }, [] { return 0; }, [](int) { openFolderPicker(); }},
      {"Debug logging", [] { return OFF_ON; }, [] { return debugLog ? 1 : 0; }, [](int v) {
        debugLog = v != 0;
        if (debugLog && !logFile) { openLog(); record("SW2NES %s: debug logging turned on", APP_VERSION); }
        else if (!debugLog && logFile) { fclose(logFile); logFile = nullptr; }
      }},
      {"Theme", [] { vector<string> v; for (const Theme& t : THEMES) v.push_back(t.name); return v; }, [] { return themeIndex; }, [](int v) { applyTheme(v); }},
      {"Game list view", [] { return vector<string>{"List", "Grid"}; }, [] { return gridView ? 1 : 0; }, [](int v) { gridView = v == 1; }},
      {"Game details panel", [] { return OFF_ON; }, [] { return detailsPanel ? 1 : 0; }, [](int v) { detailsPanel = v != 0; }},
      {"Game names", [] { return vector<string>{"Title", "File name"}; }, [] { return gameTitles ? 0 : 1; }, [](int v) { gameTitles = v == 0; orderRoms(); }},
      {"Download missing box art and descriptions", [] { return vector<string>{boxart::status().running && boxArtRow == 0 ? boxArtProgress() : string("Press {A}")}; }, [] { return 0; },
       [](int) { if (settingsByA && !boxart::status().running) startBoxArt(false); }},
      {"Download all box art and descriptions", [] { return vector<string>{boxart::status().running && boxArtRow == 1 ? boxArtProgress() : string("Press {A}")}; }, [] { return 0; },
       [](int) { if (settingsByA && !boxart::status().running) startBoxArt(true); }},
      {"On-screen Menu button", [] { return MENU_BUTTON_MODES; }, [] { return menuButtonMode; }, [](int v) { menuButtonMode = v; }},
      {"UI size", [] { return vector<string>{"Compact", "Normal"}; }, [] { return touchControls ? 1 : 0; }, [](int v) { touchControls = v != 0; ROW_H = touchControls ? 72 : 40; }},
      {"Pair controllers", [] { return vector<string>{"Press {A}"}; }, [] { return 0; }, [](int) { if (settingsByA) pairControllers(); }},
      {"Player 1 buttons", [] { return vector<string>{padMapIsDefault(0) ? "Standard - {A} to remap" : "Custom - {A} to remap"}; }, [] { return 0; }, [](int) { if (settingsByA) openRemap(0, false); }},
      {"Player 2 buttons", [] { return vector<string>{padMapIsDefault(1) ? "Standard - {A} to remap" : "Custom - {A} to remap"}; }, [] { return 0; }, [](int) { if (settingsByA) openRemap(1, false); }},
      {"Reset button mapping", [] { return vector<string>{resetMapArmed ? "Press {A} again to confirm" : "Press {A}"}; }, [] { return 0; },
       [](int) { if (!settingsByA) return; if (!resetMapArmed) { resetMapArmed = true; return; }
                 resetMapArmed = false; for (int pl = 0; pl < 2; pl++) resetPadMap(pl); saveSettings(); showToast("Both players back on the standard layout", 2500); }},
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
  };
}
static vector<SettingItem> SETTINGS_ITEMS;
static bool resetArmed = false;
static void rebuildSettings();
// Settings come in categories, cycled with L / R like the game list's tabs.
static const vector<string> SETTINGS_CATS = {"Emulation", "Picture", "Sound & controls", "App"};
static int settingsCat = 0;  // settingsIndex -1: the cursor is on the category tabs
static int settingsCatOf(const string& label) {
  static const vector<string> EMU = {"Core", "Run-ahead", "Rewind (hold ZL)", "Fast-forward", "Fast-forward speed", "Turbo speed", "Auto-load save state"};
  static const vector<string> PIC = {"Aspect", "Scale", "Overscan", "Signal", "Shader", "Brightness boost", "Curvature", "Corner radius", "Corner softness", "Vignette", "Vignette size", "Display", "Frame delay (60 Hz)", "Palette"};
  static const vector<string> SND = {"Volume", "Audio filter", "Audio buffer", "Pair controllers", "Player 1 buttons", "Player 2 buttons", "Reset button mapping"};
  return contains(EMU, label) ? 0 : contains(PIC, label) ? 1 : contains(SND, label) ? 2 : 3;  // the rest: App
}
static vector<SettingItem> settingsFor(int cat, bool adv) {
  vector<SettingItem> items;
  if (cat == 3) for (auto& it : settingsItems()) if (it.label == "Theme") items.push_back(it);  // App's first row
  if (cat == 3) items.push_back({"Advanced options", [] { return vector<string>{"Hidden", "Shown"}; }, [] { return showAdvanced ? 1 : 0; },
                                 [](int v) { showAdvanced = v != 0; rebuildSettings(); }});
  for (auto& it : settingsItems())
    if ((adv || !contains(ADVANCED_SETTINGS, it.label)) && settingsCatOf(it.label) == cat && it.label != "Theme") items.push_back(it);
  if (adv && cat == 3)
    items.push_back({"Reset all settings", [] { return vector<string>{"Press {A}", "Press {A} again to confirm"}; }, [] { return resetArmed ? 1 : 0; },
                     [](int v) { if (resetArmed) { resetArmed = false; resetAllSettings(); gfx::setHighRefresh(displayMode == 1); rebuildSettings(); settingsIndex = 0; settingsTop = 0; showToast("All settings reset to defaults"); } else resetArmed = true; }});
  if (cat == 3) items.push_back({"Version", [] { return vector<string>{"v" APP_VERSION}; }, [] { return 0; }, nullptr});  // shown, not changeable
  return items;
}
static void rebuildSettings();
static void rebuildSettings() {
  SETTINGS_ITEMS = settingsFor(settingsCat, showAdvanced);
  if (settingsIndex > (int)SETTINGS_ITEMS.size()) settingsIndex = (int)SETTINGS_ITEMS.size();
}
// The fixed number of rows Settings is sized for: its largest category with every advanced row
// shown, plus Back, so switching category or showing / hiding advanced never resizes it.
static int settingsFixedRows() {
  static int rows = 0;
  if (!rows) for (int c = 0; c < (int)SETTINGS_CATS.size(); c++) rows = std::max(rows, (int)settingsFor(c, true).size() + 1);
  return rows;
}

/* ---------------- Drawing helpers ---------------- */
static const float LH = 1.15f;  // CSS "normal" line height, approximately
// Text vertically centred in a box.
// Button names in text: "{A}" "{B}" "{X}" "{Y}" are drawn as the letter, or -- while a single Joy-Con
// is connected, whose buttons have no letters that match -- as the position glyph Nintendo and Steam
// use: four dots in a diamond, the one to press filled (A right, B bottom, X top, Y left).
static bool singleJoyConConnected = false;  // updated each frame (main loop)
static int glyphToken(const string& s, size_t i) {  // 0-3 for {A} {B} {X} {Y} at i, else -1
  if (i + 2 >= s.size() || s[i] != '{' || s[i + 2] != '}') return -1;
  const char* k = strchr("ABXY", s[i + 1]);
  return k && s[i + 1] ? (int)(k - "ABXY") : -1;
}
static float glyphWidth(float size) { return size * 1.15f; }
static void drawFaceGlyph(int which, float x, float y, float size, Color c) {  // in a size x size box
  const float r = size * 0.17f, d = size * 0.31f, cx = x + glyphWidth(size) / 2, cy = y + size * 0.62f;
  static const float DX[4] = {1, 0, 0, -1}, DY[4] = {0, 1, -1, 0};  // A right, B bottom, X top, Y left
  for (int k = 0; k < 4; k++) {
    const float px = cx + DX[k] * d - r, py = cy + DY[k] * d - r;
    if (k == which) gfx::rect(px, py, r * 2, r * 2, c, r);
    else gfx::rect(px, py, r * 2, r * 2, Color{0, 0, 0, 0}, r, std::max(1.0f, size * 0.07f), c);
  }
}
static float glyphTextWidth(const string& s, float size, gfx::Font f = gfx::REGULAR) {
  if (s.find('{') == string::npos) return gfx::textWidth(s, size, f);
  float w = 0; string run;
  for (size_t i = 0; i < s.size(); i++) {
    const int g = glyphToken(s, i);
    if (g < 0) { run += s[i]; continue; }
    w += gfx::textWidth(run, size, f); run.clear();
    w += singleJoyConConnected ? glyphWidth(size) : gfx::textWidth(string(1, "ABXY"[g]), size, f);
    i += 2;
  }
  return w + gfx::textWidth(run, size, f);
}
static float glyphText(const string& s, float x, float y, float size, Color c, gfx::Font f = gfx::REGULAR) {
  if (s.find('{') == string::npos) return gfx::text(s, x, y, size, c, f);
  float x0 = x; string run;
  auto flush = [&]() { if (!run.empty()) { gfx::text(run, x, y, size, c, f); x += gfx::textWidth(run, size, f); run.clear(); } };
  for (size_t i = 0; i < s.size(); i++) {
    const int g = glyphToken(s, i);
    if (g < 0) { run += s[i]; continue; }
    flush();
    if (singleJoyConConnected) { drawFaceGlyph(g, x, y, size, c); x += glyphWidth(size); }
    else { const string l(1, "ABXY"[g]); gfx::text(l, x, y, size, c, f); x += gfx::textWidth(l, size, f); }
    i += 2;
  }
  flush();
  return x - x0;
}
static float textMid(const string& s, float x, float y, float h, float size, Color c, gfx::Font f = gfx::REGULAR) {
  return glyphText(s, x, y + (h - size * 1.25f) / 2, size, c, f);
}
// A button's letter or symbol (A, -, +, LS, x, F...): its ink centred on (mx, my), not its font box (a capital
// has empty room on its right and none below; a minus sign sits at a letter's middle height, not the box's).
static void inkMid(const string& s, float mx, float my, float size, Color c, gfx::Font f = gfx::REGULAR) {
  gfx::textCentered(s, mx, my, size, c, f);  // (in whole screen pixels)
}
// "< value >" selector, right-aligned at xr; returns its width.
static float valueBox(const string& t, float xr, float y, float h, float size, bool active) {
  Color c = col(active ? C_TEXT : C_DIM);
  float wl = gfx::textWidth("<", size, gfx::BOLD), wt = glyphTextWidth(t, size), wr = gfx::textWidth(">", size, gfx::BOLD);
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
static void scrollbar(float x, float y, float h, int total, int vis, int topIndex, int* topRef = nullptr) {
  if (total <= vis) return;
  const float bw = barW();
  gfx::rect(x, y, bw, h, col(C_LINE), bw / 2);
  float th = std::max(24.0f, roundf(h * vis / total));
  gfx::rect(x, y + roundf((h - th) * topIndex / (total - vis)), bw, th, col(C_DIM), bw / 2);
  if (topRef) addHit(x - 10, y, bw + 20, h, nullptr, [topRef, total, vis](int rows) {  // drag the thumb: the page follows
    *topRef = std::max(0, std::min(total - vis, *topRef - rows * std::max(1, total / std::max(1, vis))));
    touchScrolled = true;
  });
}

/* ---------------- Screens ---------------- */
static const float BTN_TEXT = 20, TAB_TEXT = 19;  // button and tab labels
static const float LIST_HEAD = 26;  // column headings above the game list
// A row of tabs with small L / R shoulder-button caps either side: L and R cycle them.
static float tabWidth(const string& label) { return roundf(gfx::textWidth(label, TAB_TEXT) + 44); }
static float equalTabsWidth(const vector<string>& labels) {  // equal tabs, each as wide as the widest needs
  float w = 0; for (auto& l : labels) w = std::max(w, tabWidth(l));
  return w * labels.size() + 8 * (labels.size() - 1);
}
static float tabsWidth(const vector<string>& labels) { float w = 0; for (auto& l : labels) w += tabWidth(l) + 8; return w - 8; }
// A shoulder button's badge (the L / R tab markers): filled as the footer's button icons, fully round on its
// outer side (the button's top corner) and nearly square on the side facing the tabs; L rounded on the left, R on
// the right. Its letters centred on their ink, a unit toward the squared side, where the shape's weight is.
static const float SHOULDER_W = 28, SHOULDER_GAP = 10;
static void drawShoulder(const char* label, float bx, float cy, bool left) {
  bx = roundf(bx);
  const float h = 22, w = SHOULDER_W, by = roundf(cy + (tabH() - h) / 2);
  gfx::rect(bx, by, w, h, keyFillOf(-1), h / 2);                                // round all round,
  gfx::rect(left ? bx + w / 2 : bx, by, w / 2, h, keyFillOf(-1), 3);            // then squared on the inner side
  inkMid(label, bx + w / 2 + (left ? 1.0f : -1.0f), by + h / 2, label[1] ? 12 : 14, keyInkOf(-1), gfx::BOLD);
}
static void drawTabBar(float cx, float cy, const vector<string>& labels, int sel, int focusTab, std::function<void(int)> onTab = nullptr, bool caps = true, float fillW = 0) {  // focusTab: the tab the cursor is on, -1 none; fillW: equal tabs across this width
  cx = roundf(cx); cy = roundf(cy);  // whole pixels: a 1 px border at a fractional x can vanish
  auto cap = [&](const char* label, float bx, bool left) { drawShoulder(label, bx, cy, left); };
  if (caps) cap(singleJoyConConnected ? "SL" : "L", cx, true);
  float tx = caps ? cx + 28 + 10 : cx;
  for (size_t i = 0; i < labels.size(); i++) {
    bool on = (int)i == sel, focus = (int)i == focusTab;
    float w = fillW > 0 ? roundf((fillW - 8 * (labels.size() - 1)) / labels.size()) : tabWidth(labels[i]);
    if (onTab && touchHolding(tx, cy - 6, w + 8, tabH() + 12)) focus = true;  // lit while the finger is on it
    gfx::rect(tx, cy, w, tabH(), focus ? col(C_SEL) : on ? col(C_LINE) : Color{0, 0, 0, 0}, BTN_RADIUS, 1, col(focus ? C_ACCENT : C_LINE));
    textMid(labels[i], tx + (w - gfx::textWidth(labels[i], TAB_TEXT)) / 2, cy, tabH(), TAB_TEXT, col(on ? C_TEXT : C_DIM));
    if (onTab) addHit(tx, cy - 6, w + 8, tabH() + 12, [onTab, i] { onTab((int)i); });
    tx += w + 8;
  }
  if (caps && !singleJoyConConnected) cap("R", tx + 2, false);  // (a single Joy-Con's SR pages instead)
  if (onTab && caps) {  // the L / R caps: previous / next (a single Joy-Con's SL: next, round)
    const int n = (int)labels.size(), back = singleJoyConConnected ? 1 : n - 1;
    addHit(cx - 6, cy - 6, 40, tabH() + 12, [onTab, sel, n, back] { onTab((sel + back) % n); });
    if (!singleJoyConConnected) addHit(tx - 4, cy - 6, 40, tabH() + 12, [onTab, sel, n] { onTab((sel + 1) % n); });
  }
}
// A tab row's Back button (Settings, the in-game menu): after a divider at the row's right end.
static const float BACK_W = 132, BACK_GAP = 12;  // (BACK_GAP: from the last tab; no divider)
// The Back button at a tab row's right end (before its R badge): the B button's icon, as in the footer, and Back.
static bool drawTabBack(float rowX, float rowW, float cy, bool focus, std::function<void()> tap) {
  const float bx = rowX + rowW - BACK_W;
  const bool held = touchHolding(bx, cy - 6, BACK_W, tabH() + 12), lit = focus || held;
  gfx::rect(bx, cy, BACK_W, tabH(), held ? col(C_ACCENT, 0.35f) : focus ? col(C_SEL) : Color{0, 0, 0, 0}, BTN_RADIUS, 1, col(lit ? C_ACCENT : C_LINE));
  const float kw = 26, lw = gfx::textWidth("Back", TAB_TEXT), kx = roundf(bx + (BACK_W - (kw + 8 + lw)) / 2), ky = roundf(cy + (tabH() - kw) / 2);
  const Color fill = keyFillOf(1), ink = keyInkOf(1);  // (the theme's B colours, as in the footer)
  if (singleJoyConConnected) { gfx::rect(kx, ky, kw, kw, fill, kw / 2); drawFaceGlyph(1, kx + (kw - glyphWidth(17)) / 2, ky + (kw - 17 * 1.25f) / 2, 17, ink); }
  else gfx::badge("B", kx, ky, kw, kw, kw / 2, fill, 15, ink, gfx::BOLD);
  textMid("Back", kx + kw + 8, cy, tabH(), TAB_TEXT, col(lit ? C_TEXT : C_DIM));
  addHit(bx, cy - 6, BACK_W, tabH() + 12, tap);
  return held;
}
// L / R through a tab row with a Back button: the tabs, then Back, and round again (Back is only highlighted).
static void settingsStep(int d) {
  const int n = (int)SETTINGS_CATS.size(), pos = settingsOnBack ? n : settingsCat, to = (pos + d + n + 1) % (n + 1);
  if (to == n) { settingsOnBack = true; settingsIndex = -1; return; }
  settingsOnBack = false; settingsCat = to; resetArmed = false; rebuildSettings(); if (settingsIndex >= 0) settingsIndex = 0; settingsTop = 0;
}
static void openMenu();
static void menuStep(int d) {
  const int n = (int)MENU_CATS.size(), pos = menuOnBack ? n : menuCat, to = (pos + d + n + 1) % (n + 1);
  if (to == n) { menuOnBack = true; menuIndex = -1; return; }
  menuOnBack = false; menuCat = to; const bool onTabs = menuIndex < 0; openMenu(); if (onTabs) menuIndex = -1;
}
// A tab row with a Back button: [L] tabs [Back] [R]. The L and R badges also step (tapped).
static float tabRowTabsW(float rowW) { return rowW - (SHOULDER_W + SHOULDER_GAP) - BACK_GAP - BACK_W - (SHOULDER_GAP + SHOULDER_W); }
static void drawRowShoulders(float rowX, float rowW, float cy, std::function<void(int)> step) {
  drawShoulder(singleJoyConConnected ? "SL" : "L", rowX, cy, true);
  drawShoulder(singleJoyConConnected ? "SR" : "R", rowX + rowW - SHOULDER_W, cy, false);
  addHit(rowX - 6, cy - 6, SHOULDER_W + 12, tabH() + 12, [step] { step(-1); });
  addHit(rowX + rowW - SHOULDER_W - 6, cy - 6, SHOULDER_W + 12, tabH() + 12, [step] { step(1); });
}
// Game rows that fit: the screen less the window's padding, the header row, the column headings,
// the footer and the list's own padding.
static const float SCREEN_MARGIN = 24;  // the game list's window fills the screen but for this margin
static int maxRows() { return std::max(3, (int)((gfx::UI_H - 2 * SCREEN_MARGIN - 36 - tabRowH() - 10 - LIST_HEAD - 12 - (btnH() + 14) - 12) / ROW_H)); }

// The game list: title; tabs | search field, sort (what you see); the list; a footer of button
// legends (what you do), each also a touch button. Every control keeps one size and place.
static const float BUB = 18;  // header controls' text
static const float SEARCH_MIN = 240;  // the search field: at least this, otherwise the room left in the header row
static float sortWidth() {
  float w = 0;
  for (auto& l : ROM_SORT_LABELS) w = std::max(w, gfx::textWidth("Sort: " + l, BUB));
  return roundf(w + 40);
}
struct Legend { const char* key; string label; std::function<void()> tap; bool lit; };
// A theme with very thick borders (Retro Red): extra room, in UI units, inside each footer button (borders are
// drawn inward) and between them, so the borders do not crowd the labels or each other. 0 for the others.
static float thickPad() { return gfx::borderScale > 2 ? roundf(2 * (gfx::borderScale - 1)) : 0; }
static float legendWidth(const vector<string>& labels) {  // the widest of a legend's labels, with its badge
  float w = 0; for (auto& l : labels) w = std::max(w, gfx::textWidth(l, BTN_TEXT));
  return roundf(30 + 10 + w + 28 + 2 * thickPad());
}
// The footer's buttons, left to right, by their action (pressFooter): Play 0, Favourite 1, Search 2, Refresh 4 (Y),
// Settings 3 (X), Sort 7 (the next sort order, as the header's), View 9 (the four views in turn), Exit 5.
static const int FOOT_IDS[8] = {0, 1, 2, 4, 3, 7, 9, 5};  // (Y Refresh, X Settings)
static const int FOOT_N = 8, FOOT_EXIT = 7;  // (Exit's place)
static vector<float> footerWidths(float room = 1e9f) {  // every footer button one width: the longest label's, or less to fit the row
  const float natural = legendWidth({"Play", "Favourite", "Search", "Settings", "Refresh", "Sort", "View", "Exit", "Confirm exit"});
  return vector<float>(FOOT_N, std::min(natural, floorf((room - (FOOT_N + 1) * (6 + thickPad())) / FOOT_N)));
}
// The four views in turn (View, the right stick press): list or grid, each with or without the details panel.
static void cycleView() {
  static const struct { bool grid, panel; const char* name; } VIEWS[4] = {
      {false, true, "List & details"}, {false, false, "List"}, {true, true, "Grid & details"}, {true, false, "Grid"}};
  int at = 0;
  for (int i = 0; i < 4; i++) if (VIEWS[i].grid == gridView && VIEWS[i].panel == detailsPanel) at = i;
  const auto& v = VIEWS[(at + 1) % 4];
  gridView = v.grid; detailsPanel = v.panel; touchScrolled = false;
  saveSettings(); showToast(v.name);
}
static float bioScroll = 0, bioScrollMax = 0, artYaw = 0, artPitch = 0;  // the panel: description scroll, the box's turn (by touch)
static double artSince = 0;  // when this game was highlighted (the box's swing starts from the centre)
static double artTouchedAt = 0;  // the last time a finger turned the box (let go: it eases back)
static string artPanelGame;
static void drawArtPanel(float x, float y, float w, float h, const Rom* rom) {
  gfx::rect(x, y, w, h, col(C_BG), 10, 1, col(C_LINE));
  if (!rom) return;
  if (artPanelGame != rom->name) { artPanelGame = rom->name; bioScroll = 0; artYaw = artPitch = 0; artSince = nowMs(); }  // a new game: from the top, unturned
  ArtTex& a = artFor(rom->name);
  // the box: high in the panel; drag on it to turn it any way
  const float boxH = std::min(h * 0.54f, w * 1.0f), pad = 16;
  float boxCY = y + boxH * 0.5f, boxBottom = y + 60;  // (set from the box's own size below)
  // where the title and details go: under the box, or beside a tall box (its description then full width below both)
  float detX = x + pad, detW = w - 2 * pad, detTop = -1, boxCX = x + w / 2, boxHitW = w;
  if (a.img) {
    // one box shape for every game (its art stretched the little it differs), so nothing moves between games:
    // a landscape box for art wider than tall (the SNES's own boxes), a portrait one otherwise
    const float artAspect = (float)gfx::imageWidth(a.img) / gfx::imageHeight(a.img);
    const float aspect = artAspect >= 1 ? 1.37f : 0.72f;
    float fw = w * 0.7f, fh = fw / aspect;
    if (fh > boxH * 0.82f) { fh = boxH * 0.82f; fw = fh * aspect; }
    // close to the panel's top (the slight tilt and perspective need a little room above and below)
    boxCY = y + 10 + fh * 0.56f + fh * 0.10f;  // (shifted down by a tenth of the box)
    boxBottom = boxCY + fh * 0.56f;
    if (aspect < 1) {  // a tall box (NES, PS2): in the top left, the details beside it
      fw = w * 0.42f; fh = fw / aspect;
      if (fh > h * 0.5f) { fh = h * 0.5f; fw = fh * aspect; }
      boxCX = x + pad + fw * 0.62f;  // (room for its swing)
      boxCY = y + 12 + fh * 0.56f;
      boxBottom = boxCY + fh * 0.56f;
      boxHitW = fw * 1.25f + pad;
      detX = x + pad + fw * 1.25f + 10; detW = x + w - pad - detX; detTop = y + 18;
    }
    {  // let go: ease back to the normal swing (the shortest way round)
      static double last = 0; const double now = nowMs(), dt = last ? std::min(0.1, (now - last) / 1000.0) : 0; last = now;
      if (!(touchIsDown && now - artTouchedAt < 150)) {
        artYaw = fmodf(artYaw, 360); if (artYaw > 180) artYaw -= 360; if (artYaw < -180) artYaw += 360;
        artPitch = fmodf(artPitch, 360); if (artPitch > 180) artPitch -= 360; if (artPitch < -180) artPitch += 360;
        const float k = (float)exp(-dt * 8);  // about half a second
        artYaw *= k; artPitch *= k;
        if (fabsf(artYaw) < 0.05f) artYaw = 0;
        if (fabsf(artPitch) < 0.05f) artPitch = 0;
      }
    }
    // an even, slow swing left and right, from facing straight ahead when the game is highlighted
    const float swing = 32 * sinf((float)((nowMs() - artSince) / 1000.0) * 0.55f);
    const float yaw = swing + artYaw, pitch = 6 + artPitch;
    // for now (until the ScreenScraper sides): the front's art on a black box
    gfx::clip((int)x + 2, (int)y + 2, (int)w - 4, (int)h - 4);  // never past the panel's border, however it is turned
    // the box's depth: from its side image's proportions (ScreenScraper), else a typical box's
    const float depth = a.sideRatio > 0 ? std::max(fw, fh) * std::min(0.35f, std::max(0.03f, a.sideRatio)) : fh * boxDepthRatio(boxart::region(rom->name));
    gfx::box3D(a.img, a.back, a.spineImg, boxCX, boxCY, fw, fh, depth, yaw, pitch, a.sideColor);
    gfx::noClip();
    addMoveHit(x, y, boxHitW, std::max(boxH, boxBottom - y), [](float dx, float dy) { artYaw += dx * 0.6f; artPitch -= dy * 0.6f; artTouchedAt = nowMs(); });
  } else {
    const string msg = boxart::status().running ? boxArtProgress() : a.hasInfo ? "No box art found" : "";
    if (!a.hasInfo && !boxart::status().running) {  // nothing at all yet: say where to get it
      float my = y + 20;
      for (auto& l : wrap("No box art or description. Go to Settings to download missing box art and descriptions.", w - 2 * 16, 17)) { gfx::text(l, x + 16, my, 17, col(C_DIM)); my += 24; }
      return;
    }
    textMid(msg, x + (w - gfx::textWidth(msg, 16)) / 2, y + 20, 22, 16, col(C_DIM));
  }
  // the details: title, developer / year / genre, the description (it scrolls: touch, or the right stick)
  const bool beside = detTop >= 0;  // a tall box: the details beside it (then a screenshot), the description below
  const bool flow = false; const float off = 0;
  float ty = beside ? detTop : boxBottom + 10;  // the details: beside a tall box, otherwise straight under the box
  const float tw = w - 2 * pad, bottom = y + h - pad;
  const string title = a.hasInfo && !a.title.empty() ? a.title : stripNes(rom->name);
  // the year and the genre: one line ("1995  ·  RPG"), or, beside a tall box, each on its own line
  vector<string> line2s;
  if (!a.line2.empty()) {
    if (!beside) line2s.push_back(a.line2);
    else { const string sep = "  \u00b7  "; size_t p = 0, q; while ((q = a.line2.find(sep, p)) != string::npos) { line2s.push_back(a.line2.substr(p, q - p)); p = q + sep.size(); } line2s.push_back(a.line2.substr(p)); }
  }
  if (beside) {  // beside a tall box: the box's back at the top, the details underneath ending level with the box's bottom
    const float boxTop = boxCY - (boxBottom - boxCY);
    const float faceTop = boxTop + (boxBottom - boxTop) * 0.05f, faceBottom = boxBottom - (boxBottom - boxTop) * 0.05f;  // its front face (not the tilt)
    float detH = wrap(title, detW, 22, gfx::BOLD).size() * 28.0f;
    if (!a.line.empty()) detH += wrap(a.line, detW, 17).size() * 24.0f;
    for (const string& l2 : line2s) detH += wrap(l2, detW, 17).size() * 24.0f;
    // the back and the details under it, as one block centred on the box
    float bw = 0, bh = 0;
    if (a.back) {  // the column's full width, smaller only if the block would be taller than the box
      const float ba = (float)gfx::imageWidth(a.back) / gfx::imageHeight(a.back);
      const float room = faceBottom - faceTop - detH - 10;
      bw = detW; bh = bw / ba;
      if (bh > room) { bh = std::max(0.0f, room); bw = bh * ba; }
      if (bh <= 30) bw = bh = 0;
    }
    const float blockH = (bh > 0 ? bh + 10 : 0) + detH;
    float top = std::max(y + 12, boxCY - blockH / 2);
    if (bh > 0) { gfx::imageDraw(a.back, detX, top, bw, bh); top += bh + 10; }
    ty = top;
  }
  for (auto& l : wrap(title, detW, 22, gfx::BOLD)) { if (ty + 28 > bottom) break; gfx::text(l, detX, ty, 22, col(C_TEXT), gfx::BOLD); ty += 28; }
  if (!a.line.empty()) for (auto& l : wrap(a.line, detW, 17)) { if (ty + 24 > bottom) break; gfx::text(l, detX, ty + 2, 17, col(C_TEXT)); ty += 24; }
  for (const string& l2 : line2s) for (auto& l : wrap(l2, detW, 17)) { if (ty + 24 > bottom) break; gfx::text(l, detX, ty + 2, 17, col(C_TEXT)); ty += 24; }
  if (beside) {  // under the details, beside the box: a gameplay screenshot, as large as fits; then the description below both
    ty = std::max(ty, boxBottom + 4);
  }
  bioScrollMax = 0;
  if (!a.hasInfo) {
    for (auto& l : wrap("No description. Go to Settings to download missing box art and descriptions.", tw, 17)) { if (ty + 24 > bottom) break; gfx::text(l, x + pad, ty + 4, 17, col(C_DIM)); ty += 24; }
    return;
  }
  if (a.bio.empty()) { if (ty + 24 <= bottom) gfx::text("No description found", x + pad, ty + 4, 17, col(C_DIM)); return; }
  const float BIO = 18, LINE = 26;
  // the description's lines: beside a tall box (its column) while level with it, then full width
  struct BioLine { string text; bool last; float lx, lw, cy; };  // cy: where it sits, unscrolled
  vector<BioLine> lines;
  const float startY = ty + off + 6;  // (unscrolled)
  float cyAt = startY;
  auto widthAt = [&](float cy, float& lx) { if (flow && cy < boxBottom + 4) { lx = detX; return detW; } lx = x + pad; return tw - 10; };
  {
    size_t p = 0;
    while (p <= a.bio.size()) {
      size_t e = a.bio.find('\n', p); if (e == string::npos) e = a.bio.size();
      const string para = a.bio.substr(p, e - p);
      vector<string> ws; { size_t q = 0; while (q < para.size()) { size_t sp = para.find(' ', q); if (sp == string::npos) sp = para.size(); if (sp > q) ws.push_back(para.substr(q, sp - q)); q = sp + 1; } }
      size_t k = 0;
      if (ws.empty()) { cyAt += LINE; }
      while (k < ws.size()) {
        float lx; const float lw = widthAt(cyAt, lx);
        string line = ws[k++];
        while (k < ws.size() && gfx::textWidth(line + " " + ws[k], BIO) <= lw) line += " " + ws[k++];
        lines.push_back(BioLine{line, k >= ws.size(), lx, lw, cyAt});
        cyAt += LINE;
      }
      p = e + 1;
    }
  }
  const float areaTop = flow ? y + 4 : ty + 6, areaBottom = y + h - pad;
  const float areaH = areaBottom - areaTop;
  if (areaH < LINE) return;
  // scrolling: the description's end reaching the panel's bottom
  bioScrollMax = flow ? std::max(0.0f, cyAt - areaBottom) : std::max(0.0f, cyAt - startY - areaH);
  bioScroll = std::max(0.0f, std::min(bioScrollMax, bioScroll));
  const float shift = flow ? bioScroll : (startY - areaTop) + bioScroll;  // content y -> screen y
  gfx::clip((int)x, (int)areaTop, (int)w, (int)areaH);
  for (const BioLine& L : lines) {
    const float ly = L.cy - shift;
    if (ly + LINE < areaTop || ly > areaBottom) continue;
    vector<string> ws; { size_t q = 0; while (q < L.text.size()) { size_t sp = L.text.find(' ', q); if (sp == string::npos) sp = L.text.size(); if (sp > q) ws.push_back(L.text.substr(q, sp - q)); q = sp + 1; } }
    float wordsW = 0; for (auto& wd : ws) wordsW += gfx::textWidth(wd, BIO);
    const float gap = ws.size() > 1 ? (L.lw - wordsW) / (ws.size() - 1) : 0;
    if (L.last || ws.size() < 2 || gap > BIO * 1.6f) { gfx::text(L.text, L.lx, ly, BIO, col(C_TEXT)); continue; }  // a paragraph's end, or too stretched: as it is
    float wx = L.lx;
    for (auto& wd : ws) { gfx::text(wd, wx, ly, BIO, col(C_TEXT)); wx += gfx::textWidth(wd, BIO) + gap; }
  }
  gfx::noClip();
  if (bioScrollMax > 0) {  // how far through: a thin bar
    const float contentH = bioScrollMax + areaH, bh = std::max(24.0f, areaH * areaH / contentH), by = areaTop + (areaH - bh) * (bioScroll / bioScrollMax);
    gfx::rect(x + w - 8, areaTop, 3, areaH, col(C_LINE), 1.5f);
    gfx::rect(x + w - 8, by, 3, bh, col(C_DIM), 1.5f);
  }
  if (!flow) addMoveHit(x, areaTop, w, areaH, [](float, float dy) { bioScroll -= dy; });  // touch: drag the text
}
static void drawBrowser() {
  const float padX = 20, padY = 18, gap = 12, divGap = 36;  // (divGap: the divider in its middle, 18 from the R badge and from the search box)
  // the tabs with their L and R badges: [L] 10 tabs 10 [R] (drawTabBar's spacing), so the divider after them is centred
  const float tabsW = (28 + 10) + tabsWidth(LIST_VIEWS) + (8 + 2 + 28), sortW = sortWidth();
  const vector<float> fw = footerWidths(gfx::UI_W - 2 * SCREEN_MARGIN - 2 * 20 - 16);  // (the footer row: the window less its padding and the scroll bar's gutter)
  float footW = 0; for (float w : fw) footW += w + 8;
  const float titleW = roundf(10 + 12 + gfx::textWidth("SW2NES", 26, gfx::BOLD) + 24);  // accent bar, title, space before the tabs
  const float headW = titleW + tabsW + divGap + SEARCH_MIN + gap + sortW;
  const float winW = gfx::UI_W - 2 * SCREEN_MARGIN, innerW = winW - 2 * padX;  // the whole screen, but for the margin
  (void)headW; (void)footW;  // (+16: the scroll bar's gutter)
  int VIS = std::max(1, std::min((int)roms.size(), maxRows()));
  if (!touchScrolled) {
    if (selected < top) top = selected;
    if (selected >= top + VIS) top = selected - VIS + 1;
  }
  if (top > std::max(0, (int)roms.size() - VIS)) top = std::max(0, (int)roms.size() - VIS);
  string empty;
  if (roms.empty() && !searchQuery.empty()) {
    empty = string(listView == 1 ? "No favourites" : listView == 2 ? "No recently played games" : "No games") + " match \"" + searchQuery + "\".\n\n" +
            "Clear the search with the \u00d7 in the search field, or press + to search for something else.";
  } else if (roms.empty()) {
    if (listView == 1) empty = "No favourites yet.\n\nHighlight a game in All or Recent and press - (or use the Favourite button).";
    else if (listView == 2) empty = "No recently played games yet.";
    else empty = "No games found in\n\n    " + romDir + "\n\nCopy .nes (or zipped) games there, or into folders inside it, for example over FTP, then choose Refresh. The folder can be changed in Settings.";
  }
  float listInnerW = innerW - 16;  // the scrollbar's gutter always kept: the list never changes width
  // The games on the left, the highlighted game's box art (on a 3D box) on the right.
  const float ART_W = detailsPanel ? roundf(listInnerW * 0.36f) : 0, gridW = detailsPanel ? listInnerW - ART_W - 16 : listInnerW;
  vector<string> emptyLines = empty.empty() ? vector<string>() : wrap(empty, gridW - 32, 17);
  float head = 0;  // (no line above the list: its column headings are inside it, at its top)
  const float footH = btnH() + 14;
  // the list takes all the height left (the screen, but for the margin)
  float listH = gfx::UI_H - 2 * SCREEN_MARGIN - (padY * 2 + tabRowH() + 10 + head + 12 + footH);
  float winH = padY * 2 + tabRowH() + 10 + head + listH + 12 + footH;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  // One row: the title, tabs | search field, sort (the game count is in the list's heading, the
  // version in Settings -> App), so the list gets the room.
  gfx::rect(cx, cy + (tabH() - 26) / 2, 10, 26, col(C_ACCENT), 2);
  { const bool it = gfx::italic; gfx::italic = false; textMid("SW2NES", cx + 22, cy, tabH(), 26, col(C_TEXT), gfx::BOLD); gfx::italic = it; }  // (the logo: upright)
  // Tabs | search field, sort
  drawTabBar(cx + titleW, cy, LIST_VIEWS, listView, focusArea == "tabs" ? (headIndex < 0 ? listView : headIndex < (int)LIST_VIEWS.size() ? headIndex : -1) : -1,
             [](int i) { if (i != listView) { listView = i; roms.clear(); orderRoms(); } focusArea = "tabs"; headIndex = -1; }, true);  // L and R (SL / SR) cycle them: their badges show it
  {
    gfx::rect(roundf(cx + titleW + tabsW + divGap / 2) - 1, cy + 8, 2, tabH() - 16, col(C_LINE), 1);  // divider
    // the search field takes the room from the divider to the sort control (right-aligned with the list's edge)
    const float searchX = cx + titleW + tabsW + divGap, searchW = cx + listInnerW - sortW - gap - searchX;
    float hx = searchX;
    const bool searching = !searchQuery.empty();
    {
      const bool focus = focusArea == "tabs" && headIndex == (int)LIST_VIEWS.size(), lit = (footFlash == 6 && nowMs() < footFlashUntil) || touchHolding(hx, cy - 4, searchW - (searching ? 44 : 0), tabH() + 8);
      gfx::rect(hx, cy, searchW, tabH(), lit ? col(C_ACCENT, 0.35f) : focus ? col(C_SEL) : col(C_BG), BTN_RADIUS, focus || lit ? 2 : 1, col(searching || focus || lit ? C_ACCENT : C_LINE));
    }
    const float clearW = searching ? 52 : 0;  // the x: a 30 circle, 14 in from the field's right edge (clear of its rounded corner)
    const string shown = searching ? searchQuery : "Search games";
    textMid(gfx::ellipsize(shown, searchW - 22 - clearW - 16, BUB), hx + 18, cy, tabH(), BUB, col(searching ? C_TEXT : C_DIM));
    addHit(hx, cy - 4, searchW - clearW, tabH() + 8, [] { pressFooter(6); });
    if (searching) {  // clear
      const float bx = hx + searchW - clearW;
      const bool cf = focusArea == "tabs" && headIndex == (int)LIST_VIEWS.size() + 1,
                 cl = (footFlash == 8 && nowMs() < footFlashUntil) || touchHolding(bx - 4, cy - 4, clearW + 8, tabH() + 8);
      const float d = 30, ox = hx + searchW - 14 - d, oy = roundf(cy + (tabH() - d) / 2);
      gfx::badge("\u00d7", ox, oy, d, d, d / 2, cl ? col(C_ACCENT) : cf ? col(C_ACCENT, 0.6f) : col(C_LINE), 21, col(C_TEXT), gfx::REGULAR, cf ? 2 : 0, col(C_TEXT));
      addHit(bx - 4, cy - 4, clearW + 8, tabH() + 8, [] { pressFooter(8); });
    }
    hx += searchW + gap;
    const string sortT = "Sort: " + ROM_SORT_LABELS[romSortByView[listView]];
    {
      const bool focus = focusArea == "tabs" && headIndex == (int)LIST_VIEWS.size() + 2, lit = (footFlash == 7 && nowMs() < footFlashUntil) || touchHolding(hx, cy - 4, sortW, tabH() + 8);
      gfx::rect(hx, cy, sortW, tabH(), lit ? col(C_ACCENT, 0.35f) : focus ? col(C_SEL) : Color{0, 0, 0, 0}, BTN_RADIUS, focus || lit ? 2 : 1, col(focus || lit ? C_ACCENT : C_LINE));
    }
    textMid(sortT, hx + (sortW - gfx::textWidth(sortT, BUB)) / 2, cy, tabH(), BUB, col(C_DIM));
    addHit(hx, cy - 4, sortW, tabH() + 8, [] { pressFooter(7); });
  }
  cy += tabRowH() + 10;
  // Columns from the right: Time Played, Last Played, Favourite.
  const float metadataW = 128;
  const float inner = barW() + 8;  // the scroll bar, inside the list on its right
  const float timeC = cx + gridW - inner - 18 - metadataW / 2;
  const float playedC = timeC - metadataW, favC = playedC - metadataW;
  cy += head;
  gfx::rect(cx, cy, gridW, listH, col(C_BG), 10, 1, col(C_LINE));
  // the list's column headings, inside it at its top (the grid has no columns)
  const float colHeadH = gridView || roms.empty() ? 0 : LIST_HEAD;
  if (colHeadH > 0) {
    const float hy = cy + 8;
    gfx::text("Title", cx + 22, hy, headText(), col(C_DIM), gfx::BOLD);  // (over the games' names)
    gfx::text("Time Played", timeC - gfx::textWidth("Time Played", headText(), gfx::BOLD) / 2, hy, headText(), col(C_DIM), gfx::BOLD);
    gfx::text("Last Played", playedC - gfx::textWidth("Last Played", headText(), gfx::BOLD) / 2, hy, headText(), col(C_DIM), gfx::BOLD);
    gfx::text("Favourite", favC - gfx::textWidth("Favourite", headText(), gfx::BOLD) / 2, hy, headText(), col(C_DIM), gfx::BOLD);
  }
  if (roms.empty()) {
    float ly = cy + 16;
    for (auto& l : emptyLines) { glyphText(l, cx + 16, ly, 17, col(C_TEXT)); ly += 17 * 1.45f; }
  } else if (gridView) {  // the grid: a cover and a title for each game
    const float gap = 14, areaX = cx + 12 + thickPad(), areaW = gridW - 24 - 2 * thickPad() - inner, areaY = cy + 12 + thickPad(), areaH = listH - 24 - 2 * thickPad();  // (room for a thick highlight border)
    const float target = GRID_ASPECT >= 1 ? 190.0f : 150.0f;  // about this wide
    const int cols = std::max(2, (int)((areaW + gap) / (target + gap)));
    const float tileW = floorf((areaW - (cols - 1) * gap) / cols), coverH = roundf(tileW / GRID_ASPECT), capH = 28, tileH = coverH + capH + gap;
    const int rows = std::max(1, (int)((areaH + gap) / tileH)), n = (int)roms.size(), totalRows = (n + cols - 1) / cols;
    gridCols = cols; gridRowsShown = rows;
    // The scroll position in pixels (UI units): a drag follows the finger, a flick glides on and slows, and the
    // D-pad or paging slides it (eased) until the highlighted game is in view.
    const float maxScroll = std::max(0.0f, totalRows * tileH - gap - areaH);
    static double scrollAt = 0;
    const double nowT = nowMs(), dt = scrollAt ? std::min(0.1, (nowT - scrollAt) / 1000.0) : 0;
    scrollAt = nowT;
    static int sbRow = -1, sbSet = -1;  // the scroll bar's row (it may move it)
    if (sbSet >= 0 && sbRow != sbSet) { gridScroll = gridScrollTo = sbRow * tileH; gridFlingV = 0; touchScrolled = true; }
    if (gridDragging && !touchIsDown) {  // the finger lifted: a quick one glides on
      gridDragging = false;
      if (fabsf(gridDragV) > 2) gridFlingV = gridDragV * 1.5f;
    }
    if (gridFlingV != 0) {
      gridScroll += gridFlingV * (float)(dt * 120); gridFlingV *= (float)pow(0.95, dt * 120);
      if (fabsf(gridFlingV) < 0.3f || gridScroll < 0 || gridScroll > maxScroll) gridFlingV = 0;
      gridScrollTo = gridScroll;
    }
    if (!touchScrolled && !gridDragging) {  // the highlighted game in view
      const float selTop = (selected / cols) * tileH, selBottom = selTop + tileH - gap;
      if (selTop < gridScrollTo) gridScrollTo = selTop;
      if (selBottom > gridScrollTo + areaH) gridScrollTo = selBottom - areaH;
    }
    gridScrollTo = std::max(0.0f, std::min(maxScroll, gridScrollTo));
    // A move of the highlighted game slides the grid in a fixed 0.12 s, slowing to a clean stop (no long creep at
    // the end); a new step during a slide carries on from where the grid is, so a held D-pad moves it steadily.
    static float slideFrom = 0, slideTo = 0; static double slideStart = 0;
    if (gridScrollTo != slideTo) { slideFrom = gridScroll; slideTo = gridScrollTo; slideStart = nowT; }
    if (!gridDragging && gridFlingV == 0) {
      const float u = (float)std::min(1.0, (nowT - slideStart) / 120.0), e = 1 - (1 - u) * (1 - u) * (1 - u);  // ease out
      gridScroll = slideFrom + (slideTo - slideFrom) * e;
    }
    gridScroll = std::max(0.0f, std::min(maxScroll, gridScroll));
    const int topRow = std::max(0, (int)(gridScroll / tileH));
    // (to whole screen pixels: a move steps by the same amount each frame, not 1 and 2 pixels in turn)
    const float scrollFrac = roundf((gridScroll - topRow * tileH) * UI_SCALE) / UI_SCALE;
    top = topRow * cols;
    addMoveHit(cx, cy, gridW, listH, [maxScroll](float, float dy) {  // drag: the grid follows the finger
      gridDragging = true; gridFlingV = 0; touchScrolled = true;
      gridScroll = std::max(0.0f, std::min(maxScroll, gridScroll - dy)); gridScrollTo = gridScroll;
      gridDragV = gridDragV * 0.5f - dy * 0.5f;
    });
    // inside the panel's border, clear of its rounded corners: no cover is ever drawn over it or outside
    gfx::clip((int)cx + 2, (int)cy + 4, (int)gridW - 4, (int)listH - 8);
    for (int r = 0; r <= rows + 1; r++) for (int c = 0; c < cols; c++) {  // (and the rows cut off at the panel's edges)
      const int idx = (topRow + r) * cols + c;
      if (idx >= n) break;
      const float tx = roundf(areaX + c * (tileW + gap)), ty = roundf((areaY + r * tileH - scrollFrac) * UI_SCALE) / UI_SCALE;
      const Rom& rom = roms[idx];
      const bool sel = idx == selected && (focusArea == "list" || focusArea == "buttons"), held = touchHolding(tx, ty, tileW, coverH + capH);
      gfx::Image* const im = coverFor(rom.name);
      {  // the highlight: hugging the cover's top (a cover shorter than its slot leaves no band above it), with room
         // for a thick border (drawn inward) so the cover never covers it
        float fTop = ty;
        if (im) { const float ia = (float)gfx::imageWidth(im) / gfx::imageHeight(im); fTop = roundf(ty + coverH - std::min(coverH, tileW / ia)); }
        const float m = 6 + thickPad(), fy = fTop - m, fh = ty + coverH + capH + 2 + thickPad() - fy;
        if ((sel && focusArea == "list") || held) gfx::rect(tx - m, fy, tileW + 2 * m, fh, col(C_SEL), 8, 2, col(C_ACCENT));
        else if (sel) gfx::rect(tx - m, fy, tileW + 2 * m, fh, Color{0, 0, 0, 0}, 8, 1, col(C_DIM));  // the game Play / Favourite act on
      }
      const string title = displayName(rom.name);
      if (im) {  // fitted in its slot, standing on the slot's bottom
        const float ia = (float)gfx::imageWidth(im) / gfx::imageHeight(im);
        float dw = tileW, dh = tileW / ia;
        if (dh > coverH) { dh = coverH; dw = coverH * ia; }
        gfx::imageDraw(im, roundf(tx + (tileW - dw) / 2), roundf(ty + coverH - dh), roundf(dw), roundf(dh));
      } else if (!coverMissing(rom.name)) {  // still loading: a quiet empty slot (no flash of a title card)
        gfx::rect(tx, ty, tileW, coverH, col(C_PANEL, 0.45f), 6);
      } else {  // no art: the title on a card
        gfx::rect(tx, ty, tileW, coverH, col(C_PANEL), 6, 1, col(C_LINE));
        vector<string> ls = wrap(title, tileW - 16, 16);
        if (ls.size() > 5) ls.resize(5);
        float ly = ty + (coverH - ls.size() * 21.0f) / 2;
        for (auto& l : ls) { gfx::text(l, tx + (tileW - gfx::textWidth(l, 16)) / 2, ly, 16, col(C_DIM)); ly += 21; }
      }
      if (contains(favourites, rom.name)) {  // a favourite: a star in the corner
        gfx::imageDraw(starImage(), tx + tileW - 30, ty + 4, 26, 26);
      }
      {  // the title: a long one under the highlighted cover scrolls (after 1 s), as in the list
        const float cw = gfx::textWidth(title, rowSmall());
        static int capIdx = -1; static double capSince = 0;
        const bool lit = sel && focusArea == "list";
        if (lit && idx != capIdx) { capIdx = idx; capSince = nowMs(); }
        if (lit && cw > tileW) {
          const double over = cw - tileW, speed = 40, cycle = 1.0 + over / speed + 1.5;
          const double at = fmod((nowMs() - capSince) / 1000.0, cycle);
          const float off = at < 1.0 ? 0 : (float)std::min(over, (at - 1.0) * speed);
          gfx::clip((int)tx, (int)(ty + coverH), (int)tileW, (int)capH + 4);
          textMid(title, tx - off, ty + coverH + 2, capH, rowSmall(), col(C_TEXT));
          gfx::noClip();
          gfx::clip((int)cx + 2, (int)cy + 4, (int)gridW - 4, (int)listH - 8);  // (the grid's own clip again)
        } else {
          const string cap = gfx::ellipsize(title, tileW, rowSmall());
          textMid(cap, tx + (tileW - gfx::textWidth(cap, rowSmall())) / 2, ty + coverH + 2, capH, rowSmall(), col(sel ? C_TEXT : C_DIM));
        }
      }
      addHit(tx, ty, tileW, coverH + capH, [idx] { if (focusArea == "list" && selected == idx) touchPress |= B_A; else { focusArea = "list"; selected = idx; } });
    }
    gfx::noClip();
    gfx::rect(cx, cy, gridW, listH, Color{0, 0, 0, 0}, 10, 1, col(C_LINE));  // the border again, over the covers
    for (int k = 1; k <= 2 * rows * cols; k++) {  // two pages above and below, decoded ahead (paging lands on covers ready)
      const int below = (topRow + rows) * cols + k - 1, above = topRow * cols - k;
      if (below < n) coverFor(roms[below].name);
      if (above >= 0) coverFor(roms[above].name);
    }
    coverFrame();
    sbRow = sbSet = topRow;
    scrollbar(cx + gridW - barW() - 6, cy + 6, listH - 12, totalRows, rows, topRow, &sbRow);
  } else {
    addHit(cx, cy, gridW, listH, nullptr, [VIS](int rows) {  // drag: the page scrolls
      top = std::max(0, std::min(std::max(0, (int)roms.size() - VIS), top + rows)); touchScrolled = true;
    });
    gfx::clip((int)cx, (int)cy, (int)gridW, (int)listH);
    for (int i = 0; i < VIS && top + i < (int)roms.size(); i++) {
      const Rom& r = roms[top + i];
      float ry = cy + 6 + colHeadH + i * ROW_H, rx = cx + 6, rw = gridW - 12 - inner;
      bool active = (top + i == selected && focusArea == "list") || touchHolding(cx + 6, cy + 6 + colHeadH + i * ROW_H, gridW - 12 - inner, ROW_H);
      if (active) { gfx::rect(rx, ry, rw, ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, ROW_H, col(C_ACCENT), 2); }
      else if (top + i == selected && focusArea == "buttons") gfx::rect(rx, ry, 4, ROW_H, col(C_DIM), 2);  // the game Play / Favourite act on
      {  // tap: select, then start; the Favourite column toggles the mark
        const int idx = top + i; const string nm = r.name;
        addHit(rx, ry, rw, ROW_H, [idx] { if (focusArea == "list" && selected == idx) touchPress |= B_A; else { focusArea = "list"; selected = idx; } });
        addHit(favC - metadataW / 2, ry, metadataW, ROW_H, [nm, idx] { focusArea = "list"; selected = idx; toggleFavourite(nm); });
      }
      bool fav = contains(favourites, r.name);
      string played = playLabel(r.name);
      if (!played.empty()) textMid(played, timeC - gfx::textWidth(played, rowSmall()) / 2, ry, ROW_H, rowSmall(), col(C_DIM));
      if (fav) {  // a star, centred in the column and the row
        const float sz = touchControls ? 30 : 26;
        gfx::imageDraw(starImage(), roundf(favC - sz / 2), roundf(ry + (ROW_H - sz) / 2), sz, sz);
      }
      const string& date = lastPlayedLabel(r.name);
      if (!date.empty()) textMid(date, playedC - gfx::textWidth(date, rowSmall()) / 2, ry, ROW_H, rowSmall(), col(C_DIM));
      {  // the title: a long one on the highlighted row scrolls (after 1 s) to show the rest
        const string title = displayName(r.name);
        const float avail = favC - metadataW / 2 - rx - 28, tw = gfx::textWidth(title, rowText());
        static int marqueeRow = -1; static double marqueeSince = 0;
        if (active && top + i != marqueeRow) { marqueeRow = top + i; marqueeSince = nowMs(); }
        if (active && tw > avail) {
          const double over = tw - avail, speed = 40;  // UI units per second
          const double cycle = 1.0 + over / speed + 1.5;  // wait, scroll, rest at the end
          const double at = fmod((nowMs() - marqueeSince) / 1000.0, cycle);
          const float off = at < 1.0 ? 0 : (float)std::min(over, (at - 1.0) * speed);
          gfx::clip((int)(rx + 16), (int)ry, (int)avail, (int)ROW_H);
          textMid(title, rx + 16 - off, ry, ROW_H, rowText(), col(C_TEXT));
          gfx::noClip();
          gfx::clip((int)cx, (int)cy, (int)gridW, (int)listH);  // (the list's own clip again)
        } else textMid(gfx::ellipsize(title, avail, rowText()), rx + 16, ry, ROW_H, rowText(), col(C_TEXT));
      }
    }
    gfx::noClip();
    scrollbar(cx + gridW - barW() - 6, cy + 6 + colHeadH, listH - 12 - colHeadH, (int)roms.size(), VIS, top, &top);
  }
  if (auto& ba = boxart::status(); ba.running) {  // a box art download: how far, a badge in the list's bottom right
    const int tot = ba.total.load(), dn = ba.done.load();
    const string t = "Box art " + std::to_string(std::min(tot, dn + 1)) + " / " + std::to_string(tot);
    const float bw = 90, tw = gfx::textWidth(t, headText(), gfx::BOLD), w = 14 + tw + 12 + bw + 14, h = 34;
    const float bx = roundf(cx + gridW - barW() - 18 - w), by = roundf(cy + listH - 10 - h);
    gfx::rect(bx, by, w, h, col(C_PANEL, 0.96f), 8, 1, col(C_LINE));
    textMid(t, bx + 14, by, h, headText(), col(C_ACCENT), gfx::BOLD);
    const float rx = bx + 14 + tw + 12, ry = roundf(by + h / 2 - 3);
    gfx::rect(rx, ry, bw, 6, col(C_LINE), 3);
    if (tot > 0) gfx::rect(rx, ry, std::max(6.0f, bw * dn / tot), 6, col(C_ACCENT), 3);
  }
  if (detailsPanel) drawArtPanel(cx + gridW + 16, cy, ART_W, listH, selected < (int)roms.size() ? &roms[selected] : nullptr);
  cy += listH + 12;
  // Footer: button legends, right-aligned, each one place and one size
  const bool hasGame = selected < (int)roms.size();
  const vector<Legend> legends = {
      {"{A}", "Play", [] { if (selected < (int)roms.size()) pressFooter(0); }, false},
      {"-", "Favourite", [] { if (selected < (int)roms.size()) pressFooter(1); }, false},
      {"+", "Search", [] { pressFooter(2); }, false},
      {"{Y}", "Refresh", [] { pressFooter(4); }, false},
      {"{X}", "Settings", [] { pressFooter(3); }, false},
      {"LS", "Sort", [] { pressFooter(7); }, false},
      {"RS", "View", [] { pressFooter(9); }, false},
      {"{B}", exitArmed ? "Confirm exit" : "Exit", [] { pressFooter(5); }, exitArmed != 0}};
  // Each legend is as wide as its longest label (a label change never moves anything); the same
  // space before, between and after them, so the row is evenly spaced and centred on the game list.
  float sumW = 0; for (float w : fw) sumW += w;
  const float g = std::max(4.0f + thickPad(), (listInnerW - sumW) / (legends.size() + 1));
  const float ly = roundf(cy + (footH - btnH()) / 2 + 2);
  float lx0 = cx + g;
  for (size_t i = 0; i < legends.size(); i++) {
    const Legend& L = legends[i];
    const bool dim = (i == 0 || i == 1) && !hasGame;
    const float bw = fw[i], lx = roundf(lx0);
    lx0 += bw + g;
    const bool focus = focusArea == "buttons" && btnIndex == (int)i, flash = (footFlash == FOOT_IDS[i] && nowMs() < footFlashUntil) || touchHolding(lx - g / 2, ly - 4, bw + g, btnH() + 8);
    gfx::rect(lx, ly, bw, btnH(), flash ? col(C_ACCENT) : focus || L.lit ? col(C_SEL) : col(C_BG), BTN_RADIUS, focus ? 2 : 1,
              col(flash || focus || L.lit ? C_ACCENT : C_LINE));
    const string label = gfx::ellipsize(L.label, bw - 30 - 10 - 16 - 2 * thickPad(), BTN_TEXT);  // (fits: the buttons shrink to fit the row)
    const float kw = 30, kx = lx + (bw - (kw + 10 + gfx::textWidth(label, BTN_TEXT))) / 2, ky = ly + (btnH() - kw) / 2;
    const string k = L.key;
    // the theme's colour for this face button (Retro Colour), if it has one
    const int fi = k == "{A}" ? 0 : k == "{B}" ? 1 : k == "{X}" ? 2 : k == "{Y}" ? 3 : -1;
    const gfx::Color keyInk = keyInkOf(fi);
    const Color keyFill = keyFillOf(fi, dim);
    if (k[0] == '{' && singleJoyConConnected) {
      gfx::rect(kx, ky, kw, kw, keyFill, kw / 2);
      drawFaceGlyph(glyphToken(k, 0), kx + (kw - glyphWidth(20)) / 2, ky + (kw - 20 * 1.25f) / 2, 20, keyInk);
    } else {  // the circle and its letter as one: the letter exactly in the middle
      const string t = k[0] == '{' ? string(1, k[1]) : k;
      gfx::badge(t, kx, ky, kw, kw, kw / 2, keyFill, t.size() > 1 ? 13 : 18, keyInk, gfx::BOLD);
    }
    textMid(label, kx + kw + 10, ly, btnH(), BTN_TEXT, flash ? col(C_PANEL) : col(dim ? C_DIM : L.lit ? C_ACCENT : C_TEXT));
    addHit(lx - g / 2, ly - 4, bw + g, btnH() + 8, L.tap);
  }
}

#define SET_ROW_H ROW_H  // as the game list's rows
static void drawFolderPicker() {
  const float winW = 960, padX = 20, padY = 18, rowH = touchControls ? 72 : 34;
  int total = pickRows();
  int vis = std::max(5, std::min(total, (int)((gfx::UI_H * 0.9 - 36 - 50 - 40 - 14) / rowH)));
  if (!touchScrolled) {
    if (pickSel < pickTop) pickTop = pickSel;
    if (pickSel >= pickTop + vis) pickTop = pickSel - vis + 1;
  }
  pickTop = std::max(0, std::min(std::max(0, total - vis), pickTop));
  bool scroll = total > vis;
  float listW = winW - 2 * padX - (scroll ? 16 : 0), listH = vis * rowH + 12;
  float winH = padY * 2 + 34 + 12 + 30 + listH;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  addHit(0, 0, gfx::UI_W, gfx::UI_H, [] { touchPress |= B_B; });  // outside: cancel
  addHit(x, y, winW, winH, [] {});
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, "ROM folder", "{A}: open   {B}: cancel");
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
    bool active = i == pickSel || touchHolding(cx + 6, cy + 6 + k * rowH, listW - 12, rowH);
    float rx = cx + 6, ry = cy + 6 + k * rowH, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, rowH, col(C_SEL), 6); gfx::rect(rx, ry, 4, rowH, col(C_ACCENT), 2); }
    addHit(rx, ry, rw, rowH, [i] { if (pickSel == i) touchPress |= B_A; else pickSel = i; });
    string label; Color c = col(C_TEXT);
    int up = pickAtRoot() ? 0 : 1;
    if (i == 0) { label = "Use this folder"; c = col(C_ACCENT); }
    else if (up && i == 1) label = ".. (up one level)";
    else label = pickDirs[i - 1 - up] + "/";
    textMid(gfx::ellipsize(label, rw - 32, touchControls ? 22 : 18), rx + 16, ry, rowH, touchControls ? 22 : 18, c, i == 0 ? gfx::BOLD : gfx::REGULAR);
  }
  addHit(cx, cy, listW, listH, nullptr, [total, vis](int rows) { pickTop = std::max(0, std::min(std::max(0, total - vis), pickTop + rows)); touchScrolled = true; });
  scrollbar(cx + listW + 8 - (barW() - 8) / 2, cy, listH, total, vis, pickTop, &pickTop);
}
static void drawRemap() {
  const float winW = 640, padX = 20, padY = 18, innerW = winW - 2 * padX, rowH = 34;
  const bool done = remapStep >= NPAD, connected = padConnected(remapWho);
  const float listH = NPAD * rowH + 12;
  const float winH = padY * 2 + 34 + 12 + 30 + listH + 12 + btnH();
  const float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, "Player " + std::to_string(remapWho + 1) + " buttons", remapForGame ? "for this game" : "for all games");
  cy += 34 + 12;
  string hint;
  if (!connected) addHit(0, 0, gfx::UI_W, gfx::UI_H, [] { touchPress |= B_B; });
  if (!connected) hint = "No controller for player " + std::to_string(remapWho + 1) + " (connect one with Pair controllers). {B}: back";
  else if (!done && remapWaitRelease) hint = "Let go of all buttons...";
  else if (!done) hint = "Press the button for " + string(PAD_BUTTONS[remapStep].name) + ", or wait " +
                         std::to_string(std::max(1, (int)ceil((remapDeadline - nowMs()) / 1000))) + " s to leave it unmapped";
  else hint = "Save this layout?";
  textMid(gfx::ellipsize(hint, innerW, 18), cx, cy, 26, 18, col(C_TEXT));
  cy += 30;
  gfx::rect(cx, cy, innerW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int i = 0; i < NPAD; i++) {
    const float rx = cx + 6, ry = cy + 6 + i * rowH, rw = innerW - 12;
    const bool current = connected && !done && i == remapStep;
    if (current) { gfx::rect(rx, ry, rw, rowH, col(C_SEL), 6); gfx::rect(rx, ry, 4, rowH, col(C_ACCENT), 2); }
    textMid(PAD_BUTTONS[i].name, rx + 16, ry, rowH, 18, col(C_TEXT));
    const u32 style = connected ? padGetStyleSet(padStates[remapWho]) : 0;
    string v = i < remapStep ? (remapNew[i] >= 0 ? inputName(remapNew[i], style) : "Not mapped") : current ? "..." : "";
    if (!v.empty()) { float w = gfx::textWidth(v, 18); textMid(v, rx + rw - 12 - w, ry, rowH, 18, col(i < remapStep && remapNew[i] < 0 ? C_DIM : C_TEXT)); }
  }
  cy += listH + 12;
  static const char* OPTS[3] = {"Save", "Start again", "Cancel"};
  const float bw = (innerW - 20) / 3;
  for (int i = 0; i < 3; i++) {
    const bool on = (done && i == remapPick) || (done && touchHolding(cx + i * (bw + 10), cy - 6, bw, btnH() + 12));
    const float bx = cx + i * (bw + 10);
    if (done) addHit(bx, cy - 6, bw, btnH() + 12, [i] { remapPick = i; touchPress |= B_A; });
    gfx::rect(bx, cy, bw, btnH(), col(on ? C_SEL : C_BG), BTN_RADIUS, 1, col(on ? C_ACCENT : C_LINE));
    const float tw = gfx::textWidth(OPTS[i], BTN_TEXT);
    textMid(OPTS[i], bx + (bw - tw) / 2, cy, btnH(), BTN_TEXT, col(done ? C_TEXT : C_DIM));
  }
}
static void drawSettings() {
  // As wide as its tabs need (with their L / R caps), at least 640.
  const float padX = 20, padY = 18, winW = std::max(640.0f, equalTabsWidth(SETTINGS_CATS) + BACK_GAP + BACK_W + 2 * (SHOULDER_W + SHOULDER_GAP) + 2 * padX + 16), innerW = winW - 2 * padX;
  int n = (int)SETTINGS_ITEMS.size(), total = n + 1;
  // One size for every category (the largest, advanced shown); more rows than fit the screen scroll.
  int vis = std::max(5, std::min(std::max(total, settingsFixedRows()), (int)((gfx::UI_H * 0.94 - 36 - 50 - 14 - tabRowH() - 10) / SET_ROW_H)));
  if (!touchScrolled) {
    if (settingsIndex >= 0 && settingsIndex < settingsTop) settingsTop = settingsIndex;
    if (settingsIndex < 0) settingsTop = 0;
    if (settingsIndex >= settingsTop + vis) settingsTop = settingsIndex - vis + 1;
  }
  settingsTop = std::max(0, std::min(std::max(0, total - vis), settingsTop));
  float listW = innerW - 16, listH = vis * SET_ROW_H + 12;  // (scrollbar gutter always kept)
  float winH = padY * 2 + 34 + 12 + tabRowH() + 10 + listH;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  addHit(0, 0, gfx::UI_W, gfx::UI_H, [] { touchPress |= B_B; });  // outside the window: back to the game list
  addHit(x, y, winW, winH, [] {});
  panelHeader(cx, cy, "Settings", "for all games");

  cy += 34 + 12;
  drawTabBar(cx + SHOULDER_W + SHOULDER_GAP, cy, SETTINGS_CATS, settingsCat, settingsIndex < 0 && !settingsOnBack ? settingsCat : -1,
             [](int i) { if (i != settingsCat) { settingsCat = i; resetArmed = false; rebuildSettings(); settingsTop = 0; } settingsIndex = -1; settingsOnBack = false; }, false,
             tabRowTabsW(listW));
  drawTabBack(cx, listW - SHOULDER_GAP - SHOULDER_W, cy, settingsIndex < 0 && settingsOnBack, [] { touchPress |= B_B; });
  drawRowShoulders(cx, listW, cy, [](int d) { settingsStep(d); });
  cy += tabRowH() + 10;
  gfx::rect(cx, cy, listW, listH, col(C_BG), 10, 1, col(C_LINE));
  addHit(cx, cy, listW, listH, nullptr, [total, vis](int rows) { settingsTop = std::max(0, std::min(std::max(0, total - vis), settingsTop + rows)); touchScrolled = true; });
  for (int k = 0; k < vis; k++) {
    int i = settingsTop + k;
    if (i >= total) break;
    bool active = i == settingsIndex || touchHolding(cx + 6, cy + 6 + k * SET_ROW_H, listW - 12, SET_ROW_H);
    float rx = cx + 6, ry = cy + 6 + k * SET_ROW_H, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, SET_ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, SET_ROW_H, col(C_ACCENT), 2); }
    // tap: select, then act (A); on a value, its left half goes back, its right half forward
    addHit(rx, ry, rw, SET_ROW_H, [i] { if (settingsIndex == i) touchPress |= B_A; else settingsIndex = i; });
    if (i < n) {
      vector<string> ch = SETTINGS_ITEMS[i].choices();
      if (!SETTINGS_ITEMS[i].set) {  // information only (Version)
        const string& v = ch[0];
        textMid(SETTINGS_ITEMS[i].label, rx + 16, ry, SET_ROW_H, rowText(), col(C_TEXT));
        textMid(v, rx + rw - 12 - gfx::textWidth(v, rowText()), ry, SET_ROW_H, rowText(), col(C_DIM));
        continue;
      }
      // the value at most 45% of the row (a long one, e.g. a download's progress and title, ends in "..."),
      // the label in what is left, so the two never overlap
      string val = ch[SETTINGS_ITEMS[i].get()];
      if (val.find('{') == string::npos) val = gfx::ellipsize(val, rw * 0.45f - 40, rowText());
      const float vw = valueBox(val, rx + rw - 12, ry, SET_ROW_H, rowText(), active);
      textMid(gfx::ellipsize(SETTINGS_ITEMS[i].label, rw - 12 - vw - 16 - 24, rowText()), rx + 16, ry, SET_ROW_H, rowText(), col(C_TEXT));
      if (ch.size() > 1) {
        addHit(rx + rw - 12 - vw - 10, ry, vw + 22, SET_ROW_H, [i] { settingsIndex = i; touchPress |= B_RIGHT; });  // a tap: the next choice (a cycle)
      }
    } else textMid("Back", rx + 16, ry, SET_ROW_H, rowText(), col(C_TEXT));
  }
  scrollbar(cx + listW + 8 - (barW() - 8) / 2, cy, listH, total, vis, settingsTop, &settingsTop);
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

static void openMenu();
static void drawGame(bool dim) {
  if (running) gfx::pictureDraw(picture, picX, picY, picW, picH, picCX, picCY, picCW, picCH);
  drawStatsBar();
  if (hintUntil && !dim) {
    const char* hint = singleJoyConConnected ? "SL + SR + stick press: Menu" : "L + R + stick press: Menu";
    float w = gfx::textWidth(hint, 16);
    gfx::text(hint, gfx::UI_W - 16 - w, gfx::UI_H - 12 - 20, 16, gfx::rgb(0x8891a4));
  }
  static double menuBtnUntil = 0;
  if (running && mode == "running" && menuButtonMode == 0) addHit(0, 0, gfx::UI_W, gfx::UI_H, [] { menuBtnUntil = nowMs() + 3000; });  // a touch on the game shows the Menu button
  if (running && mode == "running" && (menuButtonMode == 1 || (menuButtonMode == 0 && nowMs() < menuBtnUntil))) {  // a Menu button to tap
    const float w = 132, h = 64, x = gfx::UI_W - 16 - w, y = 12;
    const bool held = touchHolding(x - 8, y - 8, w + 16, h + 16);
    gfx::rect(x, y, w, h, held ? col(C_ACCENT, 0.7f) : gfx::Color{0, 0, 0, 0.45f}, BTN_RADIUS, 1, gfx::rgb(0xffffff, held ? 0.8f : 0.35f));
    textMid("Menu", x + (w - gfx::textWidth("Menu", 18)) / 2, y, h, 18, gfx::rgb(0xffffff, 0.8f));
    addHit(x - 8, y - 8, w + 16, h + 16, [] { if (mode == "running") openMenu(); });
  }
}

static void drawMenu() {
  if (dimInMenu) gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f});
  // The game list's and Settings' layout: accent-bar title, tabs, rows of ROW_H in a list box.
  // The tabbed menu is as wide as Settings and keeps one size (its largest category); other menus
  // (cheats) fit their rows.
  const float padX = 20, padY = 18, panelW = menuHasTabs ? std::max(640.0f, equalTabsWidth(MENU_CATS) + BACK_GAP + BACK_W + 2 * (SHOULDER_W + SHOULDER_GAP) + 2 * padX + 16) : 460, innerW = panelW - 2 * padX;
  const float tabsH = menuHasTabs ? tabRowH() + 10 : 0;
  int n = (int)menuItems.size();
  const int fit = (int)((gfx::UI_H * 0.94f - padY * 2 - 34 - 12 - tabsH - 12) / ROW_H);
  int vis = std::max(4, std::min(menuHasTabs ? std::max(n, menuFixedRows()) : n, fit));
  if (!touchScrolled) {
    if (menuIndex >= 0 && menuIndex < menuTop) menuTop = menuIndex;
    if (menuIndex < 0) menuTop = 0;
    if (menuIndex >= menuTop + vis) menuTop = menuIndex - vis + 1;
  }
  if (menuTop > n - vis) menuTop = std::max(0, n - vis);
  const bool gutter = menuHasTabs || n > vis;  // (tabbed: the scrollbar's gutter always kept)
  float listW = innerW - (gutter ? 16 : 0), listH = vis * ROW_H + 12;
  float h = padY * 2 + 34 + 12 + tabsH + listH;
  float x = (gfx::UI_W - panelW) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, panelW, h, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, gfx::ellipsize(menuTitle, innerW - 20, 26, gfx::BOLD), "");
  cy += 34 + 12;
  addHit(0, 0, gfx::UI_W, gfx::UI_H, [] { touchPress |= B_B; });  // outside the panel: close / back
  addHit(x, y, panelW, h, [] {});
  if (menuHasTabs) {
    drawTabBar(cx + SHOULDER_W + SHOULDER_GAP, cy, MENU_CATS, menuCat, menuIndex < 0 && !menuOnBack ? menuCat : -1, [](int i) { if (i != menuCat) { menuCat = i; openMenu(); } menuIndex = -1; menuOnBack = false; }, false,
               tabRowTabsW(listW));
    drawTabBack(cx, listW - SHOULDER_GAP - SHOULDER_W, cy, menuIndex < 0 && menuOnBack, [] { touchPress |= B_B; });
    drawRowShoulders(cx, listW, cy, [](int d) { menuStep(d); });
    cy += tabsH;
  }
  addHit(cx, cy, listW, listH, nullptr, [n, vis](int rows) { menuTop = std::max(0, std::min(std::max(0, n - vis), menuTop + rows)); touchScrolled = true; });
  gfx::rect(cx, cy, listW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int k = 0; k < vis && menuTop + k < n; k++) {
    int i = menuTop + k;
    bool active = i == menuIndex || touchHolding(cx + 6, cy + 6 + k * ROW_H, listW - 12, ROW_H);
    float rx = cx + 6, ry = cy + 6 + k * ROW_H, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, ROW_H, col(C_ACCENT), 2); }
    const MenuItem& m = menuItems[i];
    float vw = 0;
    addHit(rx, ry, rw, ROW_H, [i] { if (menuIndex == i) touchPress |= B_A; else menuIndex = i; });
    if (m.option()) {
      vector<string> ch = m.choices(); int v = m.get(); vw = valueBox(ch[std::max(0, std::min((int)ch.size() - 1, v))], rx + rw - 12, ry, ROW_H, rowText(), active) + 16;
      addHit(rx + rw - 12 - vw - 4, ry, vw + 16, ROW_H, [i] { menuIndex = i; touchPress |= B_RIGHT; });  // a tap: the next choice (a cycle)
    }
    textMid(gfx::ellipsize(m.label, rw - 28 - vw, rowText()), rx + 16, ry, ROW_H, rowText(), col(C_TEXT));
  }
  scrollbar(cx + listW + 8 - (barW() - 8) / 2, cy, listH, n, vis, menuTop, &menuTop);
}

static void drawCodeEditor() {
  gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f});
  GG g; bool ok = ggDecode(ggEditCode(), g);
  char info[80];
  if (ok) {
    if (g.compare >= 0) snprintf(info, sizeof info, "$%04X = %02X if %02X", g.addr, g.value, g.compare);
    else snprintf(info, sizeof info, "$%04X = %02X", g.addr, g.value);
  } else snprintf(info, sizeof info, "Codes have 6 or 8 letters");
  string help = "Left/Right: move   Up/Down: letter   {A}: save   {B}: cancel";
  float cellsW = 8 * 52, w = std::max(cellsW, gfx::textWidth(help, 16)) + 56, h = 24 + 24 * LH + 16 + 54 + 14 + 2 * 16 * LH + 24;
  float x = (gfx::UI_W - w) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, w, h, col(C_PANEL), 14, 1, col(C_LINE));
  addHit(0, 0, gfx::UI_W, gfx::UI_H, [] { touchPress |= B_B; });  // outside: cancel
  addHit(x, y, w, h, [] { touchPress |= B_A; });  // the panel (below the cells): save
  string title = "Enter Game Genie code";
  gfx::text(title, x + (w - gfx::textWidth(title, 24, gfx::BOLD)) / 2, y + 22, 24, col(C_TEXT), gfx::BOLD);
  float cy = y + 24 + 24 * LH + 16, cx = x + (w - cellsW) / 2;
  for (int i = 0; i < 8; i++) {
    float bx = cx + i * 52 + 4;
    addHit(bx, cy, 44, 27, [i] { ggPos = i; touchUp = true; }); addHit(bx, cy + 27, 44, 27, [i] { ggPos = i; touchDown = true; });  // top half: up, bottom: down
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
  const float lh = 22 * 1.45f, w = 760, foot = messageAsk ? 64 : 18 * LH;
  const float h = 30 * 2 + lines.size() * lh + 18 + foot;
  float x = (gfx::UI_W - w) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, w, h, col(C_PANEL), 14, 1, col(C_LINE));
  float cy = y + 30;
  for (auto& l : lines) { textMid(l, x + 30, cy, lh, 22, col(C_TEXT)); cy += lh; }
  if (!messageAsk) {
    glyphText("Press {A} or {B} to continue", x + 30, cy + 18 - 2, 18, col(C_DIM));
    addHit(0, 0, gfx::UI_W, gfx::UI_H, [] { touchPress |= B_A; });
    return;
  }
  // a question: two buttons, A the first, B the second
  const float bh = 48, bw = 220, by = cy + 18;
  const struct { const string* label; const char* key; float bx; u64 button; } bs[2] = {{&messageYesLabel, "{A}", x + w - 30 - 2 * bw - 16, B_A}, {&messageNoLabel, "{B}", x + w - 30 - bw, B_B}};
  for (auto& b : bs) {
    const bool held = touchHolding(b.bx, by, bw, bh);
    gfx::rect(b.bx, by, bw, bh, held ? col(C_ACCENT) : col(b.button == B_A ? C_SEL : C_BG), BTN_RADIUS, 1, col(b.button == B_A ? C_ACCENT : C_LINE));
    const string t = string(b.key) + " " + *b.label;
    glyphText(t, b.bx + (bw - glyphTextWidth(t, 20)) / 2, by + (bh - 20 * 1.25f) / 2, 20, col(C_TEXT));
    const u64 btn = b.button;
    addHit(b.bx, by, bw, bh, [btn] { touchPress |= btn; });
  }
}

static void drawToast() {
  if (!toastUntil) return;
  float w = glyphTextWidth(toastText, 20) + 36, h = 20 * LH + 20;
  float x = roundf((gfx::UI_W - w) / 2), y = gfx::UI_H - 28 - h;
  gfx::rect(x, y, w, h, col(C_PANEL, 0.96f), 8, 1, col(C_LINE));
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
    bool combo = menuCombo(bits) || menuCombo(bits2);
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
    p = nesPad(0, bits); p2 = behindMenu ? 0 : nesPad(1, bits2);
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

// Touches: a tap (lifted within ~20 px of where it went down) calls the topmost hit under it; a
// drag that starts on a hit with a drag action moves it by rows. Touch is in 1280 x 720 panel
// points, the interface in UI_W x UI_H.
static std::function<void(int)> flingDrag;  // a swipe's scroll going on after the finger lifted
static float flingV = 0, flingAcc = 0;
static void stopFling() { flingDrag = nullptr; flingV = 0; flingAcc = 0; gridFlingV = 0; }
static void touchUpdate() {
  static bool down = false, dragging = false;
  static float dragV = 0;  // the finger's speed (UI units a frame, smoothed)
  static float sx = 0, sy = 0, lx = 0, ly = 0, acc = 0;
  static int dragHit = -1;
  HidTouchScreenState st{};
  const bool now = hidGetTouchScreenStates(&st, 1) && st.count > 0;
  const float x = now ? st.touches[0].x * (float)gfx::UI_W / 1280.0f : lx, y = now ? st.touches[0].y * (float)gfx::UI_H / 720.0f : ly;
  // the topmost hit under a point that taps (or drags)
  auto hitAt = [](float px, float py, bool drag) { for (int i = (int)hits.size() - 1; i >= 0; i--) { const Hit& h = hits[i]; if ((drag ? (bool)h.drag || (bool)h.move : (bool)h.tap) && px >= h.x && px < h.x + h.w && py >= h.y && py < h.y + h.h) return i; } return -1; };
  touchIsDown = now; touchX = x; touchY = y;
  if (now && !down) { touchStartX = x; touchStartY = y; }
  touchDragging = dragging || (now && down && (fabsf(x - sx) >= 20 || fabsf(y - sy) >= 20));
  if (now && !down && debugLog) record("touch: down at panel %u, %u (UI %.0f, %.0f)", st.touches[0].x, st.touches[0].y, x, y);
  if (now && !down) { down = true; dragging = false; sx = lx = x; sy = ly = y; acc = 0; dragV = 0; stopFling(); dragHit = hitAt(x, y, true); }
  else if (now) {
    if (!dragging && dragHit >= 0 && dragHit < (int)hits.size() && hits[dragHit].move && (fabsf(x - sx) > 8 || fabsf(y - sy) > 8)) dragging = true;
    else if (!dragging && dragHit >= 0 && fabsf(y - sy) > 20) dragging = true;
    if (dragging && dragHit >= 0 && dragHit < (int)hits.size() && hits[dragHit].move) hits[dragHit].move(x - lx, y - ly);
    else if (dragging && dragHit >= 0 && dragHit < (int)hits.size()) {
      acc += y - ly;
      dragV = dragV * 0.5f + (y - ly) * 0.5f;
      const int rows = (int)(acc / ROW_H);
      if (rows) { acc -= rows * ROW_H; hits[dragHit].drag(-rows); }
    }
    lx = x; ly = y;
  } else if (down) {
    down = false;
    if (!dragging && fabsf(lx - sx) < 20 && fabsf(ly - sy) < 20) { const int i = hitAt(lx, ly, false); if (i >= 0) { tapThisFrame = true; auto f = hits[i].tap; f(); } }
    // a quick swipe of a list or the grid: it goes on, slowing (as on a phone)
    if (dragging && dragHit >= 0 && dragHit < (int)hits.size() && hits[dragHit].drag && fabsf(dragV) > 3) { flingDrag = hits[dragHit].drag; flingV = dragV * 1.5f; flingAcc = 0; }
  } else if (flingDrag) {
    flingAcc += flingV;
    const int rows = (int)(flingAcc / ROW_H);
    if (rows) { flingAcc -= rows * ROW_H; flingDrag(-rows); }
    flingV *= 0.965f;
    if (fabsf(flingV) < 0.6f) stopFling();
  }
}
static void handleUi(u64 bits) {
  Nav ev = navEvents(bits);
  if (ev.up || ev.down || (ev.pressed & (B_A | B_B | B_LEFT | B_RIGHT | B_L | B_R))) touchScrolled = false;
  if (ev.up || ev.down || ev.pressed) stopFling();  // a button stops a swipe's scroll
  ev.pressed |= touchPress; ev.up |= touchUp; ev.down |= touchDown;
  touchPress = 0; touchUp = touchDown = false;
  struct ClearTap { ~ClearTap() { tapThisFrame = false; } } clearTap;  // (whichever way handleUi returns)
  u64 pressed = ev.pressed;
  double t = nowMs();
  if (mode == "browser") {
    if (bits & HidNpadButton_StickRDown) bioScroll += 6;   // the right stick scrolls the description
    if (bits & HidNpadButton_StickRUp) bioScroll -= 6;
    if (footPending >= 0) {  // a footer button was used: its action runs when its flash has shown
      if (t < footActAt) return;
      const int i = footPending; footPending = -1;
      const bool game = selected < (int)roms.size();
      switch (i) {
        case 0: if (game) startGame(roms[selected]); break;
        case 1: if (game) toggleFavourite(roms[selected].name); break;
        case 2: openSearch(); if (focusArea == "list" && roms.empty()) focusArea = "tabs"; break;
        case 3: mode = "settings"; settingsIndex = 0; settingsTop = 0; break;
        case 4: scan(); showToast("List refreshed"); break;
        case 6: openSearch(); if (focusArea == "list" && roms.empty()) focusArea = "tabs"; break;
        case 7: { int& rs = romSortByView[listView]; rs = (rs + 1) % ROM_SORT_LABELS.size(); orderRoms(); saveSettings(); showToast("Sort: " + ROM_SORT_LABELS[rs]); break; }
        case 8: searchQuery.clear(); orderRoms(); showToast("Search cleared"); headIndex = (int)LIST_VIEWS.size(); break;
        case 9: cycleView(); break;
        case 5:
          if (exitArmed) quitApp = true;
          else { exitArmed = t + 3000; showToast("Press Exit again to exit", 3000); }
          break;
      }
      return;
    }
    // the left stick press: the next sort order; the right stick press: the next of the four views
    // (a single Joy-Con's stick press is its own side's: a left Joy-Con sorts, a right one changes the view)
    if (pressed & HidNpadButton_StickL) { exitArmed = 0; pressFooter(7); return; }
    if (pressed & HidNpadButton_StickR) { exitArmed = 0; pressFooter(9); return; }
    // L/ZL and R/ZR cycle the All/Favourites/Recent tabs from anywhere.
    // L / R the tabs (ZL / ZR page); on a single Joy-Con, SL cycles the tabs and SR pages down
    int tabStep = singleJoyConConnected ? ((pressed & B_L) ? 1 : 0) : ((pressed & B_R) ? 1 : 0) - ((pressed & B_L) ? 1 : 0);
    {  // ZL / ZR: a page up / down of the list or the grid
      // held for 0.7 s, it keeps paging (every 0.15 s) until let go
      static double pageHeldSince = 0, pageRepeatAt = 0;
      const u64 pgDown = HidNpadButton_ZR | (singleJoyConConnected ? (u64)B_R : 0);  // (a single Joy-Con: SR)
      const int heldDir = ((bits & pgDown) ? 1 : 0) - ((bits & HidNpadButton_ZL) ? 1 : 0);
      int pageStep = ((pressed & pgDown) ? 1 : 0) - ((pressed & HidNpadButton_ZL) ? 1 : 0);
      if (pageStep) { pageHeldSince = t; pageRepeatAt = t + 700; }
      else if (heldDir && pageHeldSince && t >= pageRepeatAt) { pageStep = heldDir; pageRepeatAt = t + 150; }
      if (!heldDir) pageHeldSince = 0;
      if (pageStep && !roms.empty()) {
        const int page = gridView ? std::max(1, gridCols * gridRowsShown) : std::max(1, std::min((int)roms.size(), maxRows()));
        const int last = (int)roms.size() - 1, fresh = (pressed & (HidNpadButton_ZL | pgDown)) != 0;
        if (fresh && pageStep < 0 && selected == 0) selected = last;           // round: the first page, then the last
        else if (fresh && pageStep > 0 && selected == last) selected = 0;     // and the last, then the first
        else selected = std::max(0, std::min(last, selected + pageStep * page));
        focusArea = "list"; touchScrolled = false;
      }
    }
    if (tabStep) {
      int n = (int)LIST_VIEWS.size();
      listView = (listView + tabStep + n) % n; roms.clear(); orderRoms(); selected = 0;
      if (headIndex >= 0 && headIndex < n) headIndex = listView;
      if (focusArea == "list" && roms.empty()) focusArea = "tabs";
    }
    {  // anything but Exit again (B, or A on the Exit button) disarms it
      const u64 again = B_B | (focusArea == "buttons" && btnIndex == FOOT_EXIT ? (u64)B_A : 0);
      if (exitArmed && (t > exitArmed || (pressed & ~again) || ev.up || ev.down)) exitArmed = 0;
    }
    // The footer buttons' own shortcuts: - Favourite, + Search, Y Settings, X Refresh, B Exit
    // (A plays the highlighted game from the list, or presses the footer button the cursor is on).
    const bool game = selected < (int)roms.size();
    if (pressed & B_MINUS) { if (game) pressFooter(1); return; }
    if (pressed & B_PLUS) { pressFooter(2); return; }
    if (pressed & B_Y) { pressFooter(4); return; }  // Refresh
    if (pressed & B_X) { pressFooter(3); return; }  // Settings
    if (pressed & B_B) { pressFooter(5); return; }
    if (focusArea == "list" && roms.empty()) focusArea = "tabs";
    if (focusArea != "list" && focusArea != "buttons") focusArea = "tabs";
    int VIS = std::max(1, std::min((int)roms.size(), maxRows()));
    if (focusArea == "tabs") {  // the header row: tabs, search, sort. Up: the buttons (wrapping round), down: the list
      const int nTabs = (int)LIST_VIEWS.size(), SEARCH = nTabs, CLEAR = nTabs + 1, SORT = nTabs + 2;
      const bool searching = !searchQuery.empty();
      int h = headIndex < 0 ? listView : headIndex;
      if (h == CLEAR && !searching) h = SEARCH;
      if (pressed & B_LEFT) { h = std::max(0, h - 1); if (h == CLEAR && !searching) h = SEARCH; }
      if (pressed & B_RIGHT) { h = std::min(SORT, h + 1); if (h == CLEAR && !searching) h = SORT; }
      headIndex = h;  // moving only highlights: A switches to a tab (L / R and taps switch at once)
      if ((pressed & B_A) && h < nTabs) { if (h != listView) { listView = h; roms.clear(); orderRoms(); } }
      else if ((pressed & B_A) && headIndex == SEARCH) pressFooter(6);
      else if ((pressed & B_A) && headIndex == CLEAR) pressFooter(8);
      else if ((pressed & B_A) && headIndex == SORT) pressFooter(7);
      else if (ev.down) { if (roms.empty()) focusArea = "buttons"; else { focusArea = "list"; selected = 0; } }
      else if (ev.up) { if (roms.empty()) focusArea = "buttons"; else { focusArea = "list"; selected = (int)roms.size() - 1; } }  // round: the last game
    } else if (focusArea == "list" && gridView) {  // the grid: left / right a game, up / down a row
      const int n = (int)roms.size(), c = std::max(1, gridCols);
      if (pressed & B_LEFT) selected = std::max(0, selected - 1);
      if (pressed & B_RIGHT) selected = std::min(n - 1, selected + 1);
      // round: up from the top row to the bottom one, down from the bottom row to the top one (a fresh press;
      // held, it stops at the end)
      const bool freshV = (pressed & (B_UP | B_DOWN | B_SU | B_SD)) != 0;
      if (ev.up) {
        if (selected >= c) selected -= c;
        else if (freshV) focusArea = "tabs";  // the header row first; up again goes round to the bottom row
      }
      if (ev.down) {
        if (selected + c < n) selected += c;
        else if (selected / c < (n - 1) / c) selected = n - 1;  // the last row is short: its last game
        else if (freshV) focusArea = "buttons";  // the footer first; down again goes round to the top row
      }
      if (ev.up || ev.down || (pressed & (B_LEFT | B_RIGHT))) touchScrolled = false;
      if ((pressed & B_A) && game) pressFooter(0);
    } else if (focusArea == "list") {
      // round: up from the first game to the last, down from the last to the first (a fresh press; held, it stops)
      const bool freshV = (pressed & (B_UP | B_DOWN | B_SU | B_SD)) != 0;
      // (the header row first at the top, the footer first at the bottom; one more press goes round)
      if (ev.up) { if (selected > 0) selected--; else if (freshV) focusArea = "tabs"; }
      if (ev.down) { if (selected < (int)roms.size() - 1) selected++; else if (freshV) focusArea = "buttons"; }
      if (pressed & B_LEFT) selected = std::max(0, selected - VIS);
      if (pressed & B_RIGHT) selected = std::min((int)roms.size() - 1, selected + VIS);
      if ((pressed & B_A) && game) pressFooter(0);
    } else {  // the footer buttons: left / right move along them, A presses one
      if (pressed & B_LEFT) btnIndex = (btnIndex + FOOT_N - 1) % FOOT_N;
      if (pressed & B_RIGHT) btnIndex = (btnIndex + 1) % FOOT_N;
      if (ev.up) { if (roms.empty()) focusArea = "tabs"; else { focusArea = "list"; selected = std::min(selected, (int)roms.size() - 1); } }
      if (ev.down) { if (roms.empty()) focusArea = "tabs"; else { focusArea = "list"; selected = 0; } }  // round: the first game
      if ((pressed & B_A) && (game || btnIndex > 1)) pressFooter(FOOT_IDS[btnIndex]);
    }
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
  } else if (mode == "remap") {
    if (remapStep < NPAD) {  // listening to the player's own controller
      if (!padConnected(remapWho)) { if (pressed & B_B) mode = remapForGame ? "menu" : "settings"; return; }
      const u64 held = padGetButtons(padStates[remapWho]) & MAPPABLE;
      if (remapWaitRelease) { if (!held) { remapWaitRelease = false; remapDeadline = t + REMAP_WAIT_MS; } return; }
      if (held) {  // this input now presses this button (and no other)
        const int b = __builtin_ctzll(held);
        for (int i = 0; i < NPAD; i++) if (remapNew[i] == b) remapNew[i] = -1;
        remapNew[remapStep++] = b; remapWaitRelease = true;
      } else if (t >= remapDeadline) { remapNew[remapStep++] = -1; remapDeadline = t + REMAP_WAIT_MS; }  // skipped: unmapped
      return;
    }
    if (ev.up) remapPick = (remapPick + 2) % 3;
    if (ev.down) remapPick = (remapPick + 1) % 3;
    if (pressed & B_LEFT) remapPick = std::max(0, remapPick - 1);
    if (pressed & B_RIGHT) remapPick = std::min(2, remapPick + 1);
    if (pressed & B_A) {
      if (remapPick == 0) {
        if (remapForGame) {  // this game's own layout (the other player's starts as the Settings one)
          auto it = gamePadMaps.find(running->name);
          if (it == gamePadMaps.end()) { GamePadMap gm; memcpy(gm.m, padMap, sizeof gm.m); it = gamePadMaps.emplace(running->name, gm).first; }
          memcpy(it->second.m[remapWho], remapNew, sizeof remapNew);
        } else memcpy(padMap[remapWho], remapNew, sizeof remapNew);
        saveSettings();
        showToast("Player " + std::to_string(remapWho + 1) + (remapForGame ? " buttons saved for this game" : " buttons saved"), 2500);
        mode = remapForGame ? "menu" : "settings";
      }
      else if (remapPick == 1) openRemap(remapWho, remapForGame);
      else mode = remapForGame ? "menu" : "settings";
    } else if (pressed & B_B) mode = remapForGame ? "menu" : "settings";
  } else if (mode == "settings") {
    {  // L / R (or ZL / ZR): the next / previous category
      int step = ((pressed & (B_R | HidNpadButton_ZR)) ? 1 : 0) - ((pressed & (B_L | HidNpadButton_ZL)) ? 1 : 0);
      if (step) { settingsStep(step); return; }  // (the tabs, then Back)
    }
    int nItems = (int)SETTINGS_ITEMS.size();
    if (ev.up || ev.down || (pressed & B_B)) { resetArmed = false; resetMapArmed = false; }
    if (settingsIndex < 0) {  // on the category tabs, as the game list's: left / right switch straight away, down to the list
      int last = (int)SETTINGS_CATS.size() - 1;
      if (settingsOnBack) {  // the row's Back button: left back to the tabs, A goes back
        if (pressed & B_LEFT) settingsOnBack = false;
        if (pressed & B_A) { settingsOnBack = false; saveSettings(); mode = "browser"; return; }
      } else if ((pressed & B_RIGHT) && settingsCat == last) settingsOnBack = true;
      else {
        int v = (pressed & B_LEFT) ? std::max(0, settingsCat - 1) : (pressed & B_RIGHT) ? std::min(last, settingsCat + 1) : settingsCat;
        if (v != settingsCat) { settingsCat = v; rebuildSettings(); settingsTop = 0; }
      }
      if (ev.down) { settingsIndex = 0; settingsOnBack = false; }
      else if (ev.up) { settingsIndex = nItems; settingsOnBack = false; return; }  // wraps round to the bottom
      if (pressed & B_B) { saveSettings(); mode = "browser"; }
      return;
    }
    if (ev.up) { if (settingsIndex == 0) { settingsIndex = -1; settingsOnBack = false; return; } settingsIndex--; }
    if (ev.down) { if (settingsIndex >= nItems) { settingsIndex = -1; settingsOnBack = false; return; } settingsIndex++; }  // wraps round to the tabs
    if (settingsIndex < nItems) {
      SettingItem it = SETTINGS_ITEMS[settingsIndex];  // copy: the advanced toggle rebuilds the list
      if (!it.set) { if (pressed & B_B) { saveSettings(); mode = "browser"; } return; }  // information only (Version)
      int n = (int)it.choices().size();
      settingsByA = (pressed & B_A) != 0;
      if (pressed & B_LEFT) it.set((it.get() - 1 + n) % n);
      else if (pressed & (B_RIGHT | B_A)) it.set((it.get() + 1) % n);
    } else if (pressed & B_A) { saveSettings(); mode = "browser"; return; }
    if (pressed & B_B) { saveSettings(); mode = "browser"; }
  } else if (mode == "menu") {
    bool lr = menuCombo(bits);
    if (lr && !comboLatch) { comboLatch = true; closeMenu(); return; }
    if (!lr) comboLatch = false;
    {  // L / R: the next / previous category -- on release, and only for a lone tap, so starting
       // the close combo never switches category first
      static int tapStep = 0; static bool tapSpoilt = false;
      if (pressed & (B_L | B_R)) { tapStep = (pressed & B_R) ? 1 : -1; tapSpoilt = (pressed & B_L) && (pressed & B_R); }
      if (tapStep && ((bits & (MENU_COMBO & ~(u64)(B_L | B_R))) || ((bits & B_L) && (bits & B_R)))) tapSpoilt = true;
      if (tapStep && !(bits & (B_L | B_R))) {
        const int step = tapSpoilt ? 0 : tapStep; tapStep = 0;
        if (step && menuHasTabs) { menuStep(step); return; }  // (the tabs, then Back)
      }
    }
    int n = (int)menuItems.size();
    if (menuIndex < 0) {  // on the category tabs, as the game list's: left / right switch straight away, down to the list
      int last = (int)MENU_CATS.size() - 1;
      if (menuOnBack) {  // the row's Back button: left back to the tabs, A closes the menu
        if (pressed & B_LEFT) menuOnBack = false;
        if (pressed & B_A) { menuOnBack = false; closeMenu(); return; }
      } else if ((pressed & B_RIGHT) && menuCat == last) menuOnBack = true;
      else {
        int v = (pressed & B_LEFT) ? std::max(0, menuCat - 1) : (pressed & B_RIGHT) ? std::min(last, menuCat + 1) : menuCat;
        if (v != menuCat) { menuCat = v; openMenu(); menuIndex = -1; return; }
      }
      if (ev.down && n > 0) { menuIndex = 0; menuOnBack = false; }
      else if (ev.up && n > 0) { menuIndex = n - 1; menuOnBack = false; return; }  // wraps round to the bottom
      if ((pressed & B_B) && mode == "menu") closeMenu();
      return;
    }
    if (menuHasTabs) {  // as the game list: up from the first row reaches the tabs, down from the last wraps to them
      if (ev.up) { if (menuIndex == 0) { menuIndex = -1; menuOnBack = false; return; } menuIndex--; }
      if (ev.down) { if (menuIndex >= n - 1) { menuIndex = -1; menuOnBack = false; return; } menuIndex++; }
    } else {
      if (ev.up) menuIndex = (menuIndex - 1 + n) % n;
      if (ev.down) menuIndex = (menuIndex + 1) % n;
    }
    MenuItem item = menuItems[menuIndex];  // copy: actions may replace the list
    if (item.option()) {
      if (pressed & B_LEFT) cycleMenuOption(-1);
      if (pressed & (B_RIGHT | B_A)) cycleMenuOption(1);
    } else if (pressed & B_A) item.action();
    if ((pressed & B_B) && mode == "menu") {
      if (menuHasTabs) closeMenu();
      else openMenu();  // a sub-menu (cheats): back to the main menu
    }
  } else if (mode == "ggedit") {
    handleCodeEditor(ev);
  } else if (mode == "message") {
    if (pressed & (B_A | B_B)) {
      mode = "browser";
      auto then = (messageAsk && (pressed & B_B) && !(pressed & B_A)) ? messageNo : messageThen;  // a question: B is its second answer
      messageThen = nullptr; messageNo = nullptr; messageAsk = false;
      if (then) then();
    }
  }
}

// The first time there are games without box art (and the console is online): offer to download it. Asked once.
static void maybeAskArt() {
  if (artAsked || allRoms.empty() || boxart::status().running) return;
  const boxart::System sys = artSystem();
  bool missing = false;
  for (const Rom& r : allRoms) if (!hasInfoFile(r.name)) { missing = true; break; }  // (from the folder's listing)
  if (!missing || !internetConnected()) return;
  artAsked = true; saveSettings();
  showQuestion("Some of your games have no box art or description yet.\n\nDownload them now from ScreenScraper? It needs an internet connection and takes a few seconds a game. You can also do it later in Settings > App.",
               "Download", "Not now", [] { startBoxArt(false); }, nullptr);
}

int main(int argc, char** argv) {
  romfsInit();
  mkdir("sdmc:/switch", 0777); mkdir(DATA_DIR, 0777);
  // First launch only (no settings file yet): create the default ROM folder.
  // Later launches leave the SD card alone, so people who keep ROMs elsewhere
  // (and removed roms/nes) don't get it back.
  bool firstLaunch = !exists(SETTINGS());
  if (firstLaunch) { mkdir("sdmc:/roms", 0777); mkdir(DEFAULT_ROM_DIR, 0777); saveSettings(); }
  { string t; Json v; if (readText(SETTINGS(), t) && Json::parse(t, v) && v.type == Json::OBJ) if (const Json* d = v.get("debugLog")) debugLog = d->isBool() && d->b; }
  if (debugLog) openLog();  // otherwise no log at all (Settings -> App -> Debug logging)
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
  padStates[0] = &pad1; padStates[1] = &pad2;
  hidInitializeTouchScreen();

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
  boxart::cleanOldArt(artSystem());  // (once: the art of earlier builds, from other sources)
  loadDateFormat();
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
    bool gameView = running && (mode == "running" || mode == "menu" || mode == "ggedit" || mode == "remap");
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
    singleJoyConConnected = false;
    for (PadState* p : {&pad1, &pad2})
      if (padIsConnected(p)) { const u32 st = padGetStyleSet(p); if (st == HidNpadStyleTag_NpadJoyLeft || st == HidNpadStyleTag_NpadJoyRight) singleJoyConConnected = true; }
    u64 bits = padGetButtons(&pad1), bits2 = padGetButtons(&pad2);
    touchUpdate();
    frameDrawn = false;
    if (mode == "running") runFrame(bits, bits2, false);
    else {
      if (mode == "menu" && running && !pauseInMenu) runFrame(0, 0, true);
      handleUi(uiBits(&pad1, bits) | uiBits(&pad2, bits2));  // both players' controllers drive the menus
    }
    double t = nowMs();
    hits.clear();  // drawing below registers this frame's touch targets
    {  // box art: reload as it arrives; say how it went when the run ends
      static bool artWasRunning = false; static int artSeen = 0;
      auto& b = boxart::status();
      if (b.running && b.found != artSeen) { artSeen = b.found; clearArtCache(); staleCovers(false); }
      if (mode == "browser" && artCheckPending) { artCheckPending = false; maybeAskArt(); }
      if (artWasRunning && !b.running) { clearArtCache(); staleCovers(true); resetTitles(); orderRoms(); artSeen = 0; showToast(b.message, 4000); }
      artWasRunning = b.running;
      if (titleIndexDirty && !b.running) saveTitleIndex();  // (new titles read: kept for the next start)
    }
    if (toastUntil && t > toastUntil) toastUntil = 0;
    if (hintUntil && t > hintUntil) hintUntil = 0;
    if (gameView != (running && (mode == "running" || mode == "menu" || mode == "ggedit" || mode == "remap"))) gfx::beginFrame(running ? gfx::Color{0, 0, 0, 1} : col(C_BG));
    if (running && (mode == "running" || mode == "menu" || mode == "ggedit" || mode == "remap")) {
      drawGame(mode != "running");
      if (mode == "menu") drawMenu();
      if (mode == "remap") { if (dimInMenu) gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f}); drawRemap(); }
      if (mode == "ggedit") drawCodeEditor();
    } else if (mode == "settings") drawSettings();
    else if (mode == "remap") drawRemap();
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
  coverLoaderStop();  // the cover decoding thread ends too
  boxart::stop();  // a download in progress: cancelled, its thread ended before the program is (its game is redone next time)
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
