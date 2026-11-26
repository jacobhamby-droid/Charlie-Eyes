// Charlie core: eyes, animation, emotions, touch gestures, menu, software renderer.
// Hardware-free on purpose (no Arduino calls) so it can be compiled and rendered on a PC.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace ch {

// ============================================================================
// Basics
// ============================================================================
static const int SCR_W = 466, SCR_H = 466;
static const float CXF = 233.0f, CYF = 233.0f;
static const float PI_F = 3.14159265f;
static const float DEG = PI_F / 180.0f;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float sgnf(float v) { return v < 0 ? -1.0f : 1.0f; }
static inline float wrapPi(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}

static uint32_t rngState = 0x1234567u;
static inline uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}
static inline float frand() { return (rnd() & 0xFFFFFF) / 16777216.0f; }
static inline float frange(float a, float b) { return a + (b - a) * frand(); }

struct V2 { float x, y; };

// ============================================================================
// Settings (main.cpp saves these to flash when dirty)
// ============================================================================
struct Settings {
  uint8_t colorIdx = 0;
  int16_t orientOffset = 0;  // degrees, multiples of 90
  bool mirror = false;
  bool dirty = false;
};
static Settings settings;

struct Input {
  bool imu = false;
  float ax = 0, ay = 0, az = 0;
  bool touch = false;
  float tx = 0, ty = 0;
};

// Eye color presets. #0 (cyan) is Charlie's original color.
static const uint8_t PRESETS[8][3] = {
  {80, 220, 255},   // cyan (default)
  {120, 255, 160},  // mint
  {255, 110, 170},  // pink
  {255, 176, 48},   // amber
  {255, 70, 60},    // red
  {170, 120, 255},  // violet
  {255, 232, 90},   // yellow
  {235, 240, 255},  // white
};

// ============================================================================
// Pose channels, steps and animations
// ============================================================================
enum Ch {
  C_LX, C_LY,        // look offset (px)
  C_W, C_H,          // size scale (both eyes)
  C_TILT,            // degrees, + = angry (inner edge down), - = sad
  C_BL, C_BR,        // bend (arch) left / right eye, 0..~0.7
  C_SHEAR,           // slant
  C_N,               // squircle exponent
  C_HL, C_HR,        // extra height multiplier per eye
  C_WL, C_WR,        // extra width multiplier per eye
  C_YL, C_YR,        // extra vertical offset per eye (px)
  C_ROLL,            // whole-face roll (deg)
  C_GAP,             // extra spacing (px)
  C_COUNT
};
static const float NEUTRAL[C_COUNT] = {0, 0, 1, 1, 0, 0, 0, 0, 4, 1, 1, 1, 1, 0, 0, 0, 0};
static const float CH_LO[C_COUNT] = {-60, -50, 0.3f, 0.17f, -40, -0.6f, -0.6f, -0.5f, 2.6f, 0.17f, 0.17f, 0.3f, 0.3f, -40, -40, -45, -20};
static const float CH_HI[C_COUNT] = {60, 50, 1.9f, 2.0f, 40, 0.9f, 0.9f, 0.5f, 6.0f, 2.0f, 2.0f, 1.9f, 1.9f, 40, 40, 45, 40};
// (the table above is ordered by enum; BL/BR entries cover bend, SHEAR covers shear)

struct Step {
  float v[C_COUNT];
  uint16_t ms;
  Step(uint16_t m = 300) {
    memcpy(v, NEUTRAL, sizeof(v));
    ms = m;
  }
  Step& look(float x, float y) { v[C_LX] = x; v[C_LY] = y; return *this; }
  Step& size(float w, float h) { v[C_W] = w; v[C_H] = h; return *this; }
  Step& tilt(float d) { v[C_TILT] = d; return *this; }
  Step& bend(float b) { v[C_BL] = b; v[C_BR] = b; return *this; }
  Step& bends(float l, float r) { v[C_BL] = l; v[C_BR] = r; return *this; }
  Step& shear(float s) { v[C_SHEAR] = s; return *this; }
  Step& expo(float n) { v[C_N] = n; return *this; }
  Step& hts(float l, float r) { v[C_HL] = l; v[C_HR] = r; return *this; }
  Step& wds(float l, float r) { v[C_WL] = l; v[C_WR] = r; return *this; }
  Step& ys(float l, float r) { v[C_YL] = l; v[C_YR] = r; return *this; }
  Step& roll(float d) { v[C_ROLL] = d; return *this; }
  Step& gap(float g) { v[C_GAP] = g; return *this; }
};

enum Fx { FX_NONE = 0, FX_HEARTS = 1, FX_STARS = 2 };

static const int MAX_STEPS = 16;
struct Anim {
  const char* name = "";
  Step s[MAX_STEPS];
  uint8_t n = 0;
  float k = 260, z = 0.55f;  // spring stiffness / damping ratio for this animation
  uint8_t pri = 1;           // higher interrupts lower
  uint8_t fx = FX_NONE;
  bool loop = false;
  Anim& add(const Step& st) {
    if (n < MAX_STEPS) s[n++] = st;
    return *this;
  }
};

enum AnimId {
  AN_BOOT, AN_POKE_SURPRISE, AN_POKE_GIGGLE, AN_POKE_ANNOYED, AN_ANGRY, AN_SHAKE_NO, AN_NOD_YES,
  AN_DIZZY, AN_DIZZY_RECOVER, AN_SCARED, AN_WAKE_STARTLE, AN_WAKE_NATURAL, AN_SLEEP_NOD, AN_PET_END,
  AN_LOOK_AROUND, AN_CURIOUS, AN_THINK, AN_PEEK_SHY, AN_DOUBLE_TAKE, AN_EYE_ROLL, AN_YAWN, AN_STRETCH,
  AN_SIGH, AN_SAD_DROOP, AN_SQUINT_SUS, AN_SMUG, AN_WIGGLE, AN_HAPPY_BOUNCE, AN_EXCITED, AN_WINK_L,
  AN_WINK_R, AN_LOVE, AN_SNEEZE, AN_CONFUSED,
  AN_COUNT
};

static Anim ANIMS[AN_COUNT];

static Anim& def(int id, const char* name, float k, float z, uint8_t pri, uint8_t fx = FX_NONE, bool loop = false) {
  Anim& a = ANIMS[id];
  a.name = name; a.k = k; a.z = z; a.pri = pri; a.fx = fx; a.loop = loop; a.n = 0;
  return a;
}

static void buildAnims() {
  // ---- boot: wake up and look around
  def(AN_BOOT, "boot", 260, 0.55f, 5)
    .add(Step(500).size(1.05f, 0.05f))
    .add(Step(170).size(0.88f, 1.38f).look(0, -8))
    .add(Step(150).size(1.1f, 0.88f))
    .add(Step(260))
    .add(Step(260).look(-26, 0))
    .add(Step(300).look(26, -4))
    .add(Step(300));

  // ---- reactions
  def(AN_POKE_SURPRISE, "poke_surprise", 420, 0.42f, 3)
    .add(Step(75).size(0.78f, 1.38f).look(0, -8))
    .add(Step(130).size(1.12f, 0.85f))
    .add(Step(190).size(0.94f, 1.1f))
    .add(Step(450));

  {
    Anim& a = def(AN_POKE_GIGGLE, "poke_giggle", 380, 0.5f, 3);
    for (int i = 0; i < 3; i++) {
      a.add(Step(95).size(1.1f, 0.7f).bend(0.5f).look(0, 7));
      a.add(Step(95).size(0.94f, 1.12f).bend(0.32f).look(0, -11));
    }
    a.add(Step(200).size(1.05f, 0.85f).bend(0.4f));
    a.add(Step(350));
  }

  def(AN_POKE_ANNOYED, "poke_annoyed", 300, 0.7f, 3)
    .add(Step(140).size(1.05f, 0.62f).tilt(14).look(-26, 3))
    .add(Step(450).size(1.05f, 0.62f).tilt(14).look(-26, 3))
    .add(Step(260).size(1.03f, 0.72f).tilt(10).look(0, 2))
    .add(Step(300).size(1, 0.9f).tilt(4));

  {
    Anim& a = def(AN_ANGRY, "angry", 340, 0.6f, 3);
    a.add(Step(150).size(1.08f, 0.58f).tilt(22).look(0, 3));
    for (int i = 0; i < 5; i++) a.add(Step(55).size(1.08f, 0.58f).tilt(22).look((i & 1) ? -3.5f : 3.5f, 3));
    a.add(Step(500).size(1.04f, 0.7f).tilt(16).look(0, 2));
    a.add(Step(300).size(1, 0.9f).tilt(6));
  }

  def(AN_SHAKE_NO, "shake_no", 380, 0.6f, 3)
    .add(Step(130).look(-30, 0).tilt(10).size(1, 0.78f))
    .add(Step(130).look(30, 0).tilt(10).size(1, 0.78f))
    .add(Step(130).look(-30, 0).tilt(10).size(1, 0.78f))
    .add(Step(130).look(30, 0).tilt(10).size(1, 0.78f))
    .add(Step(220).tilt(6).size(1, 0.88f))
    .add(Step(250));

  def(AN_NOD_YES, "nod_yes", 380, 0.55f, 2)
    .add(Step(150).look(0, 12).size(1, 0.82f))
    .add(Step(150).look(0, -8).size(1, 1.12f))
    .add(Step(150).look(0, 12).size(1, 0.82f))
    .add(Step(150).look(0, -6).size(1, 1.08f))
    .add(Step(300));

  {
    Anim& a = def(AN_DIZZY, "dizzy", 150, 0.6f, 4, FX_STARS, true);
    for (int i = 0; i < 8; i++) {
      float ang = i * 45.0f * DEG;
      a.add(Step(165).look(cosf(ang) * 20, sinf(ang) * 20)
                .hts((i & 1) ? 1.25f : 0.72f, (i & 1) ? 0.72f : 1.25f)
                .roll((i & 1) ? 9 : -9).size(1, 0.95f));
    }
  }

  def(AN_DIZZY_RECOVER, "dizzy_recover", 200, 0.7f, 3)
    .add(Step(400).size(0.92f, 0.55f).tilt(-6).look(0, 8))
    .add(Step(350).size(1, 0.8f).look(-10, 4))
    .add(Step(400));

  {
    Anim& a = def(AN_SCARED, "scared", 480, 0.4f, 4);
    a.add(Step(80).size(0.7f, 1.4f).gap(10).look(0, -5));
    for (int i = 0; i < 6; i++) a.add(Step(50).size(0.7f, 1.4f).gap(10).look((i & 1) ? -2.5f : 2.5f, -5));
    a.add(Step(450).size(0.74f, 1.3f).gap(8).look(0, -3));
    a.add(Step(400).size(0.95f, 1.1f).gap(3));
  }

  def(AN_WAKE_STARTLE, "wake_startle", 400, 0.45f, 5)
    .add(Step(90).size(0.78f, 1.4f).look(0, -10))
    .add(Step(150).size(1.1f, 0.8f).tilt(8))
    .add(Step(500).size(1.04f, 0.8f).tilt(10).look(0, 2))
    .add(Step(350).size(1, 1).tilt(2));

  def(AN_WAKE_NATURAL, "wake_natural", 170, 0.8f, 4)
    .add(Step(700).size(1.05f, 0.25f).bend(0.3f))
    .add(Step(350).size(1.0f, 0.45f).bend(0.15f))
    .add(Step(400).size(1, 0.85f))
    .add(Step(300).look(-20, -8))
    .add(Step(350));

  def(AN_SLEEP_NOD, "sleep_nod", 120, 0.9f, 4)
    .add(Step(450).size(1, 0.7f).look(0, 6))
    .add(Step(550).size(1, 0.34f).look(0, 10))
    .add(Step(300).size(1, 0.65f))
    .add(Step(550).size(1, 0.3f).look(0, 10))
    .add(Step(700).size(1.04f, 0.1f).bend(0.05f).look(0, 12));

  def(AN_PET_END, "pet_end", 260, 0.55f, 3)
    .add(Step(220).size(1.1f, 0.8f).bend(0.5f))
    .add(Step(260).size(0.96f, 1.12f).bend(0.1f).look(0, -5))
    .add(Step(380).size(1.03f, 0.9f).bend(0.3f))
    .add(Step(300));

  // ---- autonomous / idle
  def(AN_LOOK_AROUND, "look_around", 200, 0.75f, 1)
    .add(Step(250).look(-34, -4))
    .add(Step(500).look(-34, -4))
    .add(Step(220).look(34, -4))
    .add(Step(600).look(34, -4))
    .add(Step(220).look(0, -26))
    .add(Step(450).look(0, -26))
    .add(Step(250));

  def(AN_CURIOUS, "curious", 220, 0.7f, 1)
    .add(Step(300).roll(8).hts(1.15f, 0.86f).look(22, -4))
    .add(Step(1000).roll(8).hts(1.15f, 0.86f).look(22, -4))
    .add(Step(260).roll(-6).hts(0.9f, 1.12f).look(-22, -4))
    .add(Step(900).roll(-6).hts(0.9f, 1.12f).look(-22, -4))
    .add(Step(320));

  def(AN_THINK, "think", 200, 0.75f, 1)
    .add(Step(320).look(-26, -22).size(1, 0.92f).hts(0.78f, 1.1f).tilt(5))
    .add(Step(950).look(-26, -22).size(1, 0.92f).hts(0.78f, 1.1f).tilt(5))
    .add(Step(220).look(-22, -26).size(1, 0.92f).hts(0.78f, 1.1f).tilt(5))
    .add(Step(550).look(-22, -26).size(1, 0.92f).hts(0.78f, 1.1f).tilt(5))
    .add(Step(260).size(1, 1.12f))
    .add(Step(350));

  def(AN_PEEK_SHY, "peek_shy", 220, 0.65f, 1)
    .add(Step(220).look(32, 8).size(0.82f, 0.82f).tilt(-7))
    .add(Step(800).look(32, 8).size(0.82f, 0.82f).tilt(-7))
    .add(Step(170))
    .add(Step(150))
    .add(Step(210).look(34, 8).size(0.8f, 0.8f).tilt(-7))
    .add(Step(600).look(34, 8).size(0.8f, 0.8f).tilt(-7))
    .add(Step(320));

  def(AN_DOUBLE_TAKE, "double_take", 340, 0.55f, 1)
    .add(Step(300).look(26, 0))
    .add(Step(280).look(26, 0))
    .add(Step(130))
    .add(Step(80).size(0.8f, 1.36f).look(0, -4))
    .add(Step(320).size(0.8f, 1.36f).look(0, -4))
    .add(Step(320));

  {
    Anim& a = def(AN_EYE_ROLL, "eye_roll", 220, 0.7f, 1);
    for (int i = 0; i < 8; i++) {
      float ang = (-90.0f + i * 45.0f) * DEG;
      a.add(Step(125).look(cosf(ang) * 28, sinf(ang) * 28).size(1, 0.92f));
    }
    a.add(Step(220).size(1, 0.85f).tilt(-4));
    a.add(Step(300));
  }

  def(AN_YAWN, "yawn", 170, 0.8f, 1)
    .add(Step(500).size(0.96f, 0.72f).bend(0.1f).tilt(-4).look(0, 2))
    .add(Step(550).size(1.04f, 0.26f).bend(0.38f).look(0, -6))
    .add(Step(550).size(1.04f, 0.26f).bend(0.38f).look(0, -6))
    .add(Step(420).size(1, 0.6f).look(0, 4))
    .add(Step(320));

  def(AN_STRETCH, "stretch", 200, 0.65f, 1)
    .add(Step(300).size(0.86f, 1.26f).look(0, -14))
    .add(Step(520).size(0.84f, 1.3f).look(0, -18))
    .add(Step(200).size(1.16f, 0.8f).look(0, 6))
    .add(Step(400));

  def(AN_SIGH, "sigh", 170, 0.8f, 1)
    .add(Step(600).size(1.03f, 0.92f).look(0, -6))
    .add(Step(550).size(0.98f, 0.74f).tilt(-5).look(0, 10))
    .add(Step(500));

  def(AN_SAD_DROOP, "sad_droop", 160, 0.8f, 1)
    .add(Step(500).size(0.95f, 0.82f).tilt(-15).look(0, 12))
    .add(Step(1400).size(0.95f, 0.82f).tilt(-15).look(0, 12))
    .add(Step(420).size(0.95f, 0.82f).tilt(-15).look(-10, 14))
    .add(Step(850).size(0.95f, 0.82f).tilt(-15).look(10, 14))
    .add(Step(450));

  def(AN_SQUINT_SUS, "squint_suspicious", 240, 0.75f, 1)
    .add(Step(320).size(1.06f, 0.46f).tilt(10).look(-20, 0))
    .add(Step(900).size(1.06f, 0.46f).tilt(10).look(-20, 0))
    .add(Step(260).size(1.06f, 0.46f).tilt(10).look(20, 0))
    .add(Step(650).size(1.06f, 0.46f).tilt(10).look(20, 0))
    .add(Step(300));

  def(AN_SMUG, "smug", 200, 0.8f, 1)
    .add(Step(350).size(1.05f, 0.62f).bend(0.28f).tilt(-3).look(0, -4))
    .add(Step(1300).size(1.05f, 0.62f).bend(0.28f).tilt(-3).look(0, -4))
    .add(Step(320));

  {
    Anim& a = def(AN_WIGGLE, "wiggle", 300, 0.5f, 2);
    for (int i = 0; i < 6; i++) a.add(Step(125).roll((i & 1) ? -11 : 11).bend(0.42f).size(1.06f, 0.8f));
    a.add(Step(260).size(1.04f, 0.9f).bend(0.3f));
    a.add(Step(300));
  }

  def(AN_HAPPY_BOUNCE, "happy_bounce", 360, 0.5f, 2)
    .add(Step(140).size(1.12f, 0.7f).bend(0.3f).look(0, 8))
    .add(Step(130).size(0.88f, 1.25f).look(0, -16).bend(0.1f))
    .add(Step(130).size(1.1f, 0.75f).look(0, 8).bend(0.3f))
    .add(Step(130).size(0.9f, 1.2f).look(0, -12))
    .add(Step(250).size(1.05f, 0.9f).bend(0.3f))
    .add(Step(300));

  {
    Anim& a = def(AN_EXCITED, "excited", 450, 0.4f, 2);
    a.add(Step(120).size(1.15f, 1.25f).gap(8));
    for (int i = 0; i < 6; i++) a.add(Step(70).size(1.15f, 1.25f).gap(8).look((i & 1) ? -2.5f : 2.5f, -2));
    a.add(Step(130).size(1.12f, 0.75f).bend(0.3f).look(0, 8));
    a.add(Step(130).size(0.9f, 1.25f).look(0, -16));
    a.add(Step(200).size(1.05f, 0.9f));
    a.add(Step(300));
  }

  def(AN_WINK_L, "wink_left", 420, 0.55f, 2)
    .add(Step(120).hts(0.1f, 1.05f).bends(0.38f, 0.1f).wds(1.12f, 1.0f))
    .add(Step(500).hts(0.1f, 1.05f).bends(0.38f, 0.1f).wds(1.12f, 1.0f))
    .add(Step(170))
    .add(Step(200));

  def(AN_WINK_R, "wink_right", 420, 0.55f, 2)
    .add(Step(120).hts(1.05f, 0.1f).bends(0.1f, 0.38f).wds(1.0f, 1.12f))
    .add(Step(500).hts(1.05f, 0.1f).bends(0.1f, 0.38f).wds(1.0f, 1.12f))
    .add(Step(170))
    .add(Step(200));

  {
    Anim& a = def(AN_LOVE, "love", 300, 0.5f, 3, FX_HEARTS);
    a.add(Step(300).size(1.1f, 0.9f).bend(0.45f));
    for (int i = 0; i < 3; i++) {
      a.add(Step(140).size(1.2f, 0.82f).bend(0.55f));
      a.add(Step(160).size(1.08f, 0.92f).bend(0.45f));
    }
    a.add(Step(400).size(1.05f, 0.9f).bend(0.35f));
    a.add(Step(300));
  }

  def(AN_SNEEZE, "sneeze", 380, 0.4f, 2)
    .add(Step(260).size(0.96f, 0.82f).look(0, -6))
    .add(Step(300).size(0.92f, 0.6f).look(0, -14))
    .add(Step(150).size(1.1f, 0.1f).bend(0.3f).look(0, 10))
    .add(Step(90).size(0.74f, 1.42f).look(0, -12))
    .add(Step(140).size(1.15f, 0.84f))
    .add(Step(320));

  def(AN_CONFUSED, "confused", 240, 0.7f, 1)
    .add(Step(260).hts(1.3f, 0.76f).roll(6).shear(0.1f))
    .add(Step(750).hts(1.3f, 0.76f).roll(6).shear(0.1f))
    .add(Step(300).hts(0.8f, 1.22f).roll(-6).shear(-0.1f).look(8, -4))
    .add(Step(750).hts(0.8f, 1.22f).roll(-6).shear(-0.1f).look(8, -4))
    .add(Step(320));
}

// ============================================================================
// Spring-driven pose
// ============================================================================
struct Spr { float x, v, t; };
static Spr sp[C_COUNT];
static float springK = 260, springZ = 0.55f;
static float physAcc = 0;

static void physics(float dt) {
  const float SUB = 1.0f / 120.0f;
  physAcc += dt;
  int guard = 0;
  while (physAcc >= SUB && guard++ < 10) {
    physAcc -= SUB;
    for (int c = 0; c < C_COUNT; c++) {
      float k = springK;
      float z = springZ;
      if (c == C_N) z = 1.0f;
      if (c == C_LX || c == C_LY) k *= 1.1f;
      float d = 2.0f * z * sqrtf(k);
      Spr& s = sp[c];
      s.v += (k * (s.t - s.x) - d * s.v) * SUB;
      s.x += s.v * SUB;
      s.x = clampf(s.x, CH_LO[c], CH_HI[c]);
    }
  }
  if (physAcc > 0.1f) physAcc = 0;
}

// ============================================================================
// Mind: emotions come from interaction
// ============================================================================
struct Mind {
  float joy = 0.62f, energy = 1.0f, annoy = 0.0f, love = 0.15f, bored = 0.0f, dizzy = 0.0f;
};
static Mind mind;
static uint32_t gNow = 0;
static uint32_t lastInteract = 0;
static bool sleeping = false, fallingAsleep = false;
static uint32_t sleepStart = 0;

// ============================================================================
// Animation player
// ============================================================================
struct Player { bool active = false; int id = -1; int idx = 0; uint32_t stepEnd = 0; uint32_t loopUntil = 0; };
static Player cur;
static bool dizzyFx = false;

struct Part {
  bool on; uint8_t type; float x, y, vx, vy, rot, vr, size, life, max, phase, orbit;
};
static Part parts[14];

static Part* allocPart() {
  for (int i = 0; i < 14; i++) if (!parts[i].on) return &parts[i];
  return nullptr;
}
static void spawnHeart() {
  Part* p = allocPart();
  if (!p) return;
  Part q; memset(&q, 0, sizeof(q));
  q.on = true; q.type = 0;
  q.x = CXF + frange(-120, 120); q.y = CYF - frange(70, 125);
  q.vx = frange(-8, 8); q.vy = frange(-55, -35);
  q.size = frange(13, 24); q.max = frange(1.6f, 2.4f); q.phase = frand() * 6.28f;
  *p = q;
}
static void spawnZ() {
  Part* p = allocPart();
  if (!p) return;
  Part q; memset(&q, 0, sizeof(q));
  q.on = true; q.type = 1;
  q.x = CXF + 85; q.y = CYF - 55; q.vx = 11; q.vy = -22;
  q.size = 9; q.max = 3.4f;
  *p = q;
}
static void spawnStars() {
  for (int i = 0; i < 4; i++) {
    Part* p = allocPart();
    if (!p) return;
    Part q; memset(&q, 0, sizeof(q));
    q.on = true; q.type = 2;
    q.phase = i * (PI_F / 2); q.size = 11; q.max = 30.0f; q.orbit = 1;
    *p = q;
  }
}

static bool playAnim(int id, uint32_t now, uint32_t loopUntil = 0) {
  const Anim& A = ANIMS[id];
  if (cur.active && A.pri < ANIMS[cur.id].pri) return false;
  cur.active = true; cur.id = id; cur.idx = 0;
  cur.stepEnd = now + A.s[0].ms; cur.loopUntil = loopUntil;
  if (A.fx == FX_HEARTS) for (int i = 0; i < 4; i++) spawnHeart();
  if (A.fx == FX_STARS) { dizzyFx = true; spawnStars(); }
  return true;
}

static void onAnimEnd(int id) {
  if (id == AN_SLEEP_NOD) { sleeping = true; fallingAsleep = false; sleepStart = gNow; }
  if (id == AN_DIZZY) dizzyFx = false;
}

static void animTick(uint32_t now) {
  if (!cur.active) return;
  const Anim& A = ANIMS[cur.id];
  int guard = 0;
  while (now >= cur.stepEnd && guard++ < 8) {
    cur.idx++;
    if (cur.idx >= A.n) {
      if (A.loop && now < cur.loopUntil) {
        cur.idx = 0;
      } else {
        onAnimEnd(cur.id);
        cur.active = false;
        return;
      }
    }
    cur.stepEnd += A.s[cur.idx].ms;
    if (cur.stepEnd < now) cur.stepEnd = now + A.s[cur.idx].ms;
  }
}

// ============================================================================
// Orientation + IMU
// ============================================================================
static float gAlpha = 0;          // drawing rotation (rad) so Charlie stays upright
static float lpx = 1, lpy = 0;    // low-passed gravity direction (sensor frame)
static float gRef = 0;
static bool shaking = false;
static uint32_t lastShakeT = 0, lastHitT = 0, hitT[6];
static int nHits = 0, freeCount = 0;
static uint32_t lastFall = 0;

static void onShakeStart(uint32_t now);
static void onShakeEnd(uint32_t now);
static void onFall(uint32_t now);

static void imuUpdate(uint32_t now, float dt, float ax, float ay, float az) {
  if (gRef <= 0) {
    float m0 = sqrtf(ax * ax + ay * ay + az * az);
    gRef = (m0 > 4.0f) ? 9.81f : 1.0f;
  }
  ax /= gRef; ay /= gRef; az /= gRef;
  float m = sqrtf(ax * ax + ay * ay + az * az);
  float dev = fabsf(m - 1.0f);

  // shake detection: several strong spikes in a short window
  if (dev > 0.7f && now - lastHitT > 90) {
    lastHitT = now;
    if (nHits < 6) hitT[nHits++] = now;
    else { memmove(hitT, hitT + 1, 5 * sizeof(uint32_t)); hitT[5] = now; }
  }
  int recent = 0;
  for (int i = 0; i < nHits; i++) if (now - hitT[i] < 900) recent++;
  if (recent >= 3) {
    lastShakeT = now;
    if (!shaking) { shaking = true; onShakeStart(now); }
  }
  if (shaking && now - lastShakeT > 700) { shaking = false; onShakeEnd(now); }

  // free fall
  if (m < 0.22f && !shaking) freeCount++; else freeCount = 0;
  if (freeCount >= 4 && now - lastFall > 2500) { lastFall = now; freeCount = 0; onFall(now); }

  // orientation: follow gravity unless shaking or lying flat
  if (!shaking && dev < 0.35f) {
    float k = clampf(dt * 6.0f, 0, 1);
    lpx += (ax - lpx) * k;
    lpy += (ay - lpy) * k;
  }
  float mag = sqrtf(lpx * lpx + lpy * lpy);
  if (mag > 0.4f) {
    // Waveshare maps: sensor +X = screen up, sensor +Y = screen right  =>  alpha = atan2(ay, ax)
    float target = atan2f(lpy, lpx);
    if (settings.mirror) target = -target;
    target += settings.orientOffset * DEG;
    gAlpha += wrapPi(target - gAlpha) * clampf(dt * 7.0f, 0, 1);
    gAlpha = wrapPi(gAlpha);
  }
}

// ============================================================================
// Touch gestures, menu
// ============================================================================
struct TouchTrack {
  bool down; uint32_t t0, lastMove, lastRevT;
  float sx, sy, x, y, path, exX, exY;
  int dirX, dirY, rev;
  bool consumed, petting;
};
static TouchTrack T;
static bool menuOpen = false;
static float menuP = 0, menuV = 0;
static uint32_t menuT0 = 0;
static uint32_t lastHeartT = 0;
static uint32_t lastPokeT = 0;
static int pokeStreak = 0;
static uint32_t lookAtUntil = 0;
static float lookAtX = 0, lookAtY = 0;
static uint32_t petStart = 0;

static const float MENU_RING = 168.0f;

static void toUpright(float rx, float ry, float& ux, float& uy) {
  float dx = rx - CXF, dy = ry - CYF;
  float ca = cosf(gAlpha), sa = sinf(gAlpha);
  ux = dx * ca + dy * sa;
  uy = -dx * sa + dy * ca;
}

static void wakeUp(uint32_t now, bool startled) {
  sleeping = false; fallingAsleep = false;
  mind.energy = fmaxf(mind.energy, 0.5f);
  cur.active = false;
  playAnim(startled ? AN_WAKE_STARTLE : AN_WAKE_NATURAL, now);
  if (startled) mind.annoy = clampf(mind.annoy + 0.12f, 0, 1);
}

static void onShakeStart(uint32_t now) {
  lastInteract = now;
  if (sleeping) { sleeping = false; fallingAsleep = false; mind.energy = fmaxf(mind.energy, 0.5f); }
  cur.active = false;
  mind.dizzy = 1.0f;
  playAnim(AN_DIZZY, now, now + 900);
}
static void onShakeEnd(uint32_t now) {
  lastInteract = now;
  mind.annoy = clampf(mind.annoy + 0.2f, 0, 1);
  mind.joy = clampf(mind.joy - 0.12f, 0, 1);
  cur.active = false;
  dizzyFx = false;
  playAnim(AN_DIZZY_RECOVER, now);
}
static void onFall(uint32_t now) {
  lastInteract = now;
  if (sleeping) { sleeping = false; fallingAsleep = false; }
  cur.active = false;
  mind.annoy = clampf(mind.annoy + 0.1f, 0, 1);
  playAnim(AN_SCARED, now);
}

static void onPoke(uint32_t now, float ux, float uy) {
  lastInteract = now;
  mind.energy = fminf(1.0f, mind.energy + 0.03f);
  if (now - lastPokeT < 2200) pokeStreak++; else pokeStreak = 1;
  lastPokeT = now;
  lookAtUntil = now + 900;
  lookAtX = clampf(ux * 0.22f, -34, 34);
  lookAtY = clampf(uy * 0.2f, -24, 24);

  if (pokeStreak >= 5) {
    mind.annoy = clampf(mind.annoy + 0.25f, 0, 1);
    mind.joy = clampf(mind.joy - 0.1f, 0, 1);
    playAnim(AN_ANGRY, now);
  } else if (pokeStreak >= 3) {
    mind.annoy = clampf(mind.annoy + 0.15f, 0, 1);
    playAnim((rnd() & 1) ? AN_SHAKE_NO : AN_POKE_ANNOYED, now);
  } else if (mind.annoy > 0.45f) {
    mind.annoy = clampf(mind.annoy + 0.05f, 0, 1);
    playAnim(AN_POKE_ANNOYED, now);
  } else {
    mind.joy = clampf(mind.joy + 0.04f, 0, 1);
    if (pokeStreak == 2 && mind.joy > 0.5f) playAnim(AN_POKE_GIGGLE, now);
    else playAnim(AN_POKE_SURPRISE, now);
  }
}

static void onPetStart(uint32_t now) { petStart = now; lastHeartT = now; lastInteract = now; }
static void onPetEnd(uint32_t now) {
  lastInteract = now;
  if (now - petStart > 900) {
    if (mind.love > 0.55f) playAnim(AN_LOVE, now);
    else playAnim(AN_PET_END, now);
  }
}

static void openMenu(uint32_t now) { menuOpen = true; menuT0 = now; cur.active = false; }
static void closeMenu() { menuOpen = false; }

static bool menuHit(float ux, float uy) {
  // returns true if a swatch was chosen
  for (int k = 0; k < 8; k++) {
    float ang = k * 45.0f * DEG;
    float cx = MENU_RING * sinf(ang), cy = -MENU_RING * cosf(ang);
    if (hypotf(ux - cx, uy - cy) < 46.0f) {
      settings.colorIdx = k; settings.dirty = true;
      playAnim(AN_NOD_YES, gNow);
      return true;
    }
  }
  return false;
}

static void axisTrack(float p, float& ex, int& dir, int& rev, uint32_t now, uint32_t& lastRevT) {
  if (dir == 0) {
    if (fabsf(p - ex) > 26) { dir = p > ex ? 1 : -1; ex = p; }
  } else if (dir == 1) {
    if (p > ex) ex = p;
    else if (ex - p > 26) { dir = -1; ex = p; rev++; lastRevT = now; }
  } else {
    if (p < ex) ex = p;
    else if (p - ex > 26) { dir = 1; ex = p; rev++; lastRevT = now; }
  }
}

static void orientStep();

static void touchUpdate(uint32_t now, bool down, float rx, float ry) {
  float ux = 0, uy = 0;
  if (down) toUpright(rx, ry, ux, uy);

  if (down && !T.down) {  // finger down
    memset(&T, 0, sizeof(T));
    T.down = true; T.t0 = now; T.lastMove = now;
    T.sx = T.x = ux; T.sy = T.y = uy; T.exX = ux; T.exY = uy;
    lastInteract = now;
    if (sleeping && !menuOpen) { wakeUp(now, true); T.consumed = true; }
  } else if (down && T.down) {  // finger moving
    float d = hypotf(ux - T.x, uy - T.y);
    if (d > 1.5f) T.lastMove = now;
    T.path += d; T.x = ux; T.y = uy;
    axisTrack(ux, T.exX, T.dirX, T.rev, now, T.lastRevT);
    axisTrack(uy, T.exY, T.dirY, T.rev, now, T.lastRevT);
    lastInteract = now;

    // hold the middle of the open menu for ~1.2 s: turn Charlie's 'up' by 90 degrees
    // (fixes the orientation by touch if the sensor axes are mapped differently)
    if (!T.consumed && menuOpen && now - T.t0 > 1200 && T.path < 30 && hypotf(T.x, T.y) < 70) {
      orientStep(); T.consumed = true; menuT0 = now;
      playAnim(AN_CONFUSED, now);
    }

    if (!T.consumed) {
      float dy = uy - T.sy, dx = ux - T.sx;
      if (!menuOpen && T.sy < -105 && dy > 70 && fabsf(dx) < dy * 0.9f) {
        openMenu(now); T.consumed = true;
      } else if (menuOpen && -dy > 70 && fabsf(dx) < -dy * 0.9f) {
        closeMenu(); T.consumed = true;
      }
    }
  } else if (!down && T.down) {  // finger up
    bool wasPet = T.petting;
    if (T.petting) { T.petting = false; onPetEnd(now); }
    if (!T.consumed && !wasPet && (now - T.t0) < 350 && T.path < 28) {
      if (menuOpen) {
        menuT0 = now;
        if (!menuHit(T.x, T.y) && hypotf(T.x, T.y) < 80) closeMenu();
      } else {
        onPoke(now, T.x, T.y);
      }
    }
    T.down = false;
  }

  // petting detection
  if (T.down && !menuOpen && !T.consumed) {
    bool want = false;
    if (T.rev >= 2 && now - T.lastRevT < 800) want = true;
    else if (now - T.t0 > 700 && T.path < 30) want = true;  // resting hand
    else if (T.petting && now - T.lastMove < 500 && T.path > 120) want = true;
    if (want && !T.petting) { T.petting = true; onPetStart(now); }
    if (!want && T.petting) { T.petting = false; onPetEnd(now); }
  }
}

// ============================================================================
// Idle director + base pose
// ============================================================================
static uint32_t nextIdle = 4000, nextBlink = 2500, blinkT0 = 0, nextSaccade = 1500;
static float gazeX = 0, gazeY = 0;
static bool demoMode = false;
static int demoIdx = 0;
static uint32_t demoNext = 0;
static int lastIdle = -1;
static uint32_t lastZ = 0;

static bool petting() { return T.down && T.petting; }

static void baseTarget(float* t) {
  memcpy(t, NEUTRAL, sizeof(NEUTRAL));
  float sad = clampf((0.42f - mind.joy) / 0.42f, 0, 1);
  float hap = clampf((mind.joy - 0.62f) / 0.38f, 0, 1);
  float slp = clampf((0.55f - mind.energy) / 0.45f, 0, 1);
  t[C_H] = 1 - 0.38f * slp - 0.10f * hap - 0.10f * sad - 0.06f * mind.annoy;
  t[C_W] = 1 + 0.05f * hap + 0.03f * slp + 0.03f * mind.annoy;
  t[C_TILT] = 16 * mind.annoy - 12 * sad - 6 * slp;
  t[C_BL] = t[C_BR] = 0.42f * hap;
  t[C_LY] = 8 * slp + 10 * sad;
  t[C_N] = 4.0f - 0.2f * hap + 0.2f * mind.annoy;
  if (sleeping) {
    t[C_H] = 0.1f; t[C_W] = 1.04f; t[C_BL] = t[C_BR] = 0.05f; t[C_LY] = 12; t[C_TILT] = 0;
  }
}

static void petTarget(float* t) {
  memcpy(t, NEUTRAL, sizeof(NEUTRAL));
  t[C_W] = 1.1f; t[C_H] = 0.55f; t[C_BL] = t[C_BR] = 0.6f; t[C_TILT] = -3; t[C_GAP] = 4;
  t[C_LX] = clampf(T.x * 0.2f, -26, 26);
  t[C_LY] = clampf(T.y * 0.12f, -12, 12) + 4;
}

static float weightOf(int id) {
  float sad = clampf((0.42f - mind.joy) / 0.42f, 0, 1);
  float hap = clampf((mind.joy - 0.62f) / 0.38f, 0, 1);
  float slp = clampf((0.55f - mind.energy) / 0.45f, 0, 1);
  float b = mind.bored;
  switch (id) {
    case AN_LOOK_AROUND: return 3 + 3 * b;
    case AN_CURIOUS: return 2 + hap;
    case AN_THINK: return 1.5f;
    case AN_PEEK_SHY: return 1 + 2 * mind.love;
    case AN_DOUBLE_TAKE: return 1;
    case AN_EYE_ROLL: return 0.3f + 3 * b + 2 * mind.annoy;
    case AN_YAWN: return 0.2f + 4 * slp + 2 * b;
    case AN_STRETCH: return 0.3f + 1.5f * b + slp;
    case AN_SIGH: return 0.3f + 2.5f * b + 3 * sad;
    case AN_SAD_DROOP: return 3 * sad;
    case AN_SQUINT_SUS: return 1 + 3 * mind.annoy;
    case AN_SMUG: return 2 * hap;
    case AN_WIGGLE: return 3 * hap;
    case AN_HAPPY_BOUNCE: return 3 * hap * (0.4f + mind.energy);
    case AN_EXCITED: return 1.5f * hap * mind.energy;
    case AN_WINK_L: return 1.5f * hap;
    case AN_WINK_R: return 1.5f * hap;
    case AN_LOVE: return (mind.joy > 0.6f) ? 4 * mind.love : 0;
    case AN_SNEEZE: return 0.4f;
    case AN_CONFUSED: return 0.8f;
    case AN_NOD_YES: return hap;
    case AN_SHAKE_NO: return 2 * mind.annoy;
    default: return 0;
  }
}

static void pickIdle(uint32_t now) {
  static const int IDLE[] = {AN_LOOK_AROUND, AN_CURIOUS, AN_THINK, AN_PEEK_SHY, AN_DOUBLE_TAKE, AN_EYE_ROLL,
                             AN_YAWN, AN_STRETCH, AN_SIGH, AN_SAD_DROOP, AN_SQUINT_SUS, AN_SMUG, AN_WIGGLE,
                             AN_HAPPY_BOUNCE, AN_EXCITED, AN_WINK_L, AN_WINK_R, AN_LOVE, AN_SNEEZE,
                             AN_CONFUSED, AN_NOD_YES, AN_SHAKE_NO};
  const int N = sizeof(IDLE) / sizeof(IDLE[0]);
  float w[N]; float tot = 0;
  for (int i = 0; i < N; i++) {
    w[i] = (IDLE[i] == lastIdle) ? 0 : weightOf(IDLE[i]);
    tot += w[i];
  }
  if (tot <= 0) return;
  float r = frand() * tot;
  for (int i = 0; i < N; i++) {
    r -= w[i];
    if (r <= 0) { lastIdle = IDLE[i]; playAnim(IDLE[i], now); return; }
  }
}

static float blinkAmt(uint32_t now) {
  if (blinkT0 == 0) return 0;
  float u = (now - blinkT0) / 170.0f;
  if (u >= 1.0f) return 0;
  if (u < 0.38f) { float x = u / 0.38f; return 1 - (1 - x) * (1 - x); }
  float x = (u - 0.38f) / 0.62f;
  return 1 - x * x * (3 - 2 * x);
}

static float curCol[3] = {80, 220, 255};

static void directorUpdate(uint32_t now, float dt) {
  // ---- mind dynamics
  float since = (now - lastInteract) / 1000.0f;
  mind.bored = clampf((since - 20.0f) / 200.0f, 0, 1);
  mind.energy = clampf(mind.energy + dt * (sleeping ? 0.006f : -0.0028f), 0, 1);
  float joyBase = 0.58f + 0.25f * mind.love - 0.35f * mind.bored - 0.45f * mind.annoy;
  mind.joy += (joyBase - mind.joy) * clampf(dt * 0.12f, 0, 1);
  mind.annoy = fmaxf(0, mind.annoy - dt * 0.035f);
  mind.love = fmaxf(0, mind.love - dt * 0.004f);
  mind.dizzy = fmaxf(0, mind.dizzy - dt * 0.3f);

  if (petting()) {
    mind.joy = clampf(mind.joy + dt * 0.35f, 0, 1);
    mind.love = clampf(mind.love + dt * 0.22f, 0, 1);
    mind.annoy = fmaxf(0, mind.annoy - dt * 0.1f);
    if (now - lastHeartT > 650) { lastHeartT = now; spawnHeart(); }
  }
  if (shaking && cur.active && cur.id == AN_DIZZY) cur.loopUntil = now + 500;

  // ---- animation timing
  animTick(now);

  bool idleOK = !cur.active && !sleeping && !menuOpen && !petting() && !T.down;

  // ---- sleep / wake
  if (!sleeping && !fallingAsleep && idleOK && mind.energy < 0.28f && since > 40.0f) {
    fallingAsleep = true;
    playAnim(AN_SLEEP_NOD, now);
  }
  if (sleeping && !cur.active && mind.energy > 0.85f && now - sleepStart > 60000) wakeUp(now, false);
  if (sleeping && now - lastZ > 2400) { lastZ = now; spawnZ(); }

  // ---- demo mode (serial 'd'): plays every animation in turn
  if (demoMode && !cur.active && !menuOpen && now >= demoNext) {
    sleeping = false;
    playAnim(demoIdx % AN_COUNT, now);
    demoIdx++;
    demoNext = now + 900;
  }

  // ---- idle behaviour
  if (idleOK && !demoMode && now >= nextIdle) {
    pickIdle(now);
    nextIdle = now + (uint32_t)(frange(3500, 8500) * (1.4f - 0.5f * mind.energy));
  }

  // ---- blinking
  if (!sleeping && !petting() && !menuOpen && now >= nextBlink) {
    blinkT0 = now;
    float gap = frange(2200, 6000);
    if (frand() < 0.18f) gap = 260;  // double blink
    nextBlink = now + (uint32_t)gap;
  }

  // ---- gaze saccades (only when nothing else is steering the eyes)
  if (now >= nextSaccade) {
    float r = frand();
    if (r < 0.3f) { gazeX = frange(-5, 5); gazeY = frange(-4, 4); }
    else { gazeX = sgnf(frange(-1, 1)) * frange(14, 42); gazeY = frange(-20, 14); }
    if (fabsf(gazeX) > 26 && frand() < 0.3f && !sleeping) blinkT0 = now;
    nextSaccade = now + (uint32_t)frange(700, 3200);
  }
}

// ============================================================================
// Software renderer (RGB565 framebuffer, anti-aliased convex/concave polygons)
// ============================================================================
static uint16_t* fb = nullptr;
static uint8_t covBuf[SCR_W + 4];
static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

struct Shade {
  uint8_t r, g, b;
  float alpha;
  bool grad;
  float g0, gx, gy;
};

static void fillPoly(const V2* p, int n, const Shade& sh) {
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  for (int i = 0; i < n; i++) {
    if (p[i].x < minx) minx = p[i].x;
    if (p[i].x > maxx) maxx = p[i].x;
    if (p[i].y < miny) miny = p[i].y;
    if (p[i].y > maxy) maxy = p[i].y;
  }
  int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
  int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx);
  if (y0 < 0) y0 = 0;
  if (y1 > SCR_H - 1) y1 = SCR_H - 1;
  if (x0 < 0) x0 = 0;
  if (x1 > SCR_W - 1) x1 = SCR_W - 1;
  if (y0 > y1 || x0 > x1) return;
  int a256 = (int)(clampf(sh.alpha, 0, 1) * 256.0f);

  for (int y = y0; y <= y1; y++) {
    memset(covBuf + x0, 0, x1 - x0 + 1);
    bool any = false;
    for (int s = 0; s < 3; s++) {
      float yy = y + (s * 2 + 1) / 6.0f;
      float xs[16];
      int k = 0;
      for (int i = 0; i < n; i++) {
        const V2& a = p[i];
        const V2& b = p[(i + 1 == n) ? 0 : i + 1];
        if ((a.y <= yy) != (b.y <= yy)) {
          if (k < 16) xs[k++] = a.x + (yy - a.y) * (b.x - a.x) / (b.y - a.y);
        }
      }
      for (int i = 1; i < k; i++) {  // insertion sort
        float v = xs[i]; int j = i - 1;
        while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; }
        xs[j + 1] = v;
      }
      for (int j = 0; j + 1 < k; j += 2) {
        float xl = fmaxf(xs[j], (float)x0), xr = fminf(xs[j + 1], (float)(x1 + 1));
        if (xr <= xl) continue;
        any = true;
        int il = (int)floorf(xl), ir = (int)floorf(xr);
        if (il == ir) {
          int c = covBuf[il] + (int)(85.0f * (xr - xl));
          covBuf[il] = (uint8_t)(c > 255 ? 255 : c);
        } else {
          int c = covBuf[il] + (int)(85.0f * (il + 1 - xl));
          covBuf[il] = (uint8_t)(c > 255 ? 255 : c);
          for (int x = il + 1; x < ir; x++) {
            int c2 = covBuf[x] + 85;
            covBuf[x] = (uint8_t)(c2 > 255 ? 255 : c2);
          }
          if (ir <= x1) {
            int c3 = covBuf[ir] + (int)(85.0f * (xr - ir));
            covBuf[ir] = (uint8_t)(c3 > 255 ? 255 : c3);
          }
        }
      }
    }
    if (!any) continue;
    uint16_t* row = fb + (size_t)y * SCR_W;
    for (int x = x0; x <= x1; x++) {
      int cv = covBuf[x];
      if (!cv) continue;
      int a = (cv * a256) >> 8;
      if (a <= 0) continue;
      float f = 1.0f;
      if (sh.grad) {
        float g = clampf(sh.g0 + x * sh.gx + y * sh.gy, -1.0f, 1.0f);
        f = 1.0f - 0.22f * (g + 1.0f) * 0.5f;
      }
      int sr = (int)(sh.r * f), sg = (int)(sh.g * f), sb = (int)(sh.b * f);
      uint16_t d = row[x];
      int dr = (d >> 8) & 0xF8, dg = (d >> 3) & 0xFC, db = (d << 3) & 0xF8;
      int ia = a + (a >> 7);
      int r = (sr * ia + dr * (256 - ia)) >> 8;
      int g = (sg * ia + dg * (256 - ia)) >> 8;
      int b = (sb * ia + db * (256 - ia)) >> 8;
      int t = BAYER[((y & 3) << 2) | (x & 3)];
      r += t >> 1; g += t >> 2; b += t >> 1;
      if (r > 255) r = 255;
      if (g > 255) g = 255;
      if (b > 255) b = 255;
      row[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
  }
}

// ---- shape generation -------------------------------------------------------
static const int NP = 56;
static float tabC[NP], tabS[NP];

static void makeSuper(V2* out, int count, int stride, float a, float b, float expo, float ox, float oy,
                      float aref, float bref, float bend, float shear, float rot, float cx, float cy) {
  float inv = 2.0f / expo;
  float cr = cosf(rot), sr = sinf(rot);
  for (int i = 0; i < count; i++) {
    float c = tabC[i * stride], s = tabS[i * stride];
    float ex = a * sgnf(c) * powf(fabsf(c), inv) + ox;
    float ey = b * sgnf(s) * powf(fabsf(s), inv) + oy;
    float xn = ex / aref;
    float arch = 1.0f - xn * xn;
    if (arch < 0) arch = 0;
    ey -= bend * bref * arch;
    ex += shear * ey;
    out[i].x = ex * cr - ey * sr + cx;
    out[i].y = ex * sr + ey * cr + cy;
  }
}

struct Item { V2 p[NP]; int n; Shade sh; };
static const int MAX_ITEMS = 40;
static Item items[MAX_ITEMS];
static int nItems = 0;
static float extMinY, extMaxY;

static Item* newItem() { return nItems < MAX_ITEMS ? &items[nItems] : nullptr; }
static void commitItem(Item* it) {
  for (int i = 0; i < it->n; i++) {
    if (it->p[i].y < extMinY) extMinY = it->p[i].y;
    if (it->p[i].y > extMaxY) extMaxY = it->p[i].y;
  }
  nItems++;
}

static void toScreen(float ux, float uy, float ca, float sa, float& sx, float& sy) {
  float dx = ux - CXF, dy = uy - CYF;
  sx = CXF + dx * ca - dy * sa;
  sy = CYF + dx * sa + dy * ca;
}

static void circleItem(float cx, float cy, float r, uint8_t R, uint8_t G, uint8_t B, float alpha, bool grad, float ca, float sa) {
  Item* it = newItem();
  if (!it) return;
  it->n = 32;
  for (int i = 0; i < 32; i++) {
    float t = i * 2 * PI_F / 32;
    it->p[i].x = cx + cosf(t) * r;
    it->p[i].y = cy + sinf(t) * r;
  }
  it->sh.r = R; it->sh.g = G; it->sh.b = B; it->sh.alpha = alpha; it->sh.grad = grad;
  float dvx = -sa, dvy = ca;
  float rr = fmaxf(r, 6.0f);
  it->sh.g0 = -(cx * dvx + cy * dvy) / rr; it->sh.gx = dvx / rr; it->sh.gy = dvy / rr;
  commitItem(it);
}

static void shapeItem(int kind, float cx, float cy, float size, float rot, uint8_t R, uint8_t G, uint8_t B, float alpha) {
  Item* it = newItem();
  if (!it) return;
  float cr = cosf(rot), sr = sinf(rot);
  if (kind == 0) {  // heart
    it->n = 40;
    for (int i = 0; i < 40; i++) {
      float t = i * 2 * PI_F / 40;
      float s3 = sinf(t);
      float hx = 16 * s3 * s3 * s3;
      float hy = -(13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t));
      hx *= size / 16.0f; hy *= size / 16.0f;
      it->p[i].x = cx + hx * cr - hy * sr;
      it->p[i].y = cy + hx * sr + hy * cr;
    }
  } else if (kind == 1) {  // Z
    static const float zx[10] = {-1, 1, 1, -0.35f, 1, 1, -1, -1, 0.35f, -1};
    static const float zy[10] = {-1, -1, -0.55f, 0.55f, 0.55f, 1, 1, 0.55f, -0.55f, -0.55f};
    it->n = 10;
    for (int i = 0; i < 10; i++) {
      float hx = zx[i] * size, hy = zy[i] * size;
      it->p[i].x = cx + hx * cr - hy * sr;
      it->p[i].y = cy + hx * sr + hy * cr;
    }
  } else {  // star
    it->n = 10;
    for (int i = 0; i < 10; i++) {
      float t = i * PI_F / 5 - PI_F / 2;
      float rr = (i & 1) ? size * 0.45f : size;
      float hx = cosf(t) * rr, hy = sinf(t) * rr;
      it->p[i].x = cx + hx * cr - hy * sr;
      it->p[i].y = cy + hx * sr + hy * cr;
    }
  }
  it->sh.r = R; it->sh.g = G; it->sh.b = B; it->sh.alpha = alpha; it->sh.grad = false;
  it->sh.g0 = it->sh.gx = it->sh.gy = 0;
  commitItem(it);
}

static void updateParts(float dt) {
  for (int i = 0; i < 14; i++) {
    Part& p = parts[i];
    if (!p.on) continue;
    p.life += dt;
    if (p.type == 2) {
      p.phase += dt * 4.5f;
      p.x = CXF + cosf(p.phase) * 95; p.y = CYF - 112 + sinf(p.phase) * 22;
      p.rot += dt * 3.0f;
      if (!dizzyFx && p.max > p.life + 0.4f) p.max = p.life + 0.4f;
    } else if (p.type == 0) {
      p.phase += dt * 3.0f;
      p.x += (p.vx + sinf(p.phase) * 14) * dt; p.y += p.vy * dt;
    } else {
      p.x += p.vx * dt; p.y += p.vy * dt; p.size = 9 + p.life * 5;
    }
    if (p.life >= p.max) p.on = false;
  }
}

static float menuEase() { return menuP < 0 ? 0 : (menuP > 1 ? 1 : menuP); }

static void buildScene() {
  nItems = 0; extMinY = 1e9f; extMaxY = -1e9f;
  float ca = cosf(gAlpha), sa = sinf(gAlpha);
  float mp = menuEase();
  float sM = 1.0f - 0.5f * mp;
  uint8_t cr8 = (uint8_t)clampf(curCol[0], 0, 255), cg8 = (uint8_t)clampf(curCol[1], 0, 255), cb8 = (uint8_t)clampf(curCol[2], 0, 255);
  uint8_t hr = (uint8_t)(cr8 + (255 - cr8) * 0.75f), hg = (uint8_t)(cg8 + (255 - cg8) * 0.75f), hb = (uint8_t)(cb8 + (255 - cb8) * 0.75f);
  float blink = blinkAmt(gNow);
  if (sleeping || petting()) blink = 0;

  for (int e = 0; e < 2; e++) {
    float sign = (e == 0) ? -1.0f : 1.0f;
    float wmul = sp[C_W].x * (e == 0 ? sp[C_WL].x : sp[C_WR].x);
    float hmul = sp[C_H].x * (e == 0 ? sp[C_HL].x : sp[C_HR].x);
    float a = 60.0f * wmul * (1.0f + 0.10f * blink) * sM;
    float b = 76.0f * hmul * (1.0f - 0.80f * blink) * sM;
    if (b < 11.0f * sM) b = 11.0f * sM;
    float ex = CXF + sign * (95.0f + sp[C_GAP].x) * sM + sp[C_LX].x * sM;
    float ey = CYF + sp[C_LY].x * sM + (e == 0 ? sp[C_YL].x : sp[C_YR].x);
    float roll = sp[C_ROLL].x * DEG;
    float rx = ex - CXF, ry = ey - CYF;
    ex = CXF + rx * cosf(roll) - ry * sinf(roll);
    ey = CYF + rx * sinf(roll) + ry * cosf(roll);
    float scx, scy;
    toScreen(ex, ey, ca, sa, scx, scy);
    float rot = (-sign * sp[C_TILT].x + sp[C_ROLL].x) * DEG + gAlpha;
    float bend = ((e == 0) ? sp[C_BL].x : sp[C_BR].x) * 0.38f;  // 'a little' bend only
    float shear = -sign * sp[C_SHEAR].x;
    float expo = sp[C_N].x;
    expo = lerpf(2.3f, expo, clampf((b / sM - 14.0f) / 34.0f, 0, 1));  // squashed => rounder ends

    Item* body = newItem();
    if (!body) break;
    body->n = NP;
    makeSuper(body->p, NP, 1, a, b, expo, 0, 0, a, b, bend, shear, rot, scx, scy);
    body->sh.r = cr8; body->sh.g = cg8; body->sh.b = cb8; body->sh.alpha = 1.0f; body->sh.grad = true;
    float dvx = -sa, dvy = ca;
    float bref = fmaxf(b, 18.0f * sM);
    body->sh.g0 = -(scx * dvx + scy * dvy) / bref;
    body->sh.gx = dvx / bref; body->sh.gy = dvy / bref;
    commitItem(body);

    // soft broad highlight + crisp small highlight (light from upper left), they bend with the eye
    Item* h1 = newItem();
    if (h1) {
      h1->n = NP / 2;
      makeSuper(h1->p, NP / 2, 2, 0.62f * a, 0.30f * b, 2.4f, -0.12f * a, -0.42f * b, a, b, bend, shear, rot, scx, scy);
      h1->sh.r = hr; h1->sh.g = hg; h1->sh.b = hb; h1->sh.alpha = 0.14f; h1->sh.grad = false;
      h1->sh.g0 = h1->sh.gx = h1->sh.gy = 0;
      commitItem(h1);
    }
    Item* h2 = newItem();
    if (h2) {
      h2->n = NP / 2;
      makeSuper(h2->p, NP / 2, 2, 0.24f * a, 0.12f * b, 2.4f, -0.32f * a, -0.58f * b, a, b, bend, shear, rot, scx, scy);
      h2->sh.r = hr; h2->sh.g = hg; h2->sh.b = hb; h2->sh.alpha = 0.55f; h2->sh.grad = false;
      h2->sh.g0 = h2->sh.gx = h2->sh.gy = 0;
      commitItem(h2);
    }
  }

  // ---- particles
  for (int i = 0; i < 14; i++) {
    Part& p = parts[i];
    if (!p.on) continue;
    float al = fminf(1.0f, fminf(p.life / 0.3f, (p.max - p.life) / 0.5f));
    if (al <= 0) continue;
    float sx, sy;
    toScreen(p.x, p.y, ca, sa, sx, sy);
    if (p.type == 0) shapeItem(0, sx, sy, p.size, gAlpha, 255, 100, 150, al * 0.92f);
    else if (p.type == 1) shapeItem(1, sx, sy, p.size, gAlpha, 150, 170, 240, al * 0.85f);
    else shapeItem(2, sx, sy, p.size, p.rot + gAlpha, 255, 225, 80, al * 0.95f);
  }

  // ---- color menu: ring of presets around the (shrunken) eyes
  if (mp > 0.01f) {
    for (int k = 0; k < 8; k++) {
      float ang = k * 45.0f * DEG;
      float rr = MENU_RING * mp;
      float ux = CXF + rr * sinf(ang), uy = CYF - rr * cosf(ang);
      float sx, sy;
      toScreen(ux, uy, ca, sa, sx, sy);
      float rad = 30.0f * mp;
      if (k == settings.colorIdx) circleItem(sx, sy, rad + 6, 255, 255, 255, 0.9f, false, ca, sa);
      circleItem(sx, sy, rad, PRESETS[k][0], PRESETS[k][1], PRESETS[k][2], 1.0f, true, ca, sa);
      float hx, hy;
      toScreen(ux - rad * 0.32f, uy - rad * 0.45f, ca, sa, hx, hy);
      circleItem(hx, hy, rad * 0.2f, 255, 255, 255, 0.45f, false, ca, sa);
    }
  }
}

// ============================================================================
// Public API
// ============================================================================
static int prevMinY = 0, prevMaxY = SCR_H - 1;

static void init(uint16_t* framebuffer, uint32_t seed) {
  fb = framebuffer;
  memset(fb, 0, (size_t)SCR_W * SCR_H * 2);
  rngState = seed ? seed : 0x1234567u;
  for (int i = 0; i < NP; i++) {
    float t = i * 2 * PI_F / NP;
    tabC[i] = cosf(t); tabS[i] = sinf(t);
  }
  buildAnims();
  for (int c = 0; c < C_COUNT; c++) { sp[c].x = sp[c].t = NEUTRAL[c]; sp[c].v = 0; }
  sp[C_H].x = 0.17f;
  memset(parts, 0, sizeof(parts));
  memset(&T, 0, sizeof(T));
  for (int c = 0; c < 3; c++) curCol[c] = PRESETS[settings.colorIdx & 7][c];
  prevMinY = 0; prevMaxY = SCR_H - 1;
}

static void start(uint32_t now) {
  gNow = now; lastInteract = now;
  nextIdle = now + 5000; nextBlink = now + 3000; nextSaccade = now + 1500;
  playAnim(AN_BOOT, now);
}

static void update(uint32_t now, float dt, const Input& in) {
  gNow = now;
  dt = clampf(dt, 0.001f, 0.05f);
  if (in.imu) imuUpdate(now, dt, in.ax, in.ay, in.az);
  touchUpdate(now, in.touch, in.tx, in.ty);

  // menu spring + auto close
  if (menuOpen && now - menuT0 > 12000) closeMenu();
  float mt = menuOpen ? 1.0f : 0.0f;
  menuV += ((mt - menuP) * 220.0f - menuV * 2.0f * 0.85f * sqrtf(220.0f)) * dt;
  menuP += menuV * dt;
  if (!menuOpen && menuP < 0.004f && fabsf(menuV) < 0.05f) { menuP = 0; menuV = 0; }

  directorUpdate(now, dt);

  // color blend toward chosen preset
  float kc = clampf(dt * 9.0f, 0, 1);
  for (int c = 0; c < 3; c++) curCol[c] += (PRESETS[settings.colorIdx & 7][c] - curCol[c]) * kc;

  // ---- choose pose targets
  float tg[C_COUNT];
  float tsec = now * 0.001f;
  bool animOn = cur.active;
  if (animOn) {
    const Anim& A = ANIMS[cur.id];
    memcpy(tg, A.s[cur.idx].v, sizeof(tg));
    springK = A.k; springZ = A.z;
  } else {
    if (petting()) petTarget(tg); else baseTarget(tg);
    springK = petting() ? 200.0f : 190.0f;
    springZ = 0.7f;
  }

  // ---- additive life: breathing, tiny drift, saccades, attention
  float breath = sinf(tsec * (sleeping ? 1.1f : 1.7f));
  tg[C_H] *= 1.0f + (sleeping ? 0.14f : 0.02f) * breath;
  tg[C_LX] += 2.2f * sinf(tsec * 0.9f) + 1.3f * sinf(tsec * 2.1f + 1.0f);
  tg[C_LY] += 1.6f * sinf(tsec * 0.7f + 2.0f);
  if (!sleeping) tg[C_ROLL] += 2.0f * sinf(tsec * 0.37f);

  if (!animOn && !sleeping && !menuOpen && !petting()) {
    float ax = gazeX, ay = gazeY;
    if (T.down) {
      ax = clampf(T.x * 0.22f, -34, 34); ay = clampf(T.y * 0.2f, -24, 24);
    } else if (now < lookAtUntil) {
      ax = lookAtX; ay = lookAtY;
    }
    tg[C_LX] += ax; tg[C_LY] += ay;
  }
  if (menuOpen) { tg[C_LX] = 0; tg[C_LY] = 0; }

  for (int c = 0; c < C_COUNT; c++) sp[c].t = tg[c];
  physics(dt);
  updateParts(dt);
}

// Draws the frame into the framebuffer. Returns false if nothing needs flushing.
// y0..y1 (inclusive, y0 even, y1 odd) are the rows to push to the panel.
static bool render(int& y0, int& y1) {
  buildScene();
  int a, b;
  if (menuP > 0.004f || menuOpen) {
    a = 0; b = SCR_H - 1;
  } else {
    a = (int)floorf(fminf((float)prevMinY, extMinY)) - 2;
    b = (int)ceilf(fmaxf((float)prevMaxY, extMaxY)) + 2;
  }
  if (a < 0) a = 0;
  if (b > SCR_H - 1) b = SCR_H - 1;
  a &= ~1;
  b |= 1;
  if (b > SCR_H - 1) b = SCR_H - 1;
  memset(fb + (size_t)a * SCR_W, 0, (size_t)(b - a + 1) * SCR_W * 2);
  for (int i = 0; i < nItems; i++) fillPoly(items[i].p, items[i].n, items[i].sh);
  if (menuP > 0.004f || menuOpen) { prevMinY = 0; prevMaxY = SCR_H - 1; }
  else { prevMinY = (int)floorf(extMinY); prevMaxY = (int)ceilf(extMaxY); }
  y0 = a; y1 = b;
  return true;
}

// ---- helpers for the serial console
static int animCount() { return AN_COUNT; }
static const char* animName(int i) { return (i >= 0 && i < AN_COUNT) ? ANIMS[i].name : "?"; }
static void forceAnim(int i) { if (i >= 0 && i < AN_COUNT) { sleeping = false; cur.active = false; playAnim(i, gNow); } }
static void nextColor() { settings.colorIdx = (settings.colorIdx + 1) & 7; settings.dirty = true; }
static void orientStep() { settings.orientOffset = (settings.orientOffset + 90) % 360; settings.dirty = true; }
static void mirrorToggle() { settings.mirror = !settings.mirror; settings.dirty = true; }
static void toggleSleep() {
  if (sleeping) wakeUp(gNow, false);
  else { mind.energy = 0.2f; lastInteract = gNow - 60000; }
}

}  // namespace ch
