// SW2NES native front end: OpenGL ES 3 drawing (rects, text, game picture).
#include "gfx.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <png.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <switch.h>
#include <switch2/services/vi2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

namespace gfx {

static EGLDisplay display = EGL_NO_DISPLAY;
static EGLContext context = EGL_NO_CONTEXT;
static EGLSurface surface = EGL_NO_SURFACE;
static GLuint vao = 0, vbo = 0;
static GLuint rectProg = 0, texProg = 0;
static void timerInit(); static void timerBegin(); static void timerEnd();  // GPU timing (below)
static GLint rRect, rRadius, rBorder, rFill, rBorderColor, rScreen;
static GLint tScreen, tTex, tMode, tColor;
// Picture shaders (romfs:/shaders/prelude.glsl + one file each).
struct PicProg { GLuint prog; GLint screen, tex, rows, cols, origin, size, texSize, srcOff, src, crt, dim, corner, vignette, curve, frameCount, rgbTex, nesOff, nesSrc, boost, beam; };
enum { MAX_SHADERS = 24 };
static PicProg progs[MAX_SHADERS];
static int progCount = 0;
static float frameCounter = 0;

Color rgb(uint32_t hex, float alpha) {
  return Color{((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha};
}

static GLuint compile(GLenum type, const char* src, std::string& error) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, NULL, log); error = std::string("Shader: ") + log; }
  return s;
}
static GLuint program(const char* vs, const char* fs, std::string& error) {
  GLuint p = glCreateProgram();
  glAttachShader(p, compile(GL_VERTEX_SHADER, vs, error));
  glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs, error));
  glBindAttribLocation(p, 0, "pos");
  glBindAttribLocation(p, 1, "uv");
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[512]; glGetProgramInfoLog(p, sizeof log, NULL, log); error = std::string("Link: ") + log; }
  return p;
}

// Vertices: x, y (screen px), u, v.
static const char* VS =
    "#version 300 es\n"
    "layout(location=0) in vec2 pos; layout(location=1) in vec2 uv;\n"
    "uniform vec2 screen; out vec2 vuv; out vec2 vpos;\n"
    "void main(){ vuv = uv; vpos = pos; gl_Position = vec4(pos.x / screen.x * 2.0 - 1.0, 1.0 - pos.y / screen.y * 2.0, 0.0, 1.0); }\n";
// Rounded rectangle by signed distance, with an inner border.
static const char* RECT_FS =
    "#version 300 es\nprecision mediump float;\n"
    "in vec2 vpos; uniform vec4 rect; uniform float radius; uniform float border; uniform vec4 fill; uniform vec4 bcolor; out vec4 o;\n"
    "void main(){ vec2 c = rect.xy + rect.zw * 0.5; vec2 h = rect.zw * 0.5;\n"
    "  vec2 q = abs(vpos - c) - h + vec2(radius); float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;\n"
    "  float a = clamp(0.5 - d, 0.0, 1.0); vec4 col = fill;\n"
    "  if (border > 0.0) { float bi = clamp(0.5 - (d + border), 0.0, 1.0); col = mix(bcolor, fill, bi); }\n"
    "  o = vec4(col.rgb, col.a * a); }\n";
// Textures: mode 0 = RGBA image (alpha scaled), 1 = text atlas (R = coverage, tinted).
static const char* TEX_FS =
    "#version 300 es\nprecision mediump float;\n"
    "in vec2 vuv; uniform sampler2D tex; uniform int mode; uniform vec4 color; out vec4 o;\n"
    "void main(){ vec4 t = texture(tex, vuv); if (mode == 1) o = vec4(color.rgb, color.a * t.r); else o = vec4(t.rgb, t.a * color.a); }\n";
// Game picture: nearest texels; CRT strips give each screen row / column's
// darkening (exactly as the JS overlays: two black layers, multiplied).
/* ---------------- Fonts ---------------- */
static FT_Library ft = NULL;
static FT_Face faces[3] = {NULL, NULL, NULL};
static std::vector<uint8_t> fontData[3];
static GLuint atlas = 0;
enum { ATLAS = 2048 };
static int shelfX = 0, shelfY = 0, shelfH = 0;
struct Glyph { float u0, v0, u1, v1; int w, h, left, top; float advance; };
static std::map<uint64_t, Glyph> glyphs;

static bool readFile(const char* path, std::vector<uint8_t>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  out.resize(n);
  bool ok = fread(out.data(), 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}

static const Glyph& glyph(int font, int px, uint32_t cp) {
  uint64_t key = ((uint64_t)font << 40) | ((uint64_t)px << 24) | cp;
  auto it = glyphs.find(key);
  if (it != glyphs.end()) return it->second;
  Glyph g = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  FT_Face face = faces[font];
  FT_Set_Pixel_Sizes(face, 0, px);
  if (FT_Load_Char(face, cp, FT_LOAD_RENDER) == 0) {
    FT_Bitmap& b = face->glyph->bitmap;
    g.w = b.width; g.h = b.rows; g.left = face->glyph->bitmap_left; g.top = face->glyph->bitmap_top;
    g.advance = face->glyph->advance.x / 64.0f;
    if (g.w > 0 && g.h > 0) {
      if (shelfX + g.w + 1 > ATLAS) { shelfX = 0; shelfY += shelfH + 1; shelfH = 0; }
      if (shelfY + g.h + 1 > ATLAS) {  // atlas full: start over (rare: few sizes are used)
        glyphs.clear(); shelfX = shelfY = shelfH = 0;
        std::vector<uint8_t> zero(ATLAS * ATLAS, 0);
        glBindTexture(GL_TEXTURE_2D, atlas);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ATLAS, ATLAS, GL_RED, GL_UNSIGNED_BYTE, zero.data());
      }
      std::vector<uint8_t> pixels(g.w * g.h);
      for (int y = 0; y < g.h; y++) memcpy(&pixels[y * g.w], b.buffer + y * b.pitch, g.w);
      glBindTexture(GL_TEXTURE_2D, atlas);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, shelfX, shelfY, g.w, g.h, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      g.u0 = shelfX / (float)ATLAS; g.v0 = shelfY / (float)ATLAS;
      g.u1 = (shelfX + g.w) / (float)ATLAS; g.v1 = (shelfY + g.h) / (float)ATLAS;
      shelfX += g.w + 1; if (g.h > shelfH) shelfH = g.h;
    }
  }
  return glyphs.emplace(key, g).first->second;
}

static uint32_t nextCodepoint(const std::string& s, size_t& i) {
  unsigned char c = s[i++];
  if (c < 0x80) return c;
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t cp = c & (0x3F >> extra);
  while (extra-- > 0 && i < s.size()) cp = (cp << 6) | (s[i++] & 0x3F);
  return cp;
}

float textWidth(const std::string& s, float size, Font f) {
  int px = (int)(size * UI_SCALE + 0.5f); float w = 0;
  for (size_t i = 0; i < s.size();) w += glyph(f, px, nextCodepoint(s, i)).advance;
  return w / UI_SCALE;
}

std::string ellipsize(const std::string& s, float width, float size, Font f) {
  if (textWidth(s, size, f) <= width) return s;
  std::string t = s;
  while (!t.empty()) {
    do t.pop_back(); while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80);
    if (textWidth(t + "...", size, f) <= width) return t + "...";
  }
  return "...";
}

static std::vector<float> verts;

static void setScreen(GLint loc) { glUniform2f(loc, SCREEN_W, SCREEN_H); }

float text(const std::string& s, float x, float y, float size, Color c, Font f) {
  x *= UI_SCALE; y *= UI_SCALE; size *= UI_SCALE;
  int px = (int)(size + 0.5f);
  FT_Face face = faces[f];
  FT_Set_Pixel_Sizes(face, 0, px);
  float ascender = face->size->metrics.ascender / 64.0f, descender = -face->size->metrics.descender / 64.0f;
  float baseline = (float)(int)(y + (size * 1.25f - (ascender + descender)) / 2 + ascender + 0.5f);
  float pen = (float)(int)(x + 0.5f);
  verts.clear();
  for (size_t i = 0; i < s.size();) {
    const Glyph& g = glyph(f, px, nextCodepoint(s, i));
    if (g.w > 0) {
      float x0 = pen + g.left, y0 = baseline - g.top, x1 = x0 + g.w, y1 = y0 + g.h;
      float q[] = {x0, y0, g.u0, g.v0, x1, y0, g.u1, g.v0, x0, y1, g.u0, g.v1,
                   x1, y0, g.u1, g.v0, x1, y1, g.u1, g.v1, x0, y1, g.u0, g.v1};
      verts.insert(verts.end(), q, q + 24);
    }
    pen += g.advance;
  }
  if (!verts.empty()) {
    glUseProgram(texProg); setScreen(tScreen);
    glUniform1i(tMode, 1); glUniform4f(tColor, c.r, c.g, c.b, c.a);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlas); glUniform1i(tTex, 0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts.size() / 4));
  }
  return (pen - x) / UI_SCALE;
}

static void quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
  float q[] = {x, y, u0, v0, x + w, y, u1, v0, x, y + h, u0, v1,
               x + w, y, u1, v0, x + w, y + h, u1, v1, x, y + h, u0, v1};
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}

void rect(float x, float y, float w, float h, Color fill, float radius, float border, Color bc) {
  if (w <= 0 || h <= 0) return;
  x *= UI_SCALE; y *= UI_SCALE; w *= UI_SCALE; h *= UI_SCALE; radius *= UI_SCALE; border *= UI_SCALE;
  glUseProgram(rectProg); setScreen(rScreen);
  glUniform4f(rRect, x, y, w, h);
  float r = radius; if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
  glUniform1f(rRadius, r); glUniform1f(rBorder, border);
  glUniform4f(rFill, fill.r, fill.g, fill.b, fill.a);
  glUniform4f(rBorderColor, bc.r, bc.g, bc.b, bc.a);
  quad(x - 1, y - 1, w + 2, h + 2, 0, 0, 0, 0);
}

void clip(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) { noClip(); return; }
  x = (int)(x * UI_SCALE); y = (int)(y * UI_SCALE); w = (int)(w * UI_SCALE + 0.5f); h = (int)(h * UI_SCALE + 0.5f);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, SCREEN_H - (y + h), w, h);
}
void noClip() { glDisable(GL_SCISSOR_TEST); }

/* ---------------- Picture ---------------- */
struct Picture {
  GLuint tex, rows, cols; int w, h, crtRows, crtCols, shader; bool crt; float cornerR, cornerSoft, vigStrength, vigStart, curve;
  // NTSC signal: colour numbers (R16UI) -> decoded picture 3 columns per pixel.
  GLuint colorTex, ntscTex, fbo; int signal; float decode[4], gammaOut, emuFrame;
  float boost, beamN, beamSub;
};
static GLuint ntscProg = 0;
static GLint nScreen, nColors, nInSize, nFrame, nDotCrawl, nLumaWin, nChromaWin, nSvideo, nDecode, nGamma;

static GLuint makeTexture(GLint filter) {
  GLuint t; glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

Picture* pictureCreate(int w, int h) {
  Picture* p = new Picture();
  p->w = w; p->h = h; p->crt = false; p->crtRows = p->crtCols = 0;
  p->cornerR = 0; p->cornerSoft = 1; p->vigStrength = 0; p->vigStart = 0.5f; p->curve = 0; p->shader = 0;
  p->tex = makeTexture(GL_LINEAR);  // shaders read exact texels with texelFetch, bilinear with texture()
  p->signal = 0; p->colorTex = p->ntscTex = p->fbo = 0; p->gammaOut = 1; p->emuFrame = 0; p->boost = 1; p->beamN = 1; p->beamSub = 0;
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  p->rows = makeTexture(GL_NEAREST);
  p->cols = makeTexture(GL_NEAREST);
  return p;
}
void pictureSetSignal(Picture* p, int signal, const float decode[4], float gammaOut) {
  p->signal = ntscProg ? signal : 0;
  for (int i = 0; i < 4; i++) p->decode[i] = decode[i];
  p->gammaOut = gammaOut;
  if (p->signal && !p->colorTex) {
    p->colorTex = makeTexture(GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, p->w, p->h, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    p->ntscTex = makeTexture(GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, p->w * 3, p->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &p->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->ntscTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) p->signal = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
}
void pictureUploadColors(Picture* p, const uint16_t* colors, float emuFrame) {
  if (!p->colorTex) return;
  p->emuFrame = emuFrame;
  glBindTexture(GL_TEXTURE_2D, p->colorTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, p->w, p->h, GL_RED_INTEGER, GL_UNSIGNED_SHORT, colors);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}
void pictureUpload(Picture* p, const uint32_t* rgba) {
  glBindTexture(GL_TEXTURE_2D, p->tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, p->w, p->h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
void pictureSetCrt(Picture* p, const uint8_t* rowAlpha, int rows, const uint8_t* colAlpha, int cols) {
  p->crt = rowAlpha && colAlpha && rows > 0 && cols > 0;
  if (!p->crt) return;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_2D, p->rows);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, rows, 0, GL_RED, GL_UNSIGNED_BYTE, rowAlpha);
  glBindTexture(GL_TEXTURE_2D, p->cols);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, cols, 1, 0, GL_RED, GL_UNSIGNED_BYTE, colAlpha);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  p->crtRows = rows; p->crtCols = cols;
}
void pictureDraw(Picture* p, int x, int y, int dw, int dh, int sx, int sy, int sw, int sh, float dim) {
  int idx = p->shader >= 0 && p->shader < progCount && progs[p->shader].prog ? p->shader : 0;
  const PicProg& g = progs[idx];
  if (!g.prog) return;
  bool ntsc = p->signal > 0 && p->fbo;
  if (ntsc) {
    // Pass 1: decode the composite signal into the (3W x H) texture.
    glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
    glViewport(0, 0, p->w * 3, p->h);
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glUseProgram(ntscProg);
    glUniform2f(nScreen, (float)(p->w * 3), (float)p->h);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, p->colorTex); glUniform1i(nColors, 0);
    glUniform2f(nInSize, (float)p->w, (float)p->h);
    glUniform1f(nFrame, p->emuFrame);
    glUniform1i(nDotCrawl, p->signal == 2 ? 1 : 0);
    glUniform1i(nSvideo, p->signal == 3 ? 1 : 0);
    glUniform1f(nLumaWin, p->signal == 3 ? 6.0f : 12.0f);
    glUniform1f(nChromaWin, p->signal == 3 ? 18.0f : 24.0f);
    glUniform4f(nDecode, p->decode[0], p->decode[1], p->decode[2], p->decode[3]);
    glUniform1f(nGamma, p->gammaOut);
    quad(0, 0, (float)(p->w * 3), (float)p->h, 0, 0, 1, 1);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCREEN_W, SCREEN_H);
  }
  int cx = ntsc ? 3 : 1;
  glUseProgram(g.prog);
  glUniform2f(g.screen, SCREEN_W, SCREEN_H);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ntsc ? p->ntscTex : p->tex); glUniform1i(g.tex, 0);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, p->rows); glUniform1i(g.rows, 1);
  glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, p->cols); glUniform1i(g.cols, 2);
  glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, p->tex); glUniform1i(g.rgbTex, 3);
  glActiveTexture(GL_TEXTURE0);
  glUniform2f(g.nesOff, (float)sx, (float)sy);
  glUniform2f(g.nesSrc, (float)sw, (float)sh);
  glUniform1f(g.boost, p->boost);
  glUniform2f(g.beam, p->beamN, p->beamSub);
  glUniform2f(g.origin, (float)x, (float)y);
  glUniform2f(g.size, (float)dw, (float)dh);
  glUniform2f(g.texSize, (float)(p->w * cx), (float)p->h);
  glUniform2f(g.srcOff, (float)(sx * cx), (float)sy);
  glUniform2f(g.src, (float)(sw * cx), (float)sh);
  glUniform2f(g.corner, p->cornerR, p->cornerSoft > 0.5f ? p->cornerSoft : 0.5f);
  glUniform2f(g.vignette, p->vigStrength, p->vigStart);
  glUniform2f(g.curve, p->curve * 0.75f, p->curve);  // barrel per axis (wider than tall, as a 4:3 tube)
  glUniform1i(g.crt, p->crt && idx == 0 ? 1 : 0);   // CRT strip styles: plain pixels only
  glUniform1f(g.dim, dim);
  glUniform1f(g.frameCount, frameCounter);
  glDisable(GL_BLEND);
  quad((float)x, (float)y, (float)dw, (float)dh, 0, 0, 1, 1);
  glEnable(GL_BLEND);
}
void pictureSetShader(Picture* p, int shader) { p->shader = shader; }
void pictureSetBoost(Picture* p, float exponent) { p->boost = exponent; }
void pictureSetBeam(Picture* p, int subframes, int current) { p->beamN = (float)subframes; p->beamSub = (float)current; }
int shaderCount() { return progCount; }
bool shaderOk(int i) { return i >= 0 && i < progCount && progs[i].prog; }
int loadShaders(const std::vector<std::string>& files, std::string& log) {
  std::vector<uint8_t> pre;
  if (!readFile("romfs:/shaders/prelude.glsl", pre)) { log = "shader prelude missing"; return 0; }
  std::string prelude(pre.begin(), pre.end());
  int ok = 0;
  progCount = 0;
  for (size_t i = 0; i < files.size() && i < MAX_SHADERS; i++) {
    PicProg& g = progs[progCount++];
    memset(&g, 0, sizeof g);
    std::vector<uint8_t> body;
    if (!readFile(("romfs:/shaders/" + files[i]).c_str(), body)) { log += files[i] + ": missing; "; continue; }
    std::string src = prelude + "\n" + std::string(body.begin(), body.end()), err;
    GLuint pr = program(VS, src.c_str(), err);
    if (!err.empty()) { log += files[i] + ": " + err.substr(0, 300) + "; "; glDeleteProgram(pr); continue; }
    g.prog = pr;
    g.screen = glGetUniformLocation(pr, "screen"); g.tex = glGetUniformLocation(pr, "tex");
    g.rows = glGetUniformLocation(pr, "rows"); g.cols = glGetUniformLocation(pr, "cols");
    g.origin = glGetUniformLocation(pr, "origin"); g.size = glGetUniformLocation(pr, "size");
    g.texSize = glGetUniformLocation(pr, "texSize"); g.srcOff = glGetUniformLocation(pr, "srcOff"); g.src = glGetUniformLocation(pr, "src");
    g.crt = glGetUniformLocation(pr, "crt"); g.dim = glGetUniformLocation(pr, "dim");
    g.corner = glGetUniformLocation(pr, "corner"); g.vignette = glGetUniformLocation(pr, "vignette");
    g.curve = glGetUniformLocation(pr, "curve"); g.frameCount = glGetUniformLocation(pr, "frameCount");
    g.rgbTex = glGetUniformLocation(pr, "rgbTex"); g.nesOff = glGetUniformLocation(pr, "nesOff"); g.nesSrc = glGetUniformLocation(pr, "nesSrc");
    g.boost = glGetUniformLocation(pr, "boost"); g.beam = glGetUniformLocation(pr, "beam");
    ok++;
  }
  // NTSC decoder (stand-alone shader).
  std::vector<uint8_t> nb;
  if (readFile("romfs:/shaders/ntsc.glsl", nb)) {
    std::string err, src(nb.begin(), nb.end());
    GLuint pr = program(VS, src.c_str(), err);
    if (err.empty()) {
      ntscProg = pr;
      nScreen = glGetUniformLocation(pr, "screen"); nColors = glGetUniformLocation(pr, "colors"); nInSize = glGetUniformLocation(pr, "inSize");
      nFrame = glGetUniformLocation(pr, "frameCount"); nDotCrawl = glGetUniformLocation(pr, "dotCrawl"); nLumaWin = glGetUniformLocation(pr, "lumaWin");
      nChromaWin = glGetUniformLocation(pr, "chromaWin"); nSvideo = glGetUniformLocation(pr, "svideo"); nDecode = glGetUniformLocation(pr, "decode");
      nGamma = glGetUniformLocation(pr, "gammaOut");
    } else { log += "ntsc.glsl: " + err.substr(0, 300) + "; "; glDeleteProgram(pr); }
  } else log += "ntsc.glsl: missing; ";
  return ok;
}
void pictureSetEffects(Picture* p, float cornerRadius, float cornerSoftness, float vignetteStrength, float vignetteStart, float curvature) {
  p->curve = curvature; p->cornerR = cornerRadius; p->cornerSoft = cornerSoftness; p->vigStrength = vignetteStrength; p->vigStart = vignetteStart;
}
void pictureDestroy(Picture* p) {
  if (!p) return;
  glDeleteTextures(1, &p->tex); glDeleteTextures(1, &p->rows); glDeleteTextures(1, &p->cols);
  if (p->colorTex) { glDeleteTextures(1, &p->colorTex); glDeleteTextures(1, &p->ntscTex); glDeleteFramebuffers(1, &p->fbo); }
  delete p;
}

/* ---------------- Images ---------------- */
struct Image { GLuint tex; int w, h; };
Image* imageCreate(int w, int h) {
  Image* im = new Image();
  im->w = w; im->h = h; im->tex = makeTexture(GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  return im;
}
void imageUpload(Image* im, const uint32_t* rgba) {
  glBindTexture(GL_TEXTURE_2D, im->tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, im->w, im->h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
void imageDraw(Image* im, float x, float y, float w, float h, float alpha) {
  x *= UI_SCALE; y *= UI_SCALE; w *= UI_SCALE; h *= UI_SCALE;
  glUseProgram(texProg); setScreen(tScreen);
  glUniform1i(tMode, 0); glUniform4f(tColor, 1, 1, 1, alpha);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, im->tex); glUniform1i(tTex, 0);
  quad(x, y, w, h, 0, 0, 1, 1);
}
void imageDestroy(Image* im) { if (im) { glDeleteTextures(1, &im->tex); delete im; } }

/* ---------------- Setup ---------------- */
bool init(std::string& error) {
  NWindow* win = nwindowGetDefault();
  nwindowSetDimensions(win, SCREEN_W, SCREEN_H);
  display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(display, NULL, NULL)) { error = "eglInitialize failed"; return false; }
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
  EGLConfig config; EGLint n = 0;
  if (!eglChooseConfig(display, cfgAttr, &config, 1, &n) || n == 0) { error = "eglChooseConfig failed"; return false; }
  surface = eglCreateWindowSurface(display, config, win, NULL);
  if (surface == EGL_NO_SURFACE) { error = "eglCreateWindowSurface failed"; return false; }
  const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttr);
  if (context == EGL_NO_CONTEXT) { error = "eglCreateContext failed"; return false; }
  eglMakeCurrent(display, surface, surface, context);
  eglSwapInterval(display, 1);

  rectProg = program(VS, RECT_FS, error);
  texProg = program(VS, TEX_FS, error);
  if (!error.empty()) return false;
  rScreen = glGetUniformLocation(rectProg, "screen"); rRect = glGetUniformLocation(rectProg, "rect");
  rRadius = glGetUniformLocation(rectProg, "radius"); rBorder = glGetUniformLocation(rectProg, "border");
  rFill = glGetUniformLocation(rectProg, "fill"); rBorderColor = glGetUniformLocation(rectProg, "bcolor");
  tScreen = glGetUniformLocation(texProg, "screen"); tTex = glGetUniformLocation(texProg, "tex");
  tMode = glGetUniformLocation(texProg, "mode"); tColor = glGetUniformLocation(texProg, "color");

  glGenVertexArrays(1, &vao); glBindVertexArray(vao);
  glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
  glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glViewport(0, 0, SCREEN_W, SCREEN_H);

  if (FT_Init_FreeType(&ft)) { error = "FreeType init failed"; return false; }
  const char* files[3] = {"romfs:/Roboto-Regular.ttf", "romfs:/Roboto-Bold.ttf", "romfs:/SourceCodePro-Regular.ttf"};
  for (int i = 0; i < 3; i++) {
    if (!readFile(files[i], fontData[i]) || FT_New_Memory_Face(ft, fontData[i].data(), (FT_Long)fontData[i].size(), 0, &faces[i])) {
      error = std::string("Font missing: ") + files[i]; return false;
    }
  }
  timerInit();
  atlas = makeTexture(GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS, ATLAS, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
  return true;
}

void shutdown() {
  for (int i = 0; i < 3; i++) if (faces[i]) FT_Done_Face(faces[i]);
  if (ft) FT_Done_FreeType(ft);
  if (display != EGL_NO_DISPLAY) {
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    eglTerminate(display);
  }
  display = EGL_NO_DISPLAY;
}

void beginFrame(Color c) {
  timerBegin();
  noClip();
  glClearColor(c.r, c.g, c.b, 1);
  glClear(GL_COLOR_BUFFER_BIT);
}
double swapMs = 0, waitMs = 0;
// GPU time per frame from GL_EXT_disjoint_timer_query (ring of queries read
// a few frames later); -1 when the driver does not offer it.
static PFNGLGENQUERIESEXTPROC qGen = 0; static PFNGLBEGINQUERYEXTPROC qBegin = 0; static PFNGLENDQUERYEXTPROC qEnd = 0;
static PFNGLGETQUERYOBJECTUIVEXTPROC qGetU = 0; static PFNGLGETQUERYOBJECTUI64VEXTPROC qGet64 = 0;
enum { NQ = 4 };
static GLuint queries[NQ]; static bool qActive[NQ]; static int qHead = 0; static bool timerOk = false, qOpen = false;
static double gpuEma = -1;
static void timerInit() {
  const char* ext = (const char*)glGetString(GL_EXTENSIONS);
  if (!ext || !strstr(ext, "GL_EXT_disjoint_timer_query")) return;
  qGen = (PFNGLGENQUERIESEXTPROC)eglGetProcAddress("glGenQueriesEXT"); qBegin = (PFNGLBEGINQUERYEXTPROC)eglGetProcAddress("glBeginQueryEXT");
  qEnd = (PFNGLENDQUERYEXTPROC)eglGetProcAddress("glEndQueryEXT"); qGetU = (PFNGLGETQUERYOBJECTUIVEXTPROC)eglGetProcAddress("glGetQueryObjectuivEXT");
  qGet64 = (PFNGLGETQUERYOBJECTUI64VEXTPROC)eglGetProcAddress("glGetQueryObjectui64vEXT");
  if (!qGen || !qBegin || !qEnd || !qGetU || !qGet64) return;
  qGen(NQ, queries); timerOk = true;
}
static void timerBegin() {
  if (!timerOk || qOpen) return;
  if (qActive[qHead]) {  // the oldest query: read it if done, else skip this frame
    GLuint avail = 0; qGetU(queries[qHead], GL_QUERY_RESULT_AVAILABLE_EXT, &avail);
    if (!avail) return;
    GLuint64 ns = 0; qGet64(queries[qHead], GL_QUERY_RESULT_EXT, &ns);
    double ms = ns / 1e6;
    gpuEma = gpuEma < 0 ? ms : gpuEma + (ms - gpuEma) * 0.1;
    qActive[qHead] = false;
  }
  qBegin(GL_TIME_ELAPSED_EXT, queries[qHead]); qOpen = true;
}
static void timerEnd() {
  if (!qOpen) return;
  qEnd(GL_TIME_ELAPSED_EXT); qOpen = false; qActive[qHead] = true; qHead = (qHead + 1) % NQ;
}
double gpuMs() { return gpuEma; }
bool gpuTimerAvailable() { return timerOk; }
static GLsync lastFence = 0;
static double msNow() { return armTicksToNs(armGetSystemTick()) / 1e6; }
// Saves what has been drawn so far this frame (the back buffer) as a PNG.
bool screenshot(const char* path) {
  std::vector<uint8_t> px((size_t)SCREEN_W * SCREEN_H * 4), flip((size_t)SCREEN_W * SCREEN_H * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, SCREEN_W, SCREEN_H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  for (int y = 0; y < SCREEN_H; y++) memcpy(&flip[(size_t)y * SCREEN_W * 4], &px[(size_t)(SCREEN_H - 1 - y) * SCREEN_W * 4], SCREEN_W * 4);
  for (size_t i = 3; i < flip.size(); i += 4) flip[i] = 255;
  png_image img; memset(&img, 0, sizeof img);
  img.version = PNG_IMAGE_VERSION; img.width = SCREEN_W; img.height = SCREEN_H; img.format = PNG_FORMAT_RGBA;
  return png_image_write_to_file(&img, path, 0, flip.data(), SCREEN_W * 4, NULL) != 0;
}
void endFrame(int queueMode) {
  frameCounter += 1;
  timerEnd();
  double t0 = msNow();
  eglSwapBuffers(display, surface);
  double t1 = msNow();
  swapMs = t1 - t0; waitMs = 0;
  if (lastFence) { glDeleteSync(lastFence); lastFence = 0; }
  if (queueMode == 1) lastFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  else if (queueMode == 2) { glFinish(); waitMs = msNow() - t1; }
}
void setHighRefresh(bool on) {
  NWindow* win = nwindowGetDefault();
  if (on) nwindowSetFrameRates(win, 0, 0, true);  // every refresh at the highest rate (120 Hz handheld), VRR allowed
  else { nwindowSetSwapInterval(win, 1); if (display != EGL_NO_DISPLAY) eglSwapInterval(display, 1); }
}
// The display's mode as VI2 reports it (current nominal refresh, VRR active,
// and whether its modes include 120 Hz). Returns false if it cannot be read.
bool displayInfo(unsigned& hz, bool& vrr, bool& can120) {
  Vi2Display* d = NULL;
  if (R_FAILED(nwindowGetVi2Display(nwindowGetDefault(), &d)) || !d) return false;
  u32 h = 0; bool v = false;
  if (R_FAILED(vi2QueryDisplayRefreshRate(d, &h, &v))) return false;
  Vi2DisplayModeCapability cap; memset(&cap, 0, sizeof cap);
  can120 = R_SUCCEEDED(vi2QueryDisplayModeCapability(d, &cap)) && (((cap.refresh_rates | cap.refresh_rates_secondary) & 2) != 0);
  hz = h; vrr = v;
  return true;
}
void waitPrevious() {
  if (!lastFence) return;
  double t0 = msNow();
  glClientWaitSync(lastFence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
  glDeleteSync(lastFence); lastFence = 0;
  waitMs += msNow() - t0;
}

}  // namespace gfx
