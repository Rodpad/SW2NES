// Box art scraper (see boxart.h): ScreenScraper (screenscraper.fr). Runs on its own thread; HTTPS through
// libcurl (mbedTLS) with the Mozilla CA bundle from romfs, so the server is verified.
#include "boxart.h"

#include <switch.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <zlib.h>

#include "json.h"
#if __has_include("ss_dev.h")
#include "ss_dev.h"  // the ScreenScraper developer account (not in the repository: see boxart.h)
#define SW2_HAVE_SS 1
#endif

#include <algorithm>
#include <map>

namespace boxart {

static Status st;
Status& status() { return st; }
static void (*logFn)(const char*) = nullptr;
void setLog(void (*fn)(const char*)) { logFn = fn; }
static void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf(const char* fmt, ...) {
  if (!logFn) return;
  char b[600]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  logFn(b);
}

static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string stem(const std::string& name) { const size_t d = name.rfind('.'); return d == std::string::npos ? name : name.substr(0, d); }
static bool exists(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0; }
std::string artPath(const System& sys, const std::string& gameName) { return sys.dataDir + "/art/boxart/" + stem(gameName) + ".png"; }
std::string backPath(const System& sys, const std::string& gameName) { return sys.dataDir + "/art/boxart/" + stem(gameName) + ".back"; }
std::string spinePath(const System& sys, const std::string& gameName) { return sys.dataDir + "/art/boxart/" + stem(gameName) + ".spine"; }
std::string infoPath(const System& sys, const std::string& gameName) { return sys.dataDir + "/art/info/" + stem(gameName) + ".json"; }
static std::string missPath(const System& sys, const std::string& gameName) { return sys.dataDir + "/art/boxart/" + stem(gameName) + ".none"; }
bool artMissing(const System& sys, const std::string& gameName) { return exists(missPath(sys, gameName)); }

std::string region(const std::string& gameName) {
  const std::string n = lower(gameName);
  auto has = [&](const char* t) { return n.find(t) != std::string::npos; };
  // No-Intro: (USA) (Europe) (Japan) (World), countries; GoodTools: (U) (E) (J) (UE) (JU) ...
  if (has("(japan") || has("(j)") || has("(jp)")) return "jp";
  if (has("(usa") || has("(u)") || has("(ue)") || has("(ju)") || has("(world") || has("(w)") || has("(canada")) return "us";
  if (has("(europe") || has("(e)") || has("(uk)") || has("(germany") || has("(g)") || has("(france") || has("(f)") || has("(spain") ||
      has("(s)") || has("(italy") || has("(i)") || has("(australia") || has("(a)") || has("(sweden") || has("(netherlands") || has("(eu)"))
    return "eu";
  return "us";
}

std::string tvSystem(const std::string& gameName) {
  const std::string n = lower(gameName);
  auto has = [&](const char* t) { return n.find(t) != std::string::npos; };
  if (has("(world") || has("(w)")) return "World";
  if (has("(japan") || has("(j)") || has("(jp)") || has("(asia") || has("(korea") || has("(taiwan") || has("(hong kong")) return "NTSC-J";
  if (has("(usa") || has("(u)") || has("(ue)") || has("(ju)") || has("(canada") || has("(brazil")) return "NTSC";
  if (has("(europe") || has("(e)") || has("(uk)") || has("(germany") || has("(g)") || has("(france") || has("(f)") || has("(spain") ||
      has("(s)") || has("(italy") || has("(i)") || has("(australia") || has("(a)") || has("(sweden") || has("(netherlands") || has("(eu)") ||
      has("(scandinavia") || has("(denmark") || has("(norway") || has("(finland") || has("(portugal") || has("(russia") || has("(poland"))
    return "PAL";
  return "";
}
std::string tvSystemOfSerial(const std::string& serial) {
  if (serial.size() < 4) return "";
  switch (toupper((unsigned char)serial[2])) {
    case 'U': return "NTSC";                        // SLUS, SCUS
    case 'E': return "PAL";                         // SLES, SCES, SCED
    case 'P': case 'J': case 'A': case 'K': return "NTSC-J";  // SLPS, SLPM, SCPS, SCAJ (Asia), SLKA (Korea)
  }
  return "";
}

// ---- HTTPS ----
static size_t toVector(char* p, size_t sz, size_t n, void* ud) { auto* v = (std::vector<uint8_t>*)ud; v->insert(v->end(), p, p + sz * n); return sz * n; }
// One handle for a whole download (the scraper's thread only): its connections to the servers stay open and
// are reused, so each request is not a new TCP connection, TLS handshake and CA bundle load (slow on the console:
// about 7 s a game with a fresh handle per request, against about 2 s of transfer).
static std::atomic<bool> s_cancel{false};  // stop(): the app is exiting
static int abortIfCancelled(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return s_cancel ? 1 : 0; }  // (non-zero ends the transfer)
static CURL* s_curl = nullptr;
static void closeConnections() { if (s_curl) { curl_easy_cleanup(s_curl); s_curl = nullptr; } }
static long get(const std::string& url, std::vector<uint8_t>& out) {
  out.clear();
  if (!s_curl) {
    s_curl = curl_easy_init();
    if (!s_curl) return -1;
    curl_easy_setopt(s_curl, CURLOPT_CAINFO, "romfs:/net/cacert.pem");
    curl_easy_setopt(s_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION, toVector);
    curl_easy_setopt(s_curl, CURLOPT_USERAGENT, "SW2-emulator-boxart/1.0");
    curl_easy_setopt(s_curl, CURLOPT_ACCEPT_ENCODING, "");  // compressed when the server can (ScreenScraper's game details: 843 KB -> 176 KB with gzip)
    curl_easy_setopt(s_curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(s_curl, CURLOPT_TIMEOUT, 180L);  // (a game's details are ~1 MB, and anonymous users get 128 KB/s)
    curl_easy_setopt(s_curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(s_curl, CURLOPT_XFERINFOFUNCTION, abortIfCancelled);
    curl_easy_setopt(s_curl, CURLOPT_NOPROGRESS, 0L);
  }
  curl_easy_setopt(s_curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(s_curl, CURLOPT_WRITEDATA, &out);
  long code = -1;
  const CURLcode r = curl_easy_perform(s_curl);
  if (r == CURLE_OK) curl_easy_getinfo(s_curl, CURLINFO_RESPONSE_CODE, &code);
  else { logf("boxart: %s: %s", url.substr(0, url.find('?')).c_str(), curl_easy_strerror(r)); closeConnections(); }  // (never the query: it holds the developer account)
  return code;
}
static std::string encode(const std::string& s) {  // a URL path segment
  static const char* HEX = "0123456789ABCDEF";
  std::string o;
  for (unsigned char ch : s) {
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += (char)ch;
    else { o += '%'; o += HEX[ch >> 4]; o += HEX[ch & 15]; }
  }
  return o;
}
static bool writeFile(const std::string& path, const std::vector<uint8_t>& d) {
  const std::string tmp = path + ".part";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
  fclose(f);
  if (!ok) { remove(tmp.c_str()); return false; }
  remove(path.c_str());
  return rename(tmp.c_str(), path.c_str()) == 0;
}
static bool readAll(const std::string& path, std::string& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? n : 0);
  const bool ok = n <= 0 || fread(&out[0], 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}

// ---- a disc's serial (ISO 9660: SYSTEM.CNF's BOOT2 line) ----
std::string discSerial(const std::string& path0) {
  std::string path = path0;
  const std::string ext = lower(path.substr(path.rfind('.') == std::string::npos ? path.size() : path.rfind('.')));
  if (ext == ".m3u" || ext == ".cue") {  // the first disc / the .bin the .cue names
    std::string text; if (!readAll(path, text)) return "";
    std::string file;
    if (ext == ".m3u") { file = text.substr(0, text.find_first_of("\r\n")); }
    else { const size_t f = text.find("FILE \""); if (f == std::string::npos) return ""; const size_t e = text.find('"', f + 6); file = text.substr(f + 6, e - f - 6); }
    if (file.empty()) return "";
    const size_t sl = path.rfind('/');
    path = (sl == std::string::npos ? std::string() : path.substr(0, sl + 1)) + file;
    if (ext == ".m3u") return discSerial(path);
  } else if (ext != ".iso" && ext != ".bin" && ext != ".img") return "";
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return "";
  // sector size and where a sector's 2048 data bytes start: plain ISO, or raw (mode 2 form 1 / mode 1)
  long secSize = 0, dataOff = 0;
  auto at = [&](long sec, long size, long off, unsigned char* buf, size_t n) { return fseek(f, sec * size + off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n; };
  unsigned char pvd[2048];
  static const long kinds[3][2] = {{2048, 0}, {2352, 24}, {2352, 16}};
  for (auto& k : kinds) if (at(16, k[0], k[1], pvd, 2048) && pvd[0] == 1 && memcmp(pvd + 1, "CD001", 5) == 0) { secSize = k[0]; dataOff = k[1]; break; }
  std::string serial;
  if (secSize) {
    auto le32 = [](const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; };
    const uint32_t rootLba = le32(pvd + 156 + 2), rootLen = le32(pvd + 156 + 10);
    std::vector<unsigned char> dir;
    for (uint32_t s = 0; s * 2048 < rootLen && s < 32; s++) { unsigned char b[2048]; if (!at(rootLba + s, secSize, dataOff, b, 2048)) break; dir.insert(dir.end(), b, b + 2048); }
    for (size_t p = 0; p + 33 < dir.size();) {
      const unsigned len = dir[p];
      if (!len) { p = (p / 2048 + 1) * 2048; continue; }  // to the next sector
      const unsigned nl = dir[p + 32];
      const std::string name((const char*)&dir[p + 33], std::min<size_t>(nl, dir.size() - p - 33));
      if (lower(name).compare(0, 10, "system.cnf") == 0) {
        const uint32_t lba = le32(&dir[p + 2]), size = std::min<uint32_t>(le32(&dir[p + 10]), 2048);
        unsigned char b[2048];
        if (at(lba, secSize, dataOff, b, 2048)) {
          const std::string cnf((const char*)b, size);
          size_t k = cnf.find("BOOT2"); if (k == std::string::npos) k = cnf.find("BOOT");
          if (k != std::string::npos) {
            size_t a = cnf.find_first_of("\\:", k);  // cdrom0:\SLUS_210.08;1
            while (a != std::string::npos && (cnf[a] == '\\' || cnf[a] == ':')) a++;
            const size_t e = cnf.find_first_of(";\r\n", a);
            std::string id = a == std::string::npos ? "" : cnf.substr(a, e == std::string::npos ? std::string::npos : e - a);
            const size_t sl2 = id.find_last_of("\\/"); if (sl2 != std::string::npos) id = id.substr(sl2 + 1);
            for (char ch : id) { if (ch == '_') serial += '-'; else if (ch != '.') serial += (char)toupper((unsigned char)ch); }
          }
        }
        break;
      }
      p += len;
    }
  }
  fclose(f);
  return serial;
}

// ---- ScreenScraper ----
// The developer account, kept out of the repository (ss_dev.h, made by tools/make_ss_dev.sh) and lightly
// obscured in the binary. Players need no account: requests go as the anonymous user.
static std::string ssDev(const unsigned char* d, size_t n) {
  std::string s;
#ifdef SW2_HAVE_SS
  for (size_t i = 0; i < n; i++) s += (char)(d[i] ^ (unsigned char)(0x5A + i * 7));
#else
  (void)d; (void)n;
#endif
  return s;
}
static std::string ssAuth(const System& sys) {
#ifdef SW2_HAVE_SS
  return "devid=" + encode(ssDev(SS_DEVID, sizeof SS_DEVID)) + "&devpassword=" + encode(ssDev(SS_DEVPASSWORD, sizeof SS_DEVPASSWORD)) +
         "&softname=" + encode(sys.softname) + "&ssid=&sspassword=&output=json";
#else
  (void)sys;
  return "";
#endif
}
static std::string str(const Json* j, const char* k) { const Json* v = j ? j->get(k) : nullptr; return v && v->isStr() ? v->s : std::string(); }
// ScreenScraper's region codes in the order to prefer for a game of region `reg` (us / eu / jp).
static std::vector<std::string> regionOrder(const std::string& reg) {
  if (reg == "jp") return {"jp", "wor", "ss", "us", "eu", "asi", "kr", "uk"};
  if (reg == "eu") return {"eu", "uk", "wor", "ss", "fr", "de", "sp", "it", "nl", "au", "us", "jp"};
  return {"us", "wor", "ss", "cus", "eu", "uk", "au", "jp"};
}
// The text in a list of {region|langue, text} entries, by preference; else the first.
static std::string pickText(const Json* arr, const char* key, const std::vector<std::string>& order) {
  if (!arr || !arr->isArr() || arr->a.empty()) return "";
  for (const std::string& r : order) for (const Json& e : arr->a) if (str(&e, key) == r) return str(&e, "text");
  return str(&arr->a[0], "text");
}
static std::string tvOfRegion(const std::string& r) {
  if (r == "jp" || r == "asi" || r == "kr" || r == "tw") return "NTSC-J";
  if (r == "us" || r == "cus" || r == "br") return "NTSC";
  if (r == "wor") return "World";
  if (r.empty() || r == "ss") return "";
  return "PAL";
}
static std::string jsonString(const std::string& s) {  // a JSON string value
  std::string o = "\"";
  for (unsigned char ch : s) {
    if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
    else if (ch == '\n') o += "\\n";
    else if (ch < 0x20) o += ' ';
    else o += (char)ch;
  }
  return o + "\"";
}
// One media of a type, by region preference (the game's own region first); "" when none.
static const Json* pickMedia(const Json* medias, const char* type, const std::vector<std::string>& order, const std::string& want = "") {
  if (!medias || !medias->isArr()) return nullptr;
  const Json* any = nullptr;
  if (!want.empty()) for (const Json& m : medias->a) if (str(&m, "type") == type && str(&m, "region") == want) return &m;
  for (const std::string& r : order) for (const Json& m : medias->a) if (str(&m, "type") == type && str(&m, "region") == r) return &m;
  for (const Json& m : medias->a) if (str(&m, "type") == type) { any = &m; break; }
  return any;
}
static bool fetchMedia(const Json* m, const std::string& path) {
  if (!m) return false;
  std::string url = str(m, "url");
  if (url.empty()) return false;
  url += "&maxwidth=640&maxheight=640&outputformat=jpg";  // smaller to fetch (anonymous users get 128 KB/s); still sharper than the box is drawn
  std::vector<uint8_t> d;
  const long code = get(url, d);
  const bool image = d.size() > 3 && ((d[0] == 0xFF && d[1] == 0xD8) || (d[1] == 'P' && d[2] == 'N' && d[3] == 'G'));
  return code == 200 && image && writeFile(path, d);
}

// Deletes the art folder of earlier builds (LaunchBox / libretro images and details, their databases)
// once, so only ScreenScraper's remains; marked by art/.screenscraper.
static void removeTree(const std::string& dir) {
  DIR* d = opendir(dir.c_str());
  if (!d) { remove(dir.c_str()); return; }
  std::vector<std::string> names;
  while (dirent* e = readdir(d)) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names.push_back(e->d_name);
  closedir(d);
  for (const std::string& n : names) {
    const std::string p = dir + "/" + n;
    struct stat s;
    if (stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode)) removeTree(p); else remove(p.c_str());
  }
  rmdir(dir.c_str());
}
void cleanOldArt(const System& sys) {
  const std::string art = sys.dataDir + "/art", marker = art + "/.screenscraper";
  if (exists(marker)) return;
  if (exists(art)) { logf("boxart: removing the art of earlier builds (other sources)"); removeTree(art); }
  mkdir(art.c_str(), 0777);
  FILE* f = fopen(marker.c_str(), "wb");
  if (f) { fputs("ScreenScraper\n", f); fclose(f); }
}

static void writeInfo(const System& sys, const std::string& gameName, const std::string& title, const std::string& developer, const std::string& publisher,
                      const std::string& year, const std::string& genre, const std::string& bio, const std::string& tv) {
  const std::string j = "{\"title\":" + jsonString(title) + ",\"region\":" + jsonString(tv) + ",\"developer\":" + jsonString(developer) +
                        ",\"publisher\":" + jsonString(publisher) + ",\"year\":" + jsonString(year) + ",\"genre\":" + jsonString(genre) +
                        ",\"source\":\"ScreenScraper\",\"bio\":" + jsonString(bio) + "}";
  std::vector<uint8_t> d(j.begin(), j.end());
  writeFile(infoPath(sys, gameName), d);
}

// What a lookup came to: found, not in ScreenScraper, or stop (quota, closed API, no access).
enum class Look { Found, NotFound, Stop };
static Look lookUp(const System& sys, const Game& g, const std::string& query, Json& out, std::string& why) {
  std::vector<uint8_t> d;
  const long code = get("https://api.screenscraper.fr/api2/jeuInfos.php?" + ssAuth(sys) + "&systemeid=" + std::to_string(sys.ssSystemId) + query, d);
  const std::string text(d.begin(), d.end());
  if (code == 200 && Json::parse(text, out) && out.get("response")) return Look::Found;
  // ScreenScraper answers errors in plain text (French), with these codes:
  if (code == 404) return Look::NotFound;  // "Erreur : Rom/Iso/Dossier non trouvée !"
  why = code == 401 || code == 403 ? "ScreenScraper refused this app (developer access)" :
        code == 423 ? "ScreenScraper's API is closed for now" :
        code == 426 ? "ScreenScraper no longer accepts this app version" :
        code == 429 || code == 430 || code == 431 ? "ScreenScraper's daily limit for anonymous users is reached; try again tomorrow" :
        code < 0 ? "no connection to ScreenScraper" : "ScreenScraper error " + std::to_string(code);
  logf("boxart: %s: HTTP %ld: %s", g.name.c_str(), code, text.substr(0, 120).c_str());
  return code >= 500 && code < 600 ? Look::NotFound : Look::Stop;  // (a server error: skip this game)
}

static void run(System sys, std::vector<Game> games, bool again, std::function<bool(const Game&, std::vector<uint8_t>&)> readFile, int limit) {
  logf("boxart: %d games%s%s (ScreenScraper)", (int)games.size(), again ? ", all again" : "", limit ? (", first " + std::to_string(limit)).c_str() : "");
  cleanOldArt(sys);
  mkdir((sys.dataDir + "/art/boxart").c_str(), 0777);
  mkdir((sys.dataDir + "/art/info").c_str(), 0777);
  for (const char* sub : {"/art/boxart", "/art/info"}) {  // files a crash or an exit left half written (never read)
    const std::string dir = sys.dataDir + sub;
    if (DIR* d = opendir(dir.c_str())) {
      std::vector<std::string> parts;
      while (dirent* e = readdir(d)) { const size_t n = strlen(e->d_name); if (n > 5 && !strcmp(e->d_name + n - 5, ".part")) parts.push_back(dir + "/" + e->d_name); }
      closedir(d);
      for (const std::string& f : parts) remove(f.c_str());
    }
  }
  if (ssAuth(sys).empty()) {
    st.current[0] = 0; st.message = "Box art: this build has no ScreenScraper access"; st.running = false;
    logf("boxart: built without ss_dev.h: no ScreenScraper access");
    return;
  }
  // A game is complete when its details file exists: it is written last, so a game interrupted part way (an exit or a
  // crash after its front image) is looked up again in full, back and side included.
  auto needArt = [&](const Game& g) { return again || !exists(infoPath(sys, g.name)) || (!exists(artPath(sys, g.name)) && !exists(missPath(sys, g.name))); };
  auto needInfo = [&](const Game& g) { return again || !exists(infoPath(sys, g.name)); };
  if (limit) {  // only the first `limit` games that need looking up
    std::vector<Game> want;
    for (const Game& g : games) if (needArt(g) || needInfo(g)) { want.push_back(g); if ((int)want.size() >= limit) break; }
    games = want;
  }
  st.total = (int)games.size();
  int found = 0, missing = 0;
  std::string stopWhy;
  for (const Game& g : games) {
    if (s_cancel) break;
    const bool wantArt = needArt(g), wantInfo = needInfo(g);
    if (!wantArt && !wantInfo) { st.done++; continue; }
    snprintf(st.current, sizeof st.current, "%s", stem(g.name).c_str());
    const u64 t0 = armGetSystemTick();
    // The query: a disc's serial (PS2), or the contents' CRC32; with the file's name and size either way.
    std::string query, ser;
    struct stat fs;
    const long long size = stat(g.path.c_str(), &fs) == 0 ? (long long)fs.st_size : 0;
    if (sys.serialOf) {
      ser = sys.serialOf(g);
      query = "&romtype=iso" + (ser.empty() ? std::string() : "&serialnum=" + encode(ser));
    } else {
      query = "&romtype=rom";
      std::vector<uint8_t> file;
      if (readFile && readFile(g, file) && !file.empty()) {
        size_t off = 0, len = file.size();
        if (sys.crcRange) sys.crcRange(file, off, len);
        if (off + len <= file.size()) {
          char crc[16]; snprintf(crc, sizeof crc, "%08X", (unsigned)crc32(0L, file.data() + off, (uInt)len));
          query += std::string("&crc=") + crc;
        }
      }
    }
    query += "&romnom=" + encode(g.name) + "&romtaille=" + std::to_string(size);
    Json resp; std::string why;
    const Look look = lookUp(sys, g, query, resp, why);
    if (look == Look::Stop) { stopWhy = why; break; }
    const Json* jeu = look == Look::Found ? resp.get("response")->get("jeu") : nullptr;
    // the game's region: its serial (PS2), the ROM ScreenScraper matched, or the file name
    std::string tv = tvSystemOfSerial(ser);
    std::string reg = tv == "NTSC" ? "us" : tv == "PAL" ? "eu" : tv == "NTSC-J" ? "jp" : "";
    if (jeu && reg.empty()) {
      const Json* regs = jeu->get("rom") ? jeu->get("rom")->get("romregions") : nullptr;
      std::string r = regs && regs->isStr() ? regs->s : "";
      if (r.find(',') != std::string::npos) r = r.substr(0, r.find(','));
      if (tv.empty()) tv = tvOfRegion(r);
      reg = r == "jp" || r == "asi" || r == "kr" ? "jp" : r == "us" || r == "cus" || r == "wor" || r == "ss" || r.empty() ? "" : "eu";
    }
    if (tv.empty()) tv = tvSystem(g.name);
    if (reg.empty()) reg = region(g.name);
    const std::vector<std::string> order = regionOrder(reg);
    if (wantArt) {
      const Json* medias = jeu ? jeu->get("medias") : nullptr;
      const Json* front = pickMedia(medias, "box-2D", order);
      const std::string fr = front ? str(front, "region") : "";
      bool ok = fetchMedia(front, artPath(sys, g.name));
      // the back and the side from the front's region when there are (the same box)
      const bool back = ok && fetchMedia(pickMedia(medias, "box-2D-back", order, fr), backPath(sys, g.name));
      const bool side = ok && fetchMedia(pickMedia(medias, "box-2D-side", order, fr), spinePath(sys, g.name));
      if (!back) remove(backPath(sys, g.name).c_str());
      if (!side) remove(spinePath(sys, g.name).c_str());
      logf("boxart: %s -> %s%s%s%s", g.name.c_str(), !jeu ? "not in ScreenScraper" : ok ? "front" : "no box art", back ? ", back" : "", side ? ", side" : "",
           fr.empty() ? "" : (" (" + fr + ")").c_str());
      if (ok) { found++; remove(missPath(sys, g.name).c_str()); }
      else { missing++; std::vector<uint8_t> none; writeFile(missPath(sys, g.name), none); }
    }
    if (s_cancel) break;  // (no details file: the game is looked up again in full next time)
    if (wantInfo) {
      std::string title = jeu ? pickText(jeu->get("noms"), "region", order) : "", year, genre, bio;
      if (jeu) {
        const std::string date = pickText(jeu->get("dates"), "region", order);
        year = date.substr(0, 4);
        if (const Json* gs = jeu->get("genres")) if (gs->isArr()) {
          const Json* best = nullptr;
          for (const Json& gg : gs->a) if (str(&gg, "principale") == "1") { best = &gg; break; }
          if (!best && !gs->a.empty()) best = &gs->a[0];
          if (best) genre = pickText(best->get("noms"), "langue", {"en"});
        }
        bio = pickText(jeu->get("synopsis"), "langue", {"en"});
      }
      if (title.empty()) title = stem(g.name);
      writeInfo(sys, g.name, title, jeu ? str(jeu->get("developpeur"), "text") : "", jeu ? str(jeu->get("editeur"), "text") : "", year, genre, bio, tv);
    }
    logf("boxart: %s: %.1f s", g.name.c_str(), armTicksToNs(armGetSystemTick() - t0) / 1e9);
    st.found = found; st.missing = missing; st.done++;
  }
  closeConnections();
  st.current[0] = 0;
  st.message = !stopWhy.empty() ? "Box art: " + stopWhy :
               std::to_string(found) + " box art found" + (missing ? ", " + std::to_string(missing) + " not found" : "");
  st.running = false;
}

struct Job { System sys; std::vector<Game> games; bool again; std::function<bool(const Game&, std::vector<uint8_t>&)> readFile; int limit; };
static void threadMain(void* arg) {
  Job* j = (Job*)arg;
  run(j->sys, std::move(j->games), j->again, std::move(j->readFile), j->limit);
  delete j;
}
static bool netUp() {
  static int state = 0;  // 0 not tried, 1 up, -1 failed
  if (state) return state > 0;
  SocketInitConfig c = *socketGetDefaultInitConfig();
  c.bsd_service_type = BsdServiceType_Auto;
  Result rc = socketInitialize(&c);
  if (R_FAILED(rc)) rc = socketInitializeDefault();
  logf("boxart: sockets 0x%x", rc);
  if (R_FAILED(rc)) { state = -1; return false; }
  const CURLcode cr = curl_global_init(CURL_GLOBAL_DEFAULT);
  logf("boxart: curl %s", curl_version());
  state = cr == CURLE_OK ? 1 : -1;
  return state > 0;
}
static Thread s_thread; static bool s_made = false;
// The app's exit: a download still running is cancelled and its thread waited for (it must not outlive the program:
// hbloader runs the next one in this same process). Its game is left without a details file, so it is redone.
void stop() {
  if (!s_made) return;
  s_cancel = true;  // ends a transfer in progress (abortIfCancelled) and the loop
  threadWaitForExit(&s_thread); threadClose(&s_thread); s_made = false;
  s_cancel = false;
}
bool start(const System& sys, std::vector<Game> games, bool again, std::function<bool(const Game&, std::vector<uint8_t>&)> readFile, int limit) {
  if (st.running) return false;
  if (!netUp()) { st.message = "Box art: the network could not be started"; return false; }
  st.total = (int)games.size(); st.done = 0; st.found = 0; st.missing = 0; st.phase = 0; st.message.clear();
  st.running = true;
  if (s_made) { threadWaitForExit(&s_thread); threadClose(&s_thread); s_made = false; }
  Job* j = new Job{sys, std::move(games), again, std::move(readFile), limit};
  Result rc = threadCreate(&s_thread, threadMain, j, nullptr, 1024 * 1024, 0x2C, -2);  // 1 MB of stack
  if (R_SUCCEEDED(rc)) rc = threadStart(&s_thread);
  logf("boxart: thread 0x%x", rc);
  if (R_FAILED(rc)) { delete j; st.running = false; st.message = "Box art: could not start"; return false; }
  s_made = true;
  return true;
}

}  // namespace boxart
