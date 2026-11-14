// Minimal JSON values for SW2NES's settings and save metadata files (same
// formats as the CivHomebrew app: settings.json, <game>.settings.json,
// <game>.slots.json, <game>.cheats.json).
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Json {
  enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;  // keeps key order

  static Json number(double v) { Json j; j.type = NUM; j.n = v; return j; }
  static Json boolean(bool v) { Json j; j.type = BOOL; j.b = v; return j; }
  static Json string(const std::string& v) { Json j; j.type = STR; j.s = v; return j; }
  static Json array() { Json j; j.type = ARR; return j; }
  static Json object() { Json j; j.type = OBJ; return j; }

  const Json* get(const std::string& k) const {
    if (type != OBJ) return nullptr;
    for (auto& kv : o) if (kv.first == k) return &kv.second;
    return nullptr;
  }
  Json& set(const std::string& k, const Json& v) {
    for (auto& kv : o) if (kv.first == k) { kv.second = v; return kv.second; }
    o.emplace_back(k, v);
    return o.back().second;
  }
  void remove(const std::string& k) {
    for (size_t i = 0; i < o.size(); i++) if (o[i].first == k) { o.erase(o.begin() + i); return; }
  }
  bool isNum() const { return type == NUM; }
  bool isBool() const { return type == BOOL; }
  bool isStr() const { return type == STR; }
  bool isArr() const { return type == ARR; }

  // ---- parse ----
  static bool parse(const std::string& text, Json& out) {
    size_t i = 0;
    if (!parseValue(text, i, out, 0)) return false;
    skip(text, i);
    return i == text.size();
  }
  static void skip(const std::string& t, size_t& i) { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) i++; }
  static bool parseString(const std::string& t, size_t& i, std::string& out) {
    if (i >= t.size() || t[i] != '"') return false;
    i++;
    while (i < t.size() && t[i] != '"') {
      char c = t[i++];
      if (c != '\\') { out += c; continue; }
      if (i >= t.size()) return false;
      char e = t[i++];
      switch (e) {
        case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
        case 'b': out += '\b'; break; case 'f': out += '\f'; break;
        case 'u': {
          if (i + 4 > t.size()) return false;
          unsigned cp = (unsigned)strtoul(t.substr(i, 4).c_str(), nullptr, 16); i += 4;
          if (cp < 0x80) out += (char)cp;
          else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
          else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
          break;
        }
        default: out += e;
      }
    }
    if (i >= t.size()) return false;
    i++;
    return true;
  }
  static bool parseValue(const std::string& t, size_t& i, Json& v, int depth) {
    if (depth > 32) return false;
    skip(t, i);
    if (i >= t.size()) return false;
    char c = t[i];
    if (c == '{') {
      v = object(); i++; skip(t, i);
      if (i < t.size() && t[i] == '}') { i++; return true; }
      for (;;) {
        skip(t, i); std::string k;
        if (!parseString(t, i, k)) return false;
        skip(t, i); if (i >= t.size() || t[i] != ':') return false; i++;
        Json item; if (!parseValue(t, i, item, depth + 1)) return false;
        v.o.emplace_back(k, item);
        skip(t, i); if (i >= t.size()) return false;
        if (t[i] == ',') { i++; continue; }
        if (t[i] == '}') { i++; return true; }
        return false;
      }
    }
    if (c == '[') {
      v = array(); i++; skip(t, i);
      if (i < t.size() && t[i] == ']') { i++; return true; }
      for (;;) {
        Json item; if (!parseValue(t, i, item, depth + 1)) return false;
        v.a.push_back(item);
        skip(t, i); if (i >= t.size()) return false;
        if (t[i] == ',') { i++; continue; }
        if (t[i] == ']') { i++; return true; }
        return false;
      }
    }
    if (c == '"') { v = Json(); v.type = STR; return parseString(t, i, v.s); }
    if (!t.compare(i, 4, "true")) { v = boolean(true); i += 4; return true; }
    if (!t.compare(i, 5, "false")) { v = boolean(false); i += 5; return true; }
    if (!t.compare(i, 4, "null")) { v = Json(); i += 4; return true; }
    const char* start = t.c_str() + i; char* end = nullptr;
    double d = strtod(start, &end);
    if (end == start) return false;
    i += end - start; v = number(d);
    return true;
  }

  // ---- write (ASCII; non-ASCII escaped as \uXXXX like the JS app) ----
  static void quote(const std::string& s, std::string& out) {
    out += '"';
    for (size_t i = 0; i < s.size(); i++) {
      unsigned char c = s[i];
      if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
      else if (c == '\n') out += "\\n";
      else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
      else if (c < 0x80) out += (char)c;
      else {
        unsigned cp = 0; int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        cp = c & (0x3F >> extra);
        while (extra-- > 0 && i + 1 < s.size()) cp = (cp << 6) | (s[++i] & 0x3F);
        char b[8]; snprintf(b, sizeof b, "\\u%04x", cp & 0xFFFF); out += b;
      }
    }
    out += '"';
  }
  void dump(std::string& out) const {
    switch (type) {
      case NUL: out += "null"; break;
      case BOOL: out += b ? "true" : "false"; break;
      case NUM: {
        char buf[32];
        if (n == (double)(long long)n && n < 1e15 && n > -1e15) snprintf(buf, sizeof buf, "%lld", (long long)n);
        else snprintf(buf, sizeof buf, "%.17g", n);
        out += buf; break;
      }
      case STR: quote(s, out); break;
      case ARR: out += '['; for (size_t i = 0; i < a.size(); i++) { if (i) out += ','; a[i].dump(out); } out += ']'; break;
      case OBJ: out += '{'; for (size_t i = 0; i < o.size(); i++) { if (i) out += ','; quote(o[i].first, out); out += ':'; o[i].second.dump(out); } out += '}'; break;
    }
  }
  std::string dump() const { std::string s; dump(s); return s; }
};
