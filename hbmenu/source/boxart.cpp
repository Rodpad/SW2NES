// Box art scraper (see boxart.h). Runs on its own thread; HTTPS through libcurl (mbedTLS) with the
// Mozilla CA bundle from romfs, so the server is verified.
#include "boxart.h"

#include <switch.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <minizip/unzip.h>

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
std::string snapPath(const System& sys, const std::string& gameName) { return sys.dataDir + "/art/boxart/" + stem(gameName) + ".snap"; }
static std::string snapMissPath(const System& sys, const std::string& gameName) { return sys.dataDir + "/art/boxart/" + stem(gameName) + ".snapnone"; }
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
static long get(const std::string& url, std::vector<uint8_t>& out) {
  out.clear();
  CURL* c = curl_easy_init();
  if (!c) return -1;
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/net/cacert.pem");
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toVector);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "SW2-emulator-boxart/1.0");
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
  long code = -1;
  const CURLcode r = curl_easy_perform(c);
  if (r == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  else logf("boxart: %s: %s", url.c_str(), curl_easy_strerror(r));
  curl_easy_cleanup(c);
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

// ---- the No-Intro database: CRC32 -> name, and titles ----
struct Db {
  std::map<uint32_t, std::string> byCrc;
  std::map<std::string, std::string> bySerial;  // disc serial ("SLUS-21008") -> name
  std::map<std::string, uint32_t> crcOf;  // official name -> CRC32 (for the metadata)
  std::vector<std::string> names;
};
static std::string titleKey(const std::string& name) {  // "Tick, The (USA) [!]" -> "tick the"
  std::string t = name.substr(0, std::min(name.find(" ("), name.find(" [")));
  std::string o;
  for (char ch : lower(t)) { if (isalnum((unsigned char)ch)) o += ch; else if (!o.empty() && o.back() != ' ') o += ' '; }
  while (!o.empty() && o.back() == ' ') o.pop_back();
  return o;
}
static bool loadDb(const System& sys, Db& db) {
  const std::string path = sys.dataDir + "/art/" + sys.dbName + ".dat";
  std::string text;
  if (!readAll(path, text) || text.size() < 1000) {
    std::vector<uint8_t> d;
    const std::string url = std::string("https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/") + sys.dbFolder + "/" + encode(sys.dbName) + ".dat";
    if (get(url, d) != 200 || d.size() < 1000 || !writeFile(path, d)) return false;
    text.assign(d.begin(), d.end());
  }
  // game ( name "X" ... rom ( name "X.ext" size N crc HHHHHHHH ... ) )
  size_t pos = 0;
  std::string game;
  while ((pos = text.find('\n', pos)) != std::string::npos) {
    ++pos;
    size_t e = text.find('\n', pos); if (e == std::string::npos) e = text.size();
    const std::string line = text.substr(pos, e - pos);
    size_t q;
    if (line.compare(0, 7, "\tname \"") == 0 && (q = line.rfind('"')) > 7) { game = line.substr(7, q - 7); db.names.push_back(game); }
    else if (line.compare(0, 9, "\tserial \"") == 0 && (q = line.rfind('"')) > 9 && !game.empty()) {
      std::string v = line.substr(9, q - 9);  // may list several: "SLUS-20312, SLUS-20312GH"
      size_t a = 0;
      while (a < v.size()) { size_t c = v.find(',', a); if (c == std::string::npos) c = v.size(); std::string one = v.substr(a, c - a); while (!one.empty() && one[0] == ' ') one.erase(0, 1); if (!one.empty()) db.bySerial.emplace(one, game); a = c + 1; }
    }
    else if ((q = line.find(" crc ")) != std::string::npos && !game.empty()) { const uint32_t crc = (uint32_t)strtoul(line.c_str() + q + 5, nullptr, 16); db.byCrc[crc] = game; db.crcOf.emplace(game, crc); }
  }
  return !db.byCrc.empty() || !db.bySerial.empty();
}

// The thumbnail file name for a game name: libretro replaces &*/:`<>?\|" with _.
static std::string thumbName(const std::string& name) {
  std::string o = name;
  for (auto& ch : o) if (strchr("&*/:`<>?\\|\"", ch)) ch = '_';
  return o;
}
static bool fetchSnap(const System& sys, const std::string& name, std::vector<uint8_t>& png) {
  const std::string url = std::string("https://raw.githubusercontent.com/libretro-thumbnails/") + sys.repo + "/master/Named_Snaps/" + encode(thumbName(name)) + ".png";
  return get(url, png) == 200 && png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G';
}
static bool fetchArt(const System& sys, const std::string& name, std::vector<uint8_t>& png) {
  const std::string url = std::string("https://raw.githubusercontent.com/libretro-thumbnails/") + sys.repo + "/master/Named_Boxarts/" + encode(thumbName(name)) + ".png";
  return get(url, png) == 200 && png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G';
}

// ---- LaunchBox Games Database: details and box images (front, back, spine) ----
// Its public Metadata.zip (one large XML) is downloaded once and read through without unpacking it to
// the card; only this system's games are kept, in a small index (<data>/art/launchbox.tsv), and the
// zip is deleted. Images: https://images.launchbox-app.com/<FileName>.
struct LbGame { std::string name, developer, publisher, year, genres, overview; };
struct LbImage { std::string type, region, file; };  // type: F front, B back, S spine
struct Lb {
  std::map<std::string, LbGame> games;                 // by DatabaseID
  std::map<std::string, std::vector<LbImage>> images;  // by DatabaseID
  std::map<std::string, std::string> byKey;            // title key (names and alternate names) -> DatabaseID
};
// "Tick, The (USA) [!]" -> "The Tick"
static std::string cleanTitle(const std::string& name) {
  std::string t = name.substr(0, std::min(name.find(" ("), name.find(" [")));
  const size_t c = t.rfind(", ");
  if (c != std::string::npos) { const std::string art = t.substr(c + 2); if (art == "The" || art == "A" || art == "An") t = art + " " + t.substr(0, c); }
  return t;
}
static std::string words(const std::string& s) {  // lower-case letters and digits, single spaces
  std::string o;
  for (char ch : lower(s)) { if (isalnum((unsigned char)ch)) o += ch; else if (ch == '\'') continue; else if (!o.empty() && o.back() != ' ') o += ' '; }
  while (!o.empty() && o.back() == ' ') o.pop_back();
  return o;
}
static std::string lbKey(const std::string& title) {  // "The Legend of Zelda: A Link to the Past" -> "legend of zelda a link to the past"
  std::string w = words(title);
  if (w.compare(0, 4, "the ") == 0) w = w.substr(4);
  return w;
}
static std::string xmlText(const std::string& s) {  // XML entities -> text
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '&') { o += s[i]; continue; }
    const size_t e = s.find(';', i);
    if (e == std::string::npos || e - i > 10) { o += s[i]; continue; }
    const std::string ent = s.substr(i + 1, e - i - 1);
    if (ent == "amp") o += '&'; else if (ent == "lt") o += '<'; else if (ent == "gt") o += '>'; else if (ent == "quot") o += '"'; else if (ent == "apos") o += '\'';
    else if (!ent.empty() && ent[0] == '#') {
      unsigned cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? (unsigned)strtoul(ent.c_str() + 2, nullptr, 16) : (unsigned)strtoul(ent.c_str() + 1, nullptr, 10);
      if (cp < 0x80) o += (char)cp;
      else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
      else { o += (char)(0xE0 | ((cp >> 12) & 15)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
    } else { o += s.substr(i, e - i + 1); }
    i = e;
  }
  return o;
}
static std::string tsvField(const std::string& s) { std::string o; for (char ch : s) o += ch == '\t' ? ' ' : ch == '\n' ? '\x01' : ch == '\r' ? ' ' : ch; return o; }
static std::string tsvText(const std::string& s) { std::string o = s; for (auto& ch : o) if (ch == '\x01') ch = '\n'; return o; }
static std::vector<std::string> splitTab(const std::string& l) { std::vector<std::string> v; size_t p = 0; for (;;) { const size_t e = l.find('\t', p); v.push_back(l.substr(p, e == std::string::npos ? std::string::npos : e - p)); if (e == std::string::npos) break; p = e + 1; } return v; }

static size_t toFile(char* p, size_t sz, size_t n, void* ud) { return fwrite(p, sz, n, (FILE*)ud) * sz; }
static int progress(void*, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
  st.gotMB = (int)(now >> 20); st.sizeMB = (int)(total >> 20);
  return 0;
}
// A large download to a file; an interrupted one (path.part left behind) carries on where it stopped.
static bool downloadTo(const std::string& url, const std::string& path) {
  struct stat ps; const curl_off_t have = stat((path + ".part").c_str(), &ps) == 0 ? (curl_off_t)ps.st_size : 0;
  FILE* f = fopen((path + ".part").c_str(), have ? "ab" : "wb");
  if (!f) return false;
  if (have) logf("boxart: resuming at %lld bytes", (long long)have);
  CURL* c = curl_easy_init();
  if (have) curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE, have);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/net/cacert.pem");
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toFile);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "SW2-emulator-boxart/1.0");
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
  long code = -1;
  const CURLcode r = curl_easy_perform(c);
  if (r == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  else logf("boxart: %s: %s", url.c_str(), curl_easy_strerror(r));
  curl_easy_cleanup(c);
  fclose(f);
  if (code != 200 && code != 206) { if (code != -1) remove((path + ".part").c_str()); return false; }  // (a broken connection keeps the part)
  remove(path.c_str());
  return rename((path + ".part").c_str(), path.c_str()) == 0;
}

// Reads Metadata.xml from the zip line by line, keeping this system's games, their alternate names
// and box images; writes the index.
static bool buildLbIndex(const System& sys, const std::string& zipPath, const std::string& indexPath) {
  unzFile z = unzOpen(zipPath.c_str());
  if (!z) return false;
  bool ok = unzLocateFile(z, "Metadata.xml", 0) == UNZ_OK && unzOpenCurrentFile(z) == UNZ_OK;
  if (!ok) { unzClose(z); return false; }
  std::map<std::string, LbGame> games;
  std::vector<std::pair<std::string, std::string>> alts;     // id, name
  std::vector<std::pair<std::string, LbImage>> imgs;        // id, image
  enum { NONE, GAME, ALT, IMAGE } in = NONE;
  std::map<std::string, std::string> f;  // the current element's fields
  std::string field, value; bool multi = false;
  auto tag = [](const std::string& l, std::string& name, std::string& val, bool& open) {  // "<Name>val</Name>" or "<Overview>start..."
    const size_t a = l.find('<'); if (a == std::string::npos || l.size() < a + 2 || l[a + 1] == '/') return false;
    const size_t b = l.find('>', a); if (b == std::string::npos || l[b - 1] == '/') return false;
    name = l.substr(a + 1, b - a - 1);
    const size_t e = l.find("</" + name + ">", b);
    open = e == std::string::npos;
    val = l.substr(b + 1, open ? std::string::npos : e - b - 1);
    return true;
  };
  std::string buf, line; std::vector<char> chunk(1 << 16);
  long lines = 0;
  for (;;) {
    const int n = unzReadCurrentFile(z, chunk.data(), (unsigned)chunk.size());
    if (n <= 0) break;
    buf.append(chunk.data(), n);
    size_t p = 0, e;
    while ((e = buf.find('\n', p)) != std::string::npos) {
      line.assign(buf, p, e - p); p = e + 1; lines++;
      if (multi) {  // inside a field that spans lines (Overview)
        const size_t end = line.find("</" + field + ">");
        if (end == std::string::npos) { value += "\n" + line; continue; }
        value += "\n" + line.substr(0, end); f[field] = xmlText(value); multi = false; continue;
      }
      const size_t s0 = line.find_first_not_of(' ');
      if (s0 == std::string::npos) continue;
      if (line.compare(s0, 6, "<Game>") == 0) { in = GAME; f.clear(); continue; }
      if (line.compare(s0, 19, "<GameAlternateName>") == 0) { in = ALT; f.clear(); continue; }
      if (line.compare(s0, 11, "<GameImage>") == 0) { in = IMAGE; f.clear(); continue; }
      if (line.compare(s0, 7, "</Game>") == 0) {
        if (f["Platform"] == sys.lbPlatform) {
          LbGame g; g.name = f["Name"]; g.developer = f["Developer"]; g.publisher = f["Publisher"]; g.genres = f["Genres"]; g.overview = f["Overview"];
          g.year = !f["ReleaseYear"].empty() ? f["ReleaseYear"] : f["ReleaseDate"].substr(0, 4);
          games[f["DatabaseID"]] = g;
        }
        in = NONE; continue;
      }
      if (line.compare(s0, 20, "</GameAlternateName>") == 0) { if (games.count(f["DatabaseID"])) alts.push_back({f["DatabaseID"], f["AlternateName"]}); in = NONE; continue; }
      if (line.compare(s0, 12, "</GameImage>") == 0) {
        const std::string& t = f["Type"];
        const std::string ty = t == "Box - Front" ? "F" : t == "Box - Back" ? "B" : t == "Box - Spine" ? "S" : "";
        if (!ty.empty() && games.count(f["DatabaseID"])) imgs.push_back({f["DatabaseID"], LbImage{ty, f["Region"], f["FileName"]}});
        in = NONE; continue;
      }
      if (in == NONE) continue;
      std::string name, val; bool open;
      if (!tag(line.substr(s0), name, val, open)) continue;
      if (open) { field = name; value = val; multi = true; } else f[name] = xmlText(val);
    }
    buf.erase(0, p);
  }
  unzCloseCurrentFile(z); unzClose(z);
  logf("boxart: LaunchBox: %ld lines read, %d %s games, %d alternate names, %d box images", lines, (int)games.size(), sys.lbPlatform, (int)alts.size(), (int)imgs.size());
  if (games.empty()) return false;
  std::string out;
  for (auto& kv : games) { const LbGame& g = kv.second; out += "G\t" + kv.first + "\t" + tsvField(g.name) + "\t" + tsvField(g.developer) + "\t" + tsvField(g.publisher) + "\t" + tsvField(g.year) + "\t" + tsvField(g.genres) + "\t" + tsvField(g.overview) + "\n"; }
  for (auto& a : alts) out += "A\t" + a.first + "\t" + tsvField(a.second) + "\n";
  for (auto& i : imgs) out += "I\t" + i.first + "\t" + i.second.type + "\t" + tsvField(i.second.region) + "\t" + tsvField(i.second.file) + "\n";
  std::vector<uint8_t> d(out.begin(), out.end());
  return writeFile(indexPath, d);
}
static bool loadLb(const System& sys, Lb& lb, bool rebuild) {
  const std::string index = sys.dataDir + "/art/launchbox.tsv", zip = sys.dataDir + "/art/launchbox-metadata.zip";
  std::string text;
  if (rebuild || !readAll(index, text) || text.size() < 100) {
    st.phase = 1;
    logf("boxart: downloading the LaunchBox database");
    if (!downloadTo("https://gamesdb.launchbox-app.com/Metadata.zip", zip)) { st.phase = 0; return false; }
    st.phase = 2;
    const bool built = buildLbIndex(sys, zip, index);
    remove(zip.c_str());
    st.phase = 0;
    if (!built || !readAll(index, text)) return false;
  }
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p); if (e == std::string::npos) e = text.size();
    const std::vector<std::string> v = splitTab(text.substr(p, e - p)); p = e + 1;
    if (v[0] == "G" && v.size() >= 8) { lb.games[v[1]] = LbGame{v[2], v[3], v[4], v[5], v[6], tsvText(v[7])}; lb.byKey.emplace(lbKey(v[2]), v[1]); }
    else if (v[0] == "A" && v.size() >= 3) lb.byKey.emplace(lbKey(v[2]), v[1]);
    else if (v[0] == "I" && v.size() >= 5) lb.images[v[1]].push_back(LbImage{v[2], v[3], v[4]});
  }
  return !lb.games.empty();
}
// The best image of a type for a region (the game's region, then worldwide / unmarked, then any).
static const LbImage* lbPick(const Lb& lb, const std::string& id, const char* type, const std::string& reg) {
  auto it = lb.images.find(id);
  if (it == lb.images.end()) return nullptr;
  auto score = [&](const std::string& r) {
    const std::string l = lower(r);
    const bool us = l.find("north america") != std::string::npos || l.find("united states") != std::string::npos || l == "canada";
    const bool jp = l.find("japan") != std::string::npos;
    const bool world = l.empty() || l == "world";
    const bool eu = !us && !jp && !world;
    if ((reg == "us" && us) || (reg == "jp" && jp) || (reg == "eu" && eu)) return 0;
    return world ? 1 : 2;
  };
  const LbImage* best = nullptr; int bestScore = 9;
  for (const LbImage& im : it->second) if (im.type == type) { const int s = score(im.region); if (s < bestScore) { best = &im; bestScore = s; } }
  return best;
}
static bool fetchLbImage(const LbImage* im, const std::string& path) {
  if (!im) return false;
  std::vector<uint8_t> d;
  if (get("https://images.launchbox-app.com/" + encode(im->file), d) != 200 || d.size() < 100) return false;
  return writeFile(path, d);
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
static void writeInfo(const System& sys, const std::string& gameName, const std::string& title, const LbGame* g, const std::string& tv) {
  const std::string j = "{\"title\":" + jsonString(g ? g->name : title) + ",\"region\":" + jsonString(tv) + ",\"developer\":" + jsonString(g ? g->developer : "") + ",\"publisher\":" + jsonString(g ? g->publisher : "") +
                        ",\"year\":" + jsonString(g ? g->year : "") + ",\"genre\":" + jsonString(g ? g->genres : "") + ",\"source\":" + jsonString(g ? "LaunchBox" : "") +
                        ",\"bio\":" + jsonString(g ? g->overview : "") + "}";
  std::vector<uint8_t> d(j.begin(), j.end());
  writeFile(infoPath(sys, gameName), d);
}

static void run(System sys, std::vector<Game> games, bool again, std::function<bool(const Game&, std::vector<uint8_t>&)> readFile, int limit) {
  logf("boxart: %d games%s%s", (int)games.size(), again ? ", all again" : "", limit ? (", first " + std::to_string(limit)).c_str() : "");
  mkdir((sys.dataDir + "/art").c_str(), 0777);
  mkdir((sys.dataDir + "/art/boxart").c_str(), 0777);
  mkdir((sys.dataDir + "/art/info").c_str(), 0777);
  for (const char* old : {"developer", "publisher", "releaseyear", "genre"}) remove((sys.dataDir + "/art/" + old + ".dat").c_str());  // (earlier test builds')
  auto needArt = [&](const Game& g) { return again || (!exists(artPath(sys, g.name)) && !exists(missPath(sys, g.name))); };
  auto needInfo = [&](const Game& g) { return again || !exists(infoPath(sys, g.name)); };
  auto needSnap = [&](const Game&) { return false; };  // screenshots: not downloaded for now (fetchSnap kept for later)
  if (limit) {  // only the first `limit` games that need looking up
    std::vector<Game> want;
    for (const Game& g : games) if (needArt(g) || needInfo(g) || needSnap(g)) { want.push_back(g); if ((int)want.size() >= limit) break; }
    games = want;
  }
  st.total = (int)games.size();
  Db db;
  const bool haveDb = loadDb(sys, db);
  logf("boxart: No-Intro database %s (%d entries)", haveDb ? "ready" : "NOT available", (int)db.byCrc.size());
  std::map<std::string, std::vector<std::string>> byTitle;  // title key -> official names
  for (auto& n : db.names) byTitle[titleKey(n)].push_back(n);
  Lb lb;
  const bool haveLb = loadLb(sys, lb, false);
  logf("boxart: LaunchBox %s (%d games)", haveLb ? "ready" : "NOT available", (int)lb.games.size());
  int found = 0, missing = 0;
  for (const Game& g : games) {
    const bool wantArt = needArt(g), wantInfo = needInfo(g), wantSnap = needSnap(g);
    if (!wantArt && !wantInfo && !wantSnap) { st.done++; continue; }
    { std::string shown = cleanTitle(stem(g.name)); snprintf(st.current, sizeof st.current, "%s", shown.c_str()); }
    std::vector<std::string> tries;  // official names, best first
    std::string ser;  // a disc's serial (PS2), for the region too
    if (haveDb && sys.serialOf) {  // 1. a disc's serial
      ser = sys.serialOf(g);
      auto it = ser.empty() ? db.bySerial.end() : db.bySerial.find(ser);
      if (it != db.bySerial.end()) tries.push_back(it->second);
      logf("boxart: %s: serial %s%s", g.name.c_str(), ser.empty() ? "(none)" : ser.c_str(), it != db.bySerial.end() ? (" -> " + it->second).c_str() : "");
    }
    if (haveDb) {  // 1. the contents' CRC32
      std::vector<uint8_t> file;
      if (!sys.serialOf && readFile && readFile(g, file) && !file.empty()) {
        size_t off = 0, len = file.size();
        if (sys.crcRange) sys.crcRange(file, off, len);
        if (off + len <= file.size()) {
          const uint32_t crc = (uint32_t)crc32(0L, file.data() + off, (uInt)len);
          auto it = db.byCrc.find(crc);
          if (it != db.byCrc.end()) tries.push_back(it->second);
        }
      }
      // 2. the same title, the game's region first
      auto t = byTitle.find(titleKey(stem(g.name)));
      if (t != byTitle.end()) {
        const std::string r = region(g.name);
        std::vector<std::string> v = t->second;
        std::stable_sort(v.begin(), v.end(), [&](const std::string& a, const std::string& b) { return (region(a) == r) > (region(b) == r); });
        for (auto& n : v) if (tries.size() < 6 && std::find(tries.begin(), tries.end(), n) == tries.end()) tries.push_back(n);
      }
    }
    tries.push_back(stem(g.name));  // 3. the file name as it is
    // The LaunchBox game: by the best names' titles
    std::string lbId;
    for (const std::string& n : tries) { auto k = lb.byKey.find(lbKey(cleanTitle(n))); if (k != lb.byKey.end()) { lbId = k->second; break; } }
    const LbGame* lg = lbId.empty() ? nullptr : &lb.games[lbId];
    const std::string reg = region(tries.front());
    if (wantArt) {
      bool ok = fetchLbImage(lbPick(lb, lbId, "F", reg), artPath(sys, g.name));
      if (!ok) { std::vector<uint8_t> png; for (const std::string& n : tries) if (fetchArt(sys, n, png)) { ok = writeFile(artPath(sys, g.name), png); break; } }  // libretro's front
      const bool back = ok && fetchLbImage(lbPick(lb, lbId, "B", reg), backPath(sys, g.name));
      const bool spine = ok && fetchLbImage(lbPick(lb, lbId, "S", reg), spinePath(sys, g.name));
      logf("boxart: %s -> %s%s%s%s", g.name.c_str(), ok ? "front" : "not found", back ? ", back" : "", spine ? ", spine" : "", lg ? (", LaunchBox: " + lg->name).c_str() : "");
      if (ok) { found++; remove(missPath(sys, g.name).c_str()); }
      else { missing++; std::vector<uint8_t> none; writeFile(missPath(sys, g.name), none); }
    }
    if (wantInfo) {
      // the TV system: the serial's (PS2), else the official name's region, else the file name's
      std::string tv = tvSystemOfSerial(ser);
      if (tv.empty()) tv = tvSystem(tries.front());
      if (tv.empty()) tv = tvSystem(g.name);
      writeInfo(sys, g.name, cleanTitle(tries.front()), lg, tv);
    }
    if (wantSnap) {  // a gameplay screenshot
      bool ok = false; std::vector<uint8_t> png;
      for (const std::string& n : tries) if (fetchSnap(sys, n, png)) { ok = writeFile(snapPath(sys, g.name), png); break; }
      if (ok) remove(snapMissPath(sys, g.name).c_str()); else { std::vector<uint8_t> none; writeFile(snapMissPath(sys, g.name), none); }
      logf("boxart: %s: screenshot %s", g.name.c_str(), ok ? "found" : "not found");
    }
    st.found = found; st.missing = missing; st.done++;
  }
  st.current[0] = 0;
  st.message = !haveDb && !haveLb && found == 0 ? "Box art: no connection to the art server" :
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
bool start(const System& sys, std::vector<Game> games, bool again, std::function<bool(const Game&, std::vector<uint8_t>&)> readFile, int limit) {
  if (st.running) return false;
  if (!netUp()) { st.message = "Box art: the network could not be started"; return false; }
  st.total = (int)games.size(); st.done = 0; st.found = 0; st.missing = 0; st.phase = 0; st.message.clear();
  st.running = true;
  static Thread t; static bool made = false;
  if (made) { threadWaitForExit(&t); threadClose(&t); made = false; }
  Job* j = new Job{sys, std::move(games), again, std::move(readFile), limit};
  Result rc = threadCreate(&t, threadMain, j, nullptr, 1024 * 1024, 0x2C, -2);  // 1 MB of stack
  if (R_SUCCEEDED(rc)) rc = threadStart(&t);
  logf("boxart: thread 0x%x", rc);
  if (R_FAILED(rc)) { delete j; st.running = false; st.message = "Box art: could not start"; return false; }
  made = true;
  return true;
}

}  // namespace boxart
