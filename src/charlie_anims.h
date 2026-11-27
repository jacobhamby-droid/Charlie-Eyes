// Charlie animation library. This is an include fragment: it is pulled in by charlie_core.h
// inside `namespace ch`, after the basic helpers (DEG etc.) and before the behaviour code.
//
// Every animation is a short list of poses ("steps"). A pose is a set of eye-shape channels that
// the springs in the core ease toward, so every move is smooth. Eyes are only ever squashed,
// stretched, tilted, shifted or gently bent: they are never cut or clipped.
#pragma once

// ---- pose channels ----------------------------------------------------------------------------
enum Ch {
  C_LX, C_LY,        // look offset (px)
  C_W, C_H,          // size scale (both eyes)
  C_TILT,            // degrees, + = angry (inner edge down), - = sad
  C_BL, C_BR,        // bend (arch) left / right eye
  C_SHEAR,           // slant
  C_N,               // squircle exponent (boxiness)
  C_HL, C_HR,        // extra height multiplier per eye
  C_WL, C_WR,        // extra width multiplier per eye
  C_YL, C_YR,        // extra vertical offset per eye (px)
  C_ROLL,            // whole-face roll (deg)
  C_GAP,             // extra spacing (px)
  C_COUNT
};
static const float NEUTRAL[C_COUNT] = {0, 0, 1, 1, 0, 0, 0, 0, 4, 1, 1, 1, 1, 0, 0, 0, 0};
// minimum heights keep every eye a rounded pill, never a hairline
static const float CH_LO[C_COUNT] = {-60, -50, 0.3f, 0.17f, -40, -0.6f, -0.6f, -0.5f, 2.6f, 0.17f, 0.17f, 0.3f, 0.3f, -40, -40, -45, -20};
static const float CH_HI[C_COUNT] = {60, 50, 1.9f, 2.0f, 40, 0.9f, 0.9f, 0.5f, 6.0f, 2.0f, 2.0f, 1.9f, 1.9f, 40, 40, 45, 40};

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
  uint8_t snd = 0;           // sound that plays when the animation starts
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
  // ---- v3: hearing, more reactions, more moods, clock
  AN_PERK_UP, AN_LISTEN, AN_HAPPY_GREET, AN_STARTLE_LOUD, AN_FLINCH, AN_POKE_TICKLE, AN_POKE_OW,
  AN_POKE_PLAYFUL, AN_LONELY, AN_FIDGET, AN_SLOW_BLINK, AN_DREAM, AN_SHY_HIDE, AN_PROUD, AN_SING,
  AN_GRUMBLE, AN_SCAN, AN_JUMP, AN_FOCUS, AN_TWITCH, AN_CHECK_TIME, AN_TIMER_SET, AN_TIMER_START,
  AN_CANCEL, AN_ALARM_SET, AN_RELIEF, AN_NUZZLE, AN_COZY,
  AN_COUNT
};

static Anim ANIMS[AN_COUNT];

static Anim& def(int id, const char* name, float k, float z, uint8_t pri, uint8_t fx = FX_NONE, bool loop = false) {
  Anim& a = ANIMS[id];
  a.name = name; a.k = k; a.z = z; a.pri = pri; a.fx = fx; a.loop = loop; a.n = 0; a.snd = 0;
  return a;
}

static void buildAnims() {
  // ---- boot: wake up and look around
  def(AN_BOOT, "boot", 260, 0.55f, 5)
    .add(Step(500).size(1.05f, 0.17f))
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
    .add(Step(700).size(1.04f, 0.17f).bend(0.05f).look(0, 12));

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
    .add(Step(120).hts(0.2f, 1.05f).bends(0.38f, 0.1f).wds(1.12f, 1.0f))
    .add(Step(500).hts(0.2f, 1.05f).bends(0.38f, 0.1f).wds(1.12f, 1.0f))
    .add(Step(170))
    .add(Step(200));

  def(AN_WINK_R, "wink_right", 420, 0.55f, 2)
    .add(Step(120).hts(1.05f, 0.2f).bends(0.1f, 0.38f).wds(1.0f, 1.12f))
    .add(Step(500).hts(1.05f, 0.2f).bends(0.1f, 0.38f).wds(1.0f, 1.12f))
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
    .add(Step(150).size(1.1f, 0.2f).bend(0.3f).look(0, 10))
    .add(Step(90).size(0.74f, 1.42f).look(0, -12))
    .add(Step(140).size(1.15f, 0.84f))
    .add(Step(320));

  def(AN_CONFUSED, "confused", 240, 0.7f, 1)
    .add(Step(260).hts(1.3f, 0.76f).roll(6).shear(0.1f))
    .add(Step(750).hts(1.3f, 0.76f).roll(6).shear(0.1f))
    .add(Step(300).hts(0.8f, 1.22f).roll(-6).shear(-0.1f).look(8, -4))
    .add(Step(750).hts(0.8f, 1.22f).roll(-6).shear(-0.1f).look(8, -4))
    .add(Step(320));

  // ======================== v3 ===================================================================
  // ---- hearing: he notices you
  def(AN_PERK_UP, "perk_up", 460, 0.42f, 3)
    .add(Step(70).size(0.9f, 1.38f).look(0, -10).gap(6))
    .add(Step(160).size(0.96f, 1.22f).look(0, -5).gap(4))
    .add(Step(220).size(1.0f, 1.12f).look(0, -3).roll(5))
    .add(Step(420).size(1.0f, 1.12f).look(0, -3).roll(5))
    .add(Step(260).size(1, 1.04f));

  def(AN_LISTEN, "listen", 180, 0.8f, 1, FX_NONE, true)
    .add(Step(350).size(1.0f, 1.12f).look(0, -3).roll(6).hts(1.08f, 0.96f))
    .add(Step(900).size(1.0f, 1.12f).look(0, -3).roll(6).hts(1.08f, 0.96f))
    .add(Step(350).size(1.0f, 1.12f).look(0, -3).roll(-6).hts(0.96f, 1.08f))
    .add(Step(900).size(1.0f, 1.12f).look(0, -3).roll(-6).hts(0.96f, 1.08f));

  {
    Anim& a = def(AN_HAPPY_GREET, "happy_greet", 380, 0.5f, 3);
    a.add(Step(130).size(1.12f, 0.7f).bend(0.4f).look(0, 7));
    a.add(Step(120).size(0.9f, 1.28f).look(0, -14));
    a.add(Step(120).size(1.1f, 0.74f).bend(0.4f).look(0, 7));
    a.add(Step(120).size(0.92f, 1.22f).look(0, -12));
    for (int i = 0; i < 4; i++) a.add(Step(115).roll((i & 1) ? -10 : 10).bend(0.42f).size(1.06f, 0.86f));
    a.add(Step(280).size(1.04f, 0.95f).bend(0.3f));
    a.add(Step(300));
  }

  {
    Anim& a = def(AN_STARTLE_LOUD, "startle_loud", 520, 0.35f, 4);
    a.add(Step(60).size(0.7f, 1.45f).gap(12).look(0, -8));
    a.add(Step(90).size(0.7f, 1.45f).gap(12).look(0, -10).ys(-16, -16));
    a.add(Step(110).size(1.1f, 0.7f).ys(10, 10));
    a.add(Step(160).size(0.95f, 1.1f));
    a.add(Step(420).size(0.9f, 1.2f).gap(4).look(0, -4));
    a.add(Step(350));
  }

  def(AN_FLINCH, "flinch", 440, 0.5f, 3)
    .add(Step(70).size(1.06f, 0.5f).tilt(-8).look(-16, 6).roll(-6))
    .add(Step(320).size(1.06f, 0.5f).tilt(-8).look(-16, 6).roll(-6))
    .add(Step(200).size(1, 0.85f).look(-6, 2))
    .add(Step(300));

  {
    Anim& a = def(AN_POKE_TICKLE, "poke_tickle", 420, 0.45f, 3);
    for (int i = 0; i < 6; i++)
      a.add(Step(85).hts((i & 1) ? 1.2f : 0.78f, (i & 1) ? 0.78f : 1.2f).roll((i & 1) ? 8 : -8).bend(0.4f).size(1.08f, 0.9f));
    a.add(Step(220).size(1.05f, 0.88f).bend(0.35f));
    a.add(Step(320));
  }

  def(AN_POKE_OW, "poke_ow", 400, 0.5f, 3)
    .add(Step(70).size(1.08f, 0.45f).tilt(-6).bend(0.1f))
    .add(Step(260).size(1.08f, 0.45f).tilt(-8))
    .add(Step(260).size(0.98f, 0.85f).tilt(-10).look(0, 8))
    .add(Step(350));

  def(AN_POKE_PLAYFUL, "poke_playful", 400, 0.5f, 3)
    .add(Step(110).size(0.92f, 1.2f).look(0, -12))
    .add(Step(110).size(1.1f, 0.8f).look(0, 6).bend(0.3f))
    .add(Step(110).size(0.92f, 1.2f).look(0, -12).roll(6))
    .add(Step(110).size(1.1f, 0.8f).look(0, 6).bend(0.3f).roll(-6))
    .add(Step(250).size(1.03f, 0.95f))
    .add(Step(250));

  // ---- moods
  def(AN_LONELY, "lonely", 150, 0.85f, 1)
    .add(Step(600).size(0.96f, 0.84f).tilt(-12).look(-22, 10))
    .add(Step(900).size(0.96f, 0.84f).tilt(-12).look(-22, 10))
    .add(Step(500).size(0.96f, 0.84f).tilt(-12).look(22, 10))
    .add(Step(900).size(0.96f, 0.84f).tilt(-12).look(22, 10))
    .add(Step(600).size(0.96f, 0.8f).tilt(-14).look(0, 16))
    .add(Step(900).size(0.96f, 0.8f).tilt(-14).look(0, 16))
    .add(Step(500));

  def(AN_FIDGET, "fidget", 420, 0.55f, 1)
    .add(Step(100).look(-8, -3).hts(1.06f, 0.96f))
    .add(Step(100).look(8, 2).hts(0.96f, 1.06f))
    .add(Step(100).look(-4, -2))
    .add(Step(220));

  def(AN_SLOW_BLINK, "slow_blink", 200, 0.85f, 1)
    .add(Step(380).size(1.02f, 0.22f).look(0, 4))
    .add(Step(250).size(1.02f, 0.22f).look(0, 4))
    .add(Step(420).size(1, 0.95f))
    .add(Step(200));

  def(AN_DREAM, "dream", 260, 0.6f, 1)
    .add(Step(180).size(1.04f, 0.3f).look(-6, 8))
    .add(Step(160).size(1.04f, 0.2f).look(8, 8))
    .add(Step(180).size(1.04f, 0.32f).look(-4, 8))
    .add(Step(160).size(1.04f, 0.22f).look(6, 9))
    .add(Step(250).size(1.04f, 0.17f).look(0, 12))
    .add(Step(200).size(1.04f, 0.17f).look(0, 12));

  def(AN_SHY_HIDE, "shy_hide", 220, 0.7f, 1)
    .add(Step(250).size(0.7f, 0.7f).look(-26, 14).tilt(-8))
    .add(Step(900).size(0.7f, 0.7f).look(-26, 14).tilt(-8))
    .add(Step(300).size(0.8f, 0.85f).look(-30, 10).tilt(-6))
    .add(Step(500).size(0.8f, 0.85f).look(-30, 10).tilt(-6))
    .add(Step(350));

  def(AN_PROUD, "proud", 240, 0.7f, 1)
    .add(Step(250).size(1.04f, 1.16f).look(0, -10).tilt(-3))
    .add(Step(900).size(1.04f, 1.16f).look(0, -10).tilt(-3))
    .add(Step(300));

  {
    Anim& a = def(AN_SING, "sing", 260, 0.55f, 1);
    for (int i = 0; i < 6; i++) a.add(Step(220).roll((i & 1) ? -7 : 7).bend(0.3f).size(1.05f, 0.95f).look((i & 1) ? -10 : 10, -4));
    a.add(Step(300));
  }

  def(AN_GRUMBLE, "grumble", 240, 0.75f, 1)
    .add(Step(300).size(1.04f, 0.66f).tilt(11).look(-18, 4))
    .add(Step(700).size(1.04f, 0.66f).tilt(11).look(-18, 4))
    .add(Step(250).size(1.04f, 0.66f).tilt(11).look(18, 4))
    .add(Step(600).size(1.04f, 0.66f).tilt(11).look(18, 4))
    .add(Step(300).size(1, 0.85f).tilt(5));

  def(AN_SCAN, "scan", 90, 0.9f, 1)
    .add(Step(500).look(-36, 0).size(1.0f, 0.72f))
    .add(Step(1000).look(36, 0).size(1.0f, 0.72f))
    .add(Step(1000).look(-36, 0).size(1.0f, 0.72f))
    .add(Step(400));

  def(AN_JUMP, "jump", 420, 0.45f, 2)
    .add(Step(120).size(1.12f, 0.68f).look(0, 8))
    .add(Step(130).size(0.82f, 1.34f).look(0, -14).ys(-20, -20))
    .add(Step(110).size(0.85f, 1.28f).ys(-26, -26))
    .add(Step(130).size(1.14f, 0.72f).ys(8, 8))
    .add(Step(220).size(1.0f, 1.0f))
    .add(Step(250));

  def(AN_FOCUS, "focus", 260, 0.8f, 1)
    .add(Step(300).size(1.08f, 0.55f).tilt(4))
    .add(Step(1200).size(1.08f, 0.55f).tilt(4))
    .add(Step(250));

  def(AN_TWITCH, "twitch", 520, 0.5f, 1)
    .add(Step(60).roll(4).wds(1.1f, 0.95f))
    .add(Step(60).roll(-4).wds(0.95f, 1.1f))
    .add(Step(60))
    .add(Step(180));

  // ---- clock helpers
  def(AN_CHECK_TIME, "check_time", 240, 0.7f, 1)
    .add(Step(200).look(30, -22).size(1, 1.05f))
    .add(Step(650).look(30, -22).size(1, 1.05f))
    .add(Step(250));

  def(AN_TIMER_SET, "timer_set", 380, 0.55f, 2)
    .add(Step(120).look(0, 8).size(1, 0.9f))
    .add(Step(130).look(0, -6))
    .add(Step(200));

  def(AN_TIMER_START, "timer_start", 340, 0.6f, 2)
    .add(Step(140).size(1.06f, 0.7f).tilt(8).look(0, 2))
    .add(Step(240).size(1.06f, 0.7f).tilt(8).look(0, 2))
    .add(Step(180).size(1.06f, 0.6f).tilt(8).look(0, 6))
    .add(Step(250).size(1.03f, 0.8f).tilt(4))
    .add(Step(250));

  def(AN_CANCEL, "cancel", 360, 0.55f, 2)
    .add(Step(140).hts(1.2f, 0.85f).roll(5))
    .add(Step(250).hts(1.2f, 0.85f).roll(5))
    .add(Step(140).hts(0.85f, 1.2f).roll(-5))
    .add(Step(250).hts(0.85f, 1.2f).roll(-5))
    .add(Step(250));

  def(AN_ALARM_SET, "alarm_set", 420, 0.55f, 2)
    .add(Step(110).hts(1.05f, 0.2f).bends(0.1f, 0.4f).wds(1.0f, 1.12f))
    .add(Step(420).hts(1.05f, 0.2f).bends(0.1f, 0.4f).wds(1.0f, 1.12f))
    .add(Step(160))
    .add(Step(200).size(1.04f, 0.92f).bend(0.3f))
    .add(Step(250));

  def(AN_RELIEF, "relief", 200, 0.8f, 3)
    .add(Step(300).size(1.04f, 0.6f).look(0, 8).bend(0.15f))
    .add(Step(500).size(1.0f, 0.55f).look(0, 10))
    .add(Step(450).size(1, 0.9f))
    .add(Step(300));

  def(AN_NUZZLE, "nuzzle", 260, 0.6f, 3)
    .add(Step(220).size(1.12f, 0.62f).bend(0.4f).look(-14, 5).roll(-6))
    .add(Step(240).size(1.1f, 0.66f).bend(0.4f).look(14, 5).roll(6))
    .add(Step(240).size(1.1f, 0.66f).bend(0.4f).look(-10, 5).roll(-5))
    .add(Step(300).size(1.04f, 0.9f).bend(0.3f));

  def(AN_COZY, "cozy", 180, 0.85f, 1)
    .add(Step(300).size(1.1f, 0.5f).bend(0.45f))
    .add(Step(1400).size(1.1f, 0.5f).bend(0.45f))
    .add(Step(300));
}

// which sound each animation makes when it starts
static void mapSounds() {
  using namespace snd;
  struct M { int a; uint8_t s; };
  static const M T[] = {
    {AN_BOOT, S_BOOT}, {AN_POKE_SURPRISE, S_SURPRISE}, {AN_POKE_GIGGLE, S_GIGGLE}, {AN_POKE_ANNOYED, S_GRUMBLE},
    {AN_ANGRY, S_ANGRY}, {AN_SHAKE_NO, S_NO}, {AN_NOD_YES, S_YES}, {AN_DIZZY, S_DIZZY}, {AN_DIZZY_RECOVER, S_SIGH},
    {AN_SCARED, S_SCARED}, {AN_WAKE_STARTLE, S_SURPRISE}, {AN_WAKE_NATURAL, S_WAKE}, {AN_SLEEP_NOD, S_SIGH},
    {AN_PET_END, S_HAPPY}, {AN_LOOK_AROUND, S_BLIP}, {AN_CURIOUS, S_CURIOUS}, {AN_THINK, S_THINK},
    {AN_PEEK_SHY, S_SHY}, {AN_DOUBLE_TAKE, S_SURPRISE}, {AN_EYE_ROLL, S_NO}, {AN_YAWN, S_YAWN},
    {AN_STRETCH, S_UP}, {AN_SIGH, S_SIGH}, {AN_SAD_DROOP, S_SAD}, {AN_SQUINT_SUS, S_GRUMBLE},
    {AN_SMUG, S_PROUD}, {AN_WIGGLE, S_GIGGLE}, {AN_HAPPY_BOUNCE, S_HAPPY}, {AN_EXCITED, S_EXCITED},
    {AN_WINK_L, S_BLIP}, {AN_WINK_R, S_BLIP}, {AN_LOVE, S_LOVE}, {AN_SNEEZE, S_SNEEZE}, {AN_CONFUSED, S_THINK},
    {AN_PERK_UP, S_PERK}, {AN_LISTEN, S_LISTEN}, {AN_HAPPY_GREET, S_EXCITED}, {AN_STARTLE_LOUD, S_SCARED},
    {AN_FLINCH, S_OUCH}, {AN_POKE_TICKLE, S_TICKLE}, {AN_POKE_OW, S_OUCH}, {AN_POKE_PLAYFUL, S_GIGGLE},
    {AN_LONELY, S_SAD}, {AN_FIDGET, S_BLIP}, {AN_DREAM, S_SNORE}, {AN_SHY_HIDE, S_SHY}, {AN_PROUD, S_PROUD},
    {AN_SING, S_SING}, {AN_GRUMBLE, S_GRUMBLE}, {AN_SCAN, S_SCAN}, {AN_JUMP, S_HAPPY}, {AN_TWITCH, S_BLIP},
    {AN_CHECK_TIME, S_THINK}, {AN_TIMER_SET, S_UP}, {AN_TIMER_START, S_CONFIRM}, {AN_CANCEL, S_CANCEL},
    {AN_ALARM_SET, S_ALARMSET}, {AN_RELIEF, S_RELIEF}, {AN_NUZZLE, S_PURR}, {AN_COZY, S_PURR},
  };
  for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) ANIMS[T[i].a].snd = T[i].s;
}
