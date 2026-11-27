// Charlie core: eyes, animation, moods, hearing, touch gestures, menus, timer/alarm, software renderer.
// Hardware-free on purpose (no Arduino calls) so it can be compiled and rendered on a PC.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "charlie_sound.h"

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
  uint8_t vol = 2;           // 0 mute, 1 low, 2 medium, 3 high
  bool alarmOn = false;
  uint8_t alarmH = 7, alarmM = 0;  // 24 h
  uint8_t timerMin = 5;
  bool dirty = false;
};
static Settings settings;

struct Input {
  bool imu = false;
  float ax = 0, ay = 0, az = 0;
  bool touch = false;
  float tx = 0, ty = 0;
  bool rtcOK = false;        // clock has been set by the user
  uint8_t hh = 0, mm = 0, ss = 0;
};

// the core asks main.cpp to write the real-time clock
struct ClockReq { bool pending = false; uint8_t h = 0, m = 0; };
static ClockReq clockReq;

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

// animation library (pose channels, steps, ~60 animations, sound map)
#include "charlie_anims.h"

static inline void say(int id) { snd::play((snd::Id)id); }

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
  float fear = 0.0f, alert = 0.0f;
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
static int pendingAnim = -1;

struct Part {
  bool on; uint8_t type; float x, y, vx, vy, rot, vr, size, life, max, phase, orbit;
};
static const int NPARTS = 24;
static Part parts[NPARTS];

static Part* allocPart() {
  for (int i = 0; i < NPARTS; i++) if (!parts[i].on) return &parts[i];
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
// little "ring ring" lines that fly off a struck bell (upright coordinates, relative to the center)
static void spawnRingMark(float rx, float ry, float angDeg) {
  Part* p = allocPart();
  if (!p) return;
  Part q; memset(&q, 0, sizeof(q));
  q.on = true; q.type = 3;
  q.x = CXF + rx; q.y = CYF + ry;
  q.rot = angDeg * DEG;
  q.vx = cosf(q.rot) * 190; q.vy = sinf(q.rot) * 190;
  q.size = 15; q.max = 0.30f;
  *p = q;
}

static bool playAnim(int id, uint32_t now, uint32_t loopUntil = 0) {
  const Anim& A = ANIMS[id];
  if (cur.active && A.pri < ANIMS[cur.id].pri) return false;
  cur.active = true; cur.id = id; cur.idx = 0;
  cur.stepEnd = now + A.s[0].ms; cur.loopUntil = loopUntil;
  if (A.snd) say(A.snd);
  // icons disabled: eyes only
  if (A.fx == FX_STARS) dizzyFx = true;
  return true;
}


// ---- reaction pools: at least 5 choices so nothing feels scripted ----
static int pick(const int* a, int n) { return a[(int)(frand() * n) % n]; }
static const int MAD_POOL[] = {AN_ANGRY, AN_MAD1, AN_MAD2, AN_MAD3, AN_MAD4, AN_MAD5};
static const int SHAKE_POOL[] = {AN_DIZZY, AN_SHAKE1, AN_SHAKE2, AN_SHAKE3, AN_SHAKE4, AN_SHAKE5};
static const int POKE_POOL[] = {AN_POKE_SURPRISE, AN_POKE1, AN_POKE2, AN_POKE3, AN_POKE4, AN_POKE5};
static const int PET_POOL[] = {AN_PET_END, AN_PET1, AN_PET2, AN_PET3, AN_PET4, AN_PET5};
static const int HAPPY_POOL[] = {AN_HAPPY_BOUNCE, AN_HAPPY1, AN_HAPPY2, AN_HAPPY3, AN_HAPPY4, AN_HAPPY5};
static const int SAD_POOL[] = {AN_SAD_DROOP, AN_SAD1, AN_SAD2, AN_SAD3, AN_SAD4, AN_SAD5};
static const int ANNOY_POOL[] = {AN_POKE_ANNOYED, AN_SHAKE_NO, AN_GRUMBLE, AN_MAD1, AN_POKE_OW};


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
static float shakePeak = 0;

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

  // track peak for soft vs hard shake
  if (dev > shakePeak) shakePeak = dev;
  else shakePeak *= 0.995f;
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
// Pages (menus), timer, alarm, ringing
// ============================================================================
enum Page { PG_FACE = 0, PG_COLOR, PG_TIMER, PG_ALARM, PG_SETTIME };
static int page = PG_FACE, shownPage = PG_FACE;
static bool menuOpen = false;
static float menuP = 0, menuV = 0;
static uint32_t menuT0 = 0;

static void setPage(int p, uint32_t now) {
  page = p;
  menuOpen = (p != PG_FACE);
  if (menuOpen) { shownPage = p; cur.active = false; }
  menuT0 = now;
}
static void closeMenu() { page = PG_FACE; menuOpen = false; }

// timer
static bool timerRun = false;
static uint32_t timerEnd = 0, timerLen = 0;
// clock (mirrors the RTC)
static int curHH = 12, curMM = 0, curSS = 0;
static bool rtcTrusted = false;
static int lastAlarmKey = -1;
static int setH = 7, setM = 0;

// ringing: a hammer swings between his eyes and strikes them like the bells of a classic alarm clock
static bool ringing = false;
static int ringKind = 0;  // 0 timer, 1 alarm
static uint32_t ringT0 = 0;
static int ringHalf = -1;
static float hammerAng = 0, hammerVis = 0;
static float ringJit[2] = {0, 0};
static uint32_t ringHitT[2] = {0, 0};
static const float HAMMER_A = 36.0f;  // swing angle (deg)
static const float HAMMER_HALF = 0.12f, RING_BURST = 1.44f, RING_CYCLE = 2.3f;

static void ringHit(int side, uint32_t now) {
  ringHitT[side] = now;
  sp[C_HL + side].v -= 7.0f;
  sp[C_WL + side].v += 3.0f;
  sp[C_YL + side].v += 75.0f;
  say(side == 0 ? snd::S_BELL_L : snd::S_BELL_R);
  float sx = side == 0 ? -1.0f : 1.0f;
  float hx = sx * 66.0f, hy = -128.0f + 91.0f;  // where the hammer head lands
  spawnRingMark(hx, hy - 6, side == 0 ? -150.0f : -30.0f);
  spawnRingMark(hx, hy - 6, -90.0f + sx * 18.0f);
  spawnRingMark(hx, hy - 6, side == 0 ? -200.0f : 20.0f);
}

static void startRing(int kind, uint32_t now) {
  ringing = true; ringKind = kind; ringT0 = now; ringHalf = -1;
  sleeping = false; fallingAsleep = false;
  mind.energy = fmaxf(mind.energy, 0.6f);
  pendingAnim = -1; dizzyFx = false;
  cur.active = false;
  closeMenu();
  lastInteract = now;
}

static void stopRing(uint32_t now, bool byUser) {
  ringing = false;
  lastInteract = now;
  cur.active = false;
  mind.joy = clampf(mind.joy + (byUser ? 0.0f : -0.1f), 0, 1);
  playAnim(byUser ? AN_RELIEF : AN_SIGH, now);
}

static void ringUpdate(uint32_t now, float dt) {
  if (ringing) {
    float t = (now - ringT0) / 1000.0f;
    float tc = fmodf(t, RING_CYCLE);
    float target = 0;
    if (tc < RING_BURST) {
      float u = tc / HAMMER_HALF;
      target = -HAMMER_A * cosf(PI_F * u);
      int k = (int)floorf(u);
      int gk = (int)floorf(t / RING_CYCLE) * 100 + k;
      if (gk != ringHalf) { ringHalf = gk; ringHit(k & 1, now); }
    }
    hammerAng += (target - hammerAng) * clampf(dt * 28.0f, 0, 1);
    if (t > 75.0f) stopRing(now, false);
  } else {
    hammerAng += (0 - hammerAng) * clampf(dt * 10.0f, 0, 1);
  }
  hammerVis += ((ringing ? 1.0f : 0.0f) - hammerVis) * clampf(dt * 9.0f, 0, 1);
  for (int s = 0; s < 2; s++) {
    float tt = (now - ringHitT[s]) / 1000.0f;
    ringJit[s] = (ringHitT[s] != 0 && tt < 0.4f) ? 4.5f * expf(-tt / 0.09f) * sinf(2 * PI_F * 34.0f * tt) : 0.0f;
  }
}

static void clockUpdate(uint32_t now, const Input& in) {
  curHH = in.hh; curMM = in.mm; curSS = in.ss;
  rtcTrusted = in.rtcOK;
  if (timerRun && (int32_t)(now - timerEnd) >= 0) {
    timerRun = false;
    startRing(0, now);
  }
  if (rtcTrusted && !ringing) {
    int key = curHH * 60 + curMM;
    int alarmKey = settings.alarmH * 60 + settings.alarmM;
    if (key != alarmKey) lastAlarmKey = -1;
    if (settings.alarmOn && key == alarmKey && lastAlarmKey != key) {
      lastAlarmKey = key;
      startRing(1, now);
    }
  }
}

// ============================================================================
// Hearing: what he does when the mic notices something
// ============================================================================
static uint32_t micLoudSeen = 0, micVoiceSeen = 0, micNameSeen = 0;
static uint32_t nameCool = 0, voiceCool = 0, listenCool = 0;

static void wakeUp(uint32_t now, bool startled);

static void onLoud(uint32_t now, float amt) {
  if (ringing || menuOpen) return;
  lastInteract = now;
  mind.fear = clampf(mind.fear + 0.3f + 0.5f * amt, 0, 1);
  mind.alert = 1.0f;
  mind.annoy = clampf(mind.annoy + 0.04f, 0, 1);
  if (sleeping) { wakeUp(now, true); return; }
  cur.active = false; pendingAnim = -1;
  if (amt > 0.45f) playAnim(AN_STARTLE_LOUD, now);
  else playAnim(AN_FLINCH, now);
}

static void onName(uint32_t now) {
  if (ringing || menuOpen || now < nameCool) return;
  nameCool = now + 2500;
  lastInteract = now;
  mind.alert = 1.0f;
  mind.joy = clampf(mind.joy + 0.06f, 0, 1);
  mind.bored = 0;
  mind.energy = fmaxf(mind.energy, 0.55f);
  sleeping = false; fallingAsleep = false;
  cur.active = false;
  playAnim(AN_PERK_UP, now);
  pendingAnim = (mind.joy > 0.6f && frand() < 0.6f) ? AN_HAPPY_GREET : -1;
}

static void onVoicePhrase(uint32_t now) {
  if (ringing || menuOpen || sleeping || now < voiceCool) return;
  voiceCool = now + 9000;
  lastInteract = now;
  mind.alert = fmaxf(mind.alert, 0.6f);
  if (!cur.active && frand() < 0.6f) playAnim(AN_CURIOUS, now);  // "hm?" reply
}

static void micPoll(uint32_t now) {
  uint32_t l = snd::mic.loudSeq, v = snd::mic.voiceSeq, n = snd::mic.nameSeq;
  if (l != micLoudSeen) { micLoudSeen = l; onLoud(now, snd::mic.loudAmt); }
  if (n != micNameSeen) { micNameSeen = n; onName(now); }
  if (v != micVoiceSeen) { micVoiceSeen = v; onVoicePhrase(now); }
}

// ============================================================================
// Touch gestures
// ============================================================================
enum Btn { B_NONE = 0, B_MINUS, B_PLUS, B_START, B_DIGITS, B_HUP, B_HDN, B_MUP, B_MDN, B_TOGGLE, B_CLOCK, B_CHECK, B_VOL };

struct TouchTrack {
  bool down; uint32_t t0, lastMove, lastRevT, repeatT;
  float sx, sy, x, y, path, exX, exY;
  int dirX, dirY, rev, btn;
  bool consumed, petting;
};
static TouchTrack T;
static uint32_t lastHeartT = 0, lastPurrT = 0;
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
  if (ringing) return;
  lastInteract = now;
  if (sleeping) { sleeping = false; fallingAsleep = false; mind.energy = fmaxf(mind.energy, 0.5f); }
  cur.active = false;
  // intensity judged later in onShakeEnd; start with mild happy wiggle unless already hard
  if (shakePeak > 1.8f) {
    mind.dizzy = 1.0f;
    playAnim(pick(SHAKE_POOL, 6), now, now + 1200);
  } else {
    mind.joy = clampf(mind.joy + 0.15f, 0, 1);
    mind.love = clampf(mind.love + 0.08f, 0, 1);
    playAnim(pick(HAPPY_POOL, 6), now);
  }
}
static void onShakeEnd(uint32_t now) {
  if (ringing) return;
  lastInteract = now;
  cur.active = false;
  dizzyFx = false;
  if (shakePeak > 2.4f) {
    // very hard: dizzy then eyes "fall off"
    mind.dizzy = 1.0f;
    playAnim(pick(SHAKE_POOL, 6), now, now + 800);
    // schedule fall-off by setting a high pending later; for now recover slowly
  } else if (shakePeak > 1.6f) {
    playAnim(AN_DIZZY_RECOVER, now);
  } else {
    // soft shake already made him happy
    mind.joy = clampf(mind.joy + 0.1f, 0, 1);
  }
  shakePeak *= 0.5f;
}
static void onFall(uint32_t now) {
  if (ringing) return;
  lastInteract = now;
  if (sleeping) { sleeping = false; fallingAsleep = false; }
  cur.active = false;
  mind.annoy = clampf(mind.annoy + 0.1f, 0, 1);
  mind.fear = clampf(mind.fear + 0.6f, 0, 1);
  playAnim(AN_SCARED, now);
}

static void onPoke(uint32_t now, float ux, float uy) {
  if (friendPrompt && tapOnFriendPrompt(ux, uy)) { becomeFriends(); return; }
  lastInteract = now;
  mind.energy = fminf(1.0f, mind.energy + 0.03f);
  mind.alert = fmaxf(mind.alert, 0.5f);
  if (now - lastPokeT < 2200) pokeStreak++; else pokeStreak = 1;
  lastPokeT = now;
  lookAtUntil = now + 900;
  lookAtX = clampf(ux * 0.22f, -34, 34);
  lookAtY = clampf(uy * 0.2f, -24, 24);

  if (pokeStreak >= 5) {
    mind.annoy = clampf(mind.annoy + 0.25f, 0, 1);
    mind.joy = clampf(mind.joy - 0.1f, 0, 1);
    playAnim(pick(MAD_POOL, 6), now);
  } else if (pokeStreak >= 3) {
    mind.annoy = clampf(mind.annoy + 0.15f, 0, 1);
    float r = frand();
    playAnim(pick(ANNOY_POOL, 5), now);
  } else if (mind.annoy > 0.45f) {
    mind.annoy = clampf(mind.annoy + 0.05f, 0, 1);
    playAnim(pick(ANNOY_POOL, 5), now);
  } else {
    mind.joy = clampf(mind.joy + 0.04f, 0, 1);
    float r = frand();
    if (pokeStreak == 2 && mind.joy > 0.5f) playAnim(r < 0.5f ? AN_POKE_GIGGLE : AN_POKE_TICKLE, now);
    else if (mind.joy > 0.7f && r < 0.4f) playAnim(AN_POKE_PLAYFUL, now);
    else if (r < 0.15f) playAnim(AN_POKE_OW, now);
    else playAnim(pick(POKE_POOL, 6), now);
  }
}

static void onPetStart(uint32_t now) { petStart = now; lastHeartT = now; lastInteract = now; lastPurrT = now; }
static void onPetEnd(uint32_t now) {
  lastInteract = now;
  if (now - petStart > 900) {
    if (mind.love > 0.55f) playAnim(frand() < 0.5f ? AN_LOVE : AN_NUZZLE, now);
    else playAnim(pick(PET_POOL, 6), now);
  }
}

static void orientStep();

// ---- buttons ---------------------------------------------------------------
struct BtnDef { int id; float x, y, r; };

static int btnAt(int pg, float ux, float uy) {
  static const BtnDef TIM[] = {{B_START, 0, 97, 56}, {B_MINUS, -112, 97, 44}, {B_PLUS, 112, 97, 44}};
  static const BtnDef ALM[] = {{B_HUP, -71, -87, 36}, {B_HDN, -71, 50, 36}, {B_MUP, 71, -87, 36}, {B_MDN, 71, 50, 36},
                               {B_TOGGLE, 0, 121, 48}, {B_CLOCK, -120, 121, 34}};
  static const BtnDef SET[] = {{B_HUP, -71, -87, 36}, {B_HDN, -71, 50, 36}, {B_MUP, 71, -87, 36}, {B_MDN, 71, 50, 36},
                               {B_CHECK, 0, 121, 48}};
  const BtnDef* L = nullptr; int n = 0;
  if (pg == PG_TIMER) { L = TIM; n = 3; }
  else if (pg == PG_ALARM) { L = ALM; n = 6; }
  else if (pg == PG_SETTIME) { L = SET; n = 5; }
  for (int i = 0; i < n; i++)
    if (hypotf(ux - L[i].x, uy - L[i].y) < L[i].r) return L[i].id;
  if (pg == PG_TIMER && !timerRun && fabsf(ux) < 130 && fabsf(uy + 18) < 56) return B_DIGITS;
  return B_NONE;
}
static bool isRepeat(int b) { return b == B_MINUS || b == B_PLUS || b == B_HUP || b == B_HDN || b == B_MUP || b == B_MDN; }

static void doBtn(int b, uint32_t now, bool held) {
  menuT0 = now;
  if (page == PG_TIMER) {
    int m = settings.timerMin;
    if (b == B_PLUS) { m += (m < 10) ? 1 : 5; if (m > 99) m = 99; settings.timerMin = m; settings.dirty = true; say(snd::S_UP); }
    else if (b == B_MINUS) { m -= (m <= 10) ? 1 : 5; if (m < 1) m = 1; settings.timerMin = m; settings.dirty = true; say(snd::S_DOWN); }
    else if (b == B_DIGITS) {
      static const uint8_t PRE[] = {1, 3, 5, 10, 15, 20, 30, 45, 60};
      int nx = PRE[0];
      for (unsigned i = 0; i < sizeof(PRE); i++) if (PRE[i] > m) { nx = PRE[i]; break; }
      settings.timerMin = nx; settings.dirty = true; say(snd::S_TAP);
    } else if (b == B_START) {
      if (timerRun) { timerRun = false; closeMenu(); playAnim(AN_CANCEL, now); }
      else {
        timerRun = true; timerLen = (uint32_t)settings.timerMin * 60000u; timerEnd = now + timerLen;
        closeMenu(); cur.active = false; playAnim(AN_TIMER_START, now);
      }
    }
  } else if (page == PG_ALARM || page == PG_SETTIME) {
    bool set = (page == PG_SETTIME);
    int h = set ? setH : settings.alarmH, m = set ? setM : settings.alarmM;
    if (b == B_HUP) { h = (h + 1) % 24; say(snd::S_UP); }
    else if (b == B_HDN) { h = (h + 23) % 24; say(snd::S_DOWN); }
    else if (b == B_MUP) { m = (m + 1) % 60; say(snd::S_UP); }
    else if (b == B_MDN) { m = (m + 59) % 60; say(snd::S_DOWN); }
    if (set) { setH = h; setM = m; }
    else if (b == B_HUP || b == B_HDN || b == B_MUP || b == B_MDN) { settings.alarmH = h; settings.alarmM = m; settings.dirty = true; }
    if (b == B_TOGGLE) {
      settings.alarmOn = !settings.alarmOn; settings.dirty = true; lastAlarmKey = -1;
      if (settings.alarmOn) { closeMenu(); playAnim(AN_ALARM_SET, now); }
      else { say(snd::S_CANCEL); }
    } else if (b == B_CLOCK) {
      setH = curHH; setM = curMM; setPage(PG_SETTIME, now); say(snd::S_TAP);
    } else if (b == B_CHECK) {
      clockReq.pending = true; clockReq.h = setH; clockReq.m = setM;
      rtcTrusted = true;
      setPage(PG_ALARM, now); say(snd::S_CONFIRM);
    }
  }
  (void)held;
}

static bool menuHit(float ux, float uy) {  // color page: returns true if something was chosen
  if (hypotf(ux, uy - 80) < 42) {
    settings.vol = (settings.vol + 1) & 3; settings.dirty = true;
    snd::setVolume(settings.vol);
    say(snd::S_CONFIRM);
    return true;
  }
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

static void touchUpdate(uint32_t now, bool down, float rx, float ry) {
  float ux = 0, uy = 0;
  if (down) toUpright(rx, ry, ux, uy);

  if (down && !T.down) {  // ---- finger down
    memset(&T, 0, sizeof(T));
    T.down = true; T.t0 = now; T.lastMove = now;
    T.sx = T.x = ux; T.sy = T.y = uy; T.exX = ux; T.exY = uy;
    lastInteract = now;
    if (ringing) {
      if (now - ringT0 > 500) stopRing(now, true);
      T.consumed = true;
    } else if (sleeping && !menuOpen) {
      wakeUp(now, true); T.consumed = true;
    } else if (menuOpen) {
      menuT0 = now;
      if (page != PG_COLOR) {
        T.btn = btnAt(page, ux, uy);
        if (T.btn && isRepeat(T.btn)) { doBtn(T.btn, now, false); T.repeatT = now + 450; }
      }
    }
  } else if (down && T.down) {  // ---- finger moving
    float d = hypotf(ux - T.x, uy - T.y);
    if (d > 1.5f) T.lastMove = now;
    T.path += d; T.x = ux; T.y = uy;
    axisTrack(ux, T.exX, T.dirX, T.rev, now, T.lastRevT);
    axisTrack(uy, T.exY, T.dirY, T.rev, now, T.lastRevT);
    lastInteract = now;
    if (menuOpen) menuT0 = now;

    // hold a repeating button (+/-, arrows) to keep changing the value
    if (menuOpen && !T.consumed && T.btn && isRepeat(T.btn) && now >= T.repeatT && btnAt(page, ux, uy) == T.btn) {
      doBtn(T.btn, now, true);
      T.repeatT = now + ((now - T.t0) > 1800 ? 55 : 120);
    }

    // hold the middle of the color menu ~1.2 s: turn Charlie's 'up' by 90 degrees
    if (!T.consumed && menuOpen && page == PG_COLOR && now - T.t0 > 1200 && T.path < 30 && hypotf(T.x, T.y) < 40) {
      orientStep(); T.consumed = true; menuT0 = now;
      playAnim(AN_CONFUSED, now);
    }

    if (!T.consumed) {
      float dy = uy - T.sy, dx = ux - T.sx;
      bool horiz = fabsf(dy) < fabsf(dx) * 0.9f, vert = fabsf(dx) < fabsf(dy) * 0.9f;
      if (page == PG_FACE) {
        // menus removed: swipes no longer open pages
        (void)vert; (void)horiz; (void)dy; (void)dx;
      } else if (page == PG_COLOR) {
        if (-dy > 70 && vert) { closeMenu(); T.consumed = true; }
      } else if (page == PG_TIMER) {
        if (dx < -70 && horiz) { closeMenu(); say(snd::S_DOWN); T.consumed = true; }
      } else if (page == PG_ALARM) {
        if (dx > 70 && horiz) { closeMenu(); say(snd::S_DOWN); T.consumed = true; }
      } else if (page == PG_SETTIME) {
        if (dx > 70 && horiz) { setPage(PG_ALARM, now); T.consumed = true; }
      }
    }
  } else if (!down && T.down) {  // ---- finger up
    bool wasPet = T.petting;
    if (T.petting) { T.petting = false; onPetEnd(now); }
    bool shortTap = !T.consumed && !wasPet && (now - T.t0) < 350 && T.path < 28;
    if (page == PG_FACE) {
      if (shortTap) onPoke(now, T.x, T.y);
    } else if (page == PG_COLOR) {
      if (shortTap) { menuT0 = now; menuHit(T.x, T.y); }
    } else if (!T.consumed && T.btn && !isRepeat(T.btn) && T.path < 28 && (now - T.t0) < 900 &&
               btnAt(page, T.x, T.y) == T.btn) {
      doBtn(T.btn, now, false);
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
static uint32_t nextBabble = 20000, nextDream = 0, nextSnore = 0;
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
  // alert (just heard something) and fear: taller, wide-eyed
  t[C_H] *= 1.0f + 0.10f * mind.alert + 0.18f * mind.fear;
  t[C_W] *= 1.0f - 0.06f * mind.fear;
  t[C_GAP] += 6.0f * mind.fear;
  if (sleeping) {
    t[C_H] = 0.17f; t[C_W] = 1.04f; t[C_BL] = t[C_BR] = 0.05f; t[C_LY] = 12; t[C_TILT] = 0; t[C_GAP] = 0;
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
    // v3
    case AN_FIDGET: return 1.0f + b;
    case AN_SLOW_BLINK: return 0.2f + 3 * slp;
    case AN_LONELY: return 2.5f * sad + 2 * b * (1 - hap);
    case AN_SHY_HIDE: return 0.3f + 2 * mind.love;
    case AN_PROUD: return 1.5f * hap;
    case AN_SING: return 2.0f * hap * mind.energy;
    case AN_GRUMBLE: return 3 * mind.annoy;
    case AN_SCAN: return 0.6f + 1.5f * b;
    case AN_JUMP: return 1.5f * hap * mind.energy;
    case AN_FOCUS: return 0.7f;
    case AN_TWITCH: return 0.6f;
    case AN_CHECK_TIME: return (timerRun || settings.alarmOn) ? 3.0f : 0.0f;
    case AN_COZY: return 2 * mind.love;
    case AN_NUZZLE: return 1.2f * mind.love;
    default: return 0;
  }
}

static void pickIdle(uint32_t now) {
  static const int IDLE[] = {AN_LOOK_AROUND, AN_CURIOUS, AN_THINK, AN_PEEK_SHY, AN_DOUBLE_TAKE, AN_EYE_ROLL,
                             AN_YAWN, AN_STRETCH, AN_SIGH, AN_SAD_DROOP, AN_SQUINT_SUS, AN_SMUG, AN_WIGGLE,
                             AN_HAPPY_BOUNCE, AN_EXCITED, AN_WINK_L, AN_WINK_R, AN_LOVE, AN_SNEEZE,
                             AN_CONFUSED, AN_NOD_YES, AN_SHAKE_NO, AN_FIDGET, AN_SLOW_BLINK, AN_LONELY,
                             AN_SHY_HIDE, AN_PROUD, AN_SING, AN_GRUMBLE, AN_SCAN, AN_JUMP, AN_FOCUS,
                             AN_TWITCH, AN_CHECK_TIME, AN_COZY, AN_NUZZLE};
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

static void babble() {  // a quiet mood-matched chirp, now and then
  float sad = clampf((0.42f - mind.joy) / 0.42f, 0, 1);
  float hap = clampf((mind.joy - 0.62f) / 0.38f, 0, 1);
  float r = frand();
  int id;
  if (mind.annoy > 0.4f) id = snd::S_GRUMBLE;
  else if (sad > 0.4f) id = r < 0.6f ? snd::S_SAD : snd::S_SIGH;
  else if (hap > 0.3f) id = r < 0.4f ? snd::S_HAPPY : (r < 0.7f ? snd::S_LOVE : snd::S_BLIP);
  else id = r < 0.4f ? snd::S_THINK : (r < 0.7f ? snd::S_CURIOUS : snd::S_BLIP);
  say(id);
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
  float joyBase = 0.58f + 0.25f * mind.love - 0.35f * mind.bored - 0.45f * mind.annoy - 0.2f * mind.fear;
  mind.joy += (joyBase - mind.joy) * clampf(dt * 0.12f, 0, 1);
  mind.annoy = fmaxf(0, mind.annoy - dt * 0.035f);
  mind.love = fmaxf(0, mind.love - dt * 0.004f);
  mind.dizzy = fmaxf(0, mind.dizzy - dt * 0.3f);
  mind.fear = fmaxf(0, mind.fear - dt * 0.12f);
  mind.alert = fmaxf(0, mind.alert - dt * 0.25f);

  if (petting()) {
    mind.joy = clampf(mind.joy + dt * 0.35f, 0, 1);
    mind.love = clampf(mind.love + dt * 0.22f, 0, 1);
    mind.annoy = fmaxf(0, mind.annoy - dt * 0.1f);
    mind.fear = fmaxf(0, mind.fear - dt * 0.3f);
    // no heart icons
    if (now - lastPurrT > 4500 && frand() < 0.4f) { lastPurrT = now; say(snd::S_BLIP); }  // rare soft chirp only
  }
  if (shaking && cur.active && cur.id == AN_DIZZY) cur.loopUntil = now + 500;

  // ---- hearing
  micPoll(now);

  // ---- animation timing
  animTick(now);
  if (!cur.active && pendingAnim >= 0) { int a = pendingAnim; pendingAnim = -1; playAnim(a, now); }

  bool idleOK = !cur.active && !sleeping && !menuOpen && !petting() && !T.down && !ringing;

  // someone is talking: lean in and listen until they stop
  if (!ringing && !menuOpen && !sleeping && !petting() && !T.down && snd::mic.voiceNow && now > listenCool) {
    if (cur.active && cur.id == AN_LISTEN) cur.loopUntil = now + 450;
    else if (!cur.active) playAnim(AN_LISTEN, now, now + 450);
  } else if (cur.active && cur.id == AN_LISTEN && now >= cur.loopUntil) {
    listenCool = now + 3000;
  }

  // ---- sleep / wake
  if (!sleeping && !fallingAsleep && idleOK && mind.energy < 0.28f && since > 40.0f) {
    fallingAsleep = true;
    playAnim(AN_SLEEP_NOD, now);
  }
  if (sleeping && !cur.active && mind.energy > 0.85f && now - sleepStart > 60000) wakeUp(now, false);
  if (sleeping && now - lastZ > 2400) { lastZ = now; spawnZ(); }
  if (sleeping && !ringing) {
    if (now >= nextSnore) { say(snd::S_SNORE); nextSnore = now + 3600; }
    if (!cur.active && now >= nextDream) { playAnim(AN_DREAM, now); nextDream = now + (uint32_t)frange(9000, 17000); }
  }

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
  if (idleOK && !demoMode && now >= nextBabble) {
    babble();
    nextBabble = now + (uint32_t)frange(16000, 42000);
  }

  // ---- blinking
  if (!sleeping && !petting() && !menuOpen && !ringing && now >= nextBlink) {
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
// Software renderer (RGB565 framebuffer, anti-aliased polygons)
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
  if (a256 <= 0) return;

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

// polygons live in a shared vertex pool
struct Item { V2* p; int n; Shade sh; };
static const int MAX_ITEMS = 220;
static const int POOL_SZ = 4600;
static Item items[MAX_ITEMS];
static V2 pool[POOL_SZ];
static int nItems = 0, poolUsed = 0;
static float extMinY, extMaxY;
static float gCa = 1, gSa = 0;

static Item* newItem(int nv) {
  if (nItems >= MAX_ITEMS || poolUsed + nv > POOL_SZ) return nullptr;
  Item* it = &items[nItems];
  it->p = pool + poolUsed;
  it->n = nv;
  poolUsed += nv;
  return it;
}
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
// center-relative upright coordinates -> screen
static inline void P(float rx, float ry, float& sx, float& sy) {
  sx = CXF + rx * gCa - ry * gSa;
  sy = CYF + rx * gSa + ry * gCa;
}

static void circleItem(float cx, float cy, float r, uint8_t R, uint8_t G, uint8_t B, float alpha, bool grad, float ca, float sa) {
  Item* it = newItem(32);
  if (!it) return;
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
  int nv = (kind == 0) ? 40 : 10;
  Item* it = newItem(nv);
  if (!it) return;
  float cr = cosf(rot), sr = sinf(rot);
  if (kind == 0) {  // heart
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
    for (int i = 0; i < 10; i++) {
      float hx = zx[i] * size, hy = zy[i] * size;
      it->p[i].x = cx + hx * cr - hy * sr;
      it->p[i].y = cy + hx * sr + hy * cr;
    }
  } else {  // star
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

// ---- UI primitives in center-relative upright coordinates ------------------------
struct Col { uint8_t r, g, b; };

static void flatShade(Item* it, Col c, float alpha) {
  it->sh.r = c.r; it->sh.g = c.g; it->sh.b = c.b; it->sh.alpha = alpha; it->sh.grad = false;
  it->sh.g0 = it->sh.gx = it->sh.gy = 0;
}
static void cCircle(float rx, float ry, float r, Col c, float alpha, bool grad = false) {
  float sx, sy; P(rx, ry, sx, sy);
  circleItem(sx, sy, r, c.r, c.g, c.b, alpha, grad, gCa, gSa);
}
static void cStroke(float x0, float y0, float x1, float y1, float w, Col c, float alpha) {  // rounded capsule
  float ax, ay, bx, by;
  P(x0, y0, ax, ay); P(x1, y1, bx, by);
  Item* it = newItem(14);
  if (!it) return;
  float ang = atan2f(by - ay, bx - ax), r = w * 0.5f;
  for (int i = 0; i < 7; i++) {
    float t = ang + PI_F * 0.5f + i * PI_F / 6.0f;
    it->p[i].x = ax + cosf(t) * r; it->p[i].y = ay + sinf(t) * r;
    float u = ang - PI_F * 0.5f + i * PI_F / 6.0f;
    it->p[7 + i].x = bx + cosf(u) * r; it->p[7 + i].y = by + sinf(u) * r;
  }
  flatShade(it, c, alpha);
  commitItem(it);
}
static void cPoly(const float* xy, int n, Col c, float alpha) {
  Item* it = newItem(n);
  if (!it) return;
  for (int i = 0; i < n; i++) P(xy[2 * i], xy[2 * i + 1], it->p[i].x, it->p[i].y);
  flatShade(it, c, alpha);
  commitItem(it);
}
static void cArc(float r0, float r1, float a0, float a1, Col c, float alpha) {  // ring sector, angles in radians
  int segs = (int)((a1 - a0) / (6.0f * DEG)) + 2;
  if (segs > 60) segs = 60;
  Item* it = newItem(2 * (segs + 1));
  if (!it) return;
  for (int i = 0; i <= segs; i++) {
    float a = a0 + (a1 - a0) * i / segs;
    P(cosf(a) * r1, sinf(a) * r1, it->p[i].x, it->p[i].y);
    P(cosf(a) * r0, sinf(a) * r0, it->p[2 * segs + 1 - i].x, it->p[2 * segs + 1 - i].y);
  }
  flatShade(it, c, alpha);
  commitItem(it);
}

static const uint8_t SEGMASK[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

static void drawDigit(float cx, float cy, int mask, Col c, float alpha) {
  const float w = 40, h = 78, t = 9;
  float x0 = cx - w / 2 + t / 2, x1 = cx + w / 2 - t / 2, y0 = cy - h / 2 + t / 2, y1 = cy + h / 2 - t / 2;
  float g = t * 0.62f, gm = g * 0.6f;
  float S[7][4] = {{x0 + g, y0, x1 - g, y0}, {x1, y0 + g, x1, cy - gm}, {x1, cy + gm, x1, y1 - g}, {x0 + g, y1, x1 - g, y1},
                   {x0, cy + gm, x0, y1 - g}, {x0, y0 + g, x0, cy - gm}, {x0 + g, cy, x1 - g, cy}};
  for (int i = 0; i < 7; i++) {
    bool lit = (mask >> i) & 1;
    cStroke(S[i][0], S[i][1], S[i][2], S[i][3], t, c, lit ? alpha : alpha * 0.07f);
  }
}
static void drawTime(float cy, int a, int b, int c2, int d, bool tensBlank, bool colon, Col col, float alpha) {
  drawDigit(-97, cy, tensBlank ? 0 : SEGMASK[a], col, alpha);
  drawDigit(-45, cy, SEGMASK[b], col, alpha);
  drawDigit(45, cy, SEGMASK[c2], col, alpha);
  drawDigit(97, cy, SEGMASK[d], col, alpha);
  if (colon) { cCircle(0, cy - 17, 5, col, alpha); cCircle(0, cy + 17, 5, col, alpha); }
}

static const Col BTN_BG = {40, 45, 60}, DARK = {18, 20, 28};

static void buttonBase(float x, float y, float r, Col ec, bool accent, float alpha) {
  cCircle(x, y, r + 2.5f, ec, alpha * 0.45f);
  if (accent) cCircle(x, y, r, ec, alpha, true);
  else cCircle(x, y, r, BTN_BG, alpha, true);
}
static void iconTri(float x, float y, float r, bool up, Col c, float alpha) {
  float s = up ? -1.0f : 1.0f;
  float pts[6] = {x, y + s * 0.34f * r, x + 0.4f * r, y - s * 0.22f * r, x - 0.4f * r, y - s * 0.22f * r};
  cPoly(pts, 3, c, alpha);
}
static void iconBell(float x, float y, float r, Col c, float alpha, bool slash) {
  static const float H[7][2] = {{0, -0.62f}, {0.18f, -0.55f}, {0.31f, -0.36f}, {0.35f, -0.05f}, {0.42f, 0.22f}, {0.56f, 0.38f}, {0.58f, 0.48f}};
  float pts[28];
  float s = r * 0.95f;
  for (int i = 0; i < 7; i++) { pts[2 * i] = x + H[i][0] * s; pts[2 * i + 1] = y + H[i][1] * s - 3; }
  for (int i = 0; i < 7; i++) { pts[2 * (7 + i)] = x - H[6 - i][0] * s; pts[2 * (7 + i) + 1] = y + H[6 - i][1] * s - 3; }
  cPoly(pts, 14, c, alpha);
  cCircle(x, y + 0.62f * s - 3, 0.13f * s, c, alpha);
  if (slash) cStroke(x - 0.55f * s, y - 0.5f * s, x + 0.55f * s, y + 0.55f * s, r * 0.12f, Col{255, 90, 80}, alpha);
}
static void iconClock(float x, float y, float r, Col c, float alpha) {
  cCircle(x, y, r * 0.62f, c, alpha);
  cCircle(x, y, r * 0.47f, BTN_BG, alpha);
  cStroke(x, y, x, y - 0.3f * r, r * 0.1f, c, alpha);
  cStroke(x, y, x + 0.22f * r, y + 0.1f * r, r * 0.1f, c, alpha);
}
static void iconSpeaker(float x, float y, int vol, Col c, float alpha) {
  float body[12] = {x - 15, y - 6, x - 6, y - 6, x + 5, y - 15, x + 5, y + 15, x - 6, y + 6, x - 15, y + 6};
  cPoly(body, 6, c, alpha);
  for (int i = 0; i < vol; i++) {
    float bx = x + 12 + i * 8, hh = 7 + i * 5;
    cStroke(bx, y - hh, bx, y + hh, 3.5f, c, alpha);
  }
  if (vol == 0) {
    cStroke(x + 11, y - 8, x + 25, y + 8, 3.5f, Col{255, 90, 80}, alpha);
    cStroke(x + 11, y + 8, x + 25, y - 8, 3.5f, Col{255, 90, 80}, alpha);
  }
}

static void updateParts(float dt) {
  for (int i = 0; i < NPARTS; i++) {
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
    } else if (p.type == 3) {
      p.x += p.vx * dt; p.y += p.vy * dt;
      p.vx *= (1.0f - 4.0f * dt); p.vy *= (1.0f - 4.0f * dt);
    } else {
      p.x += p.vx * dt; p.y += p.vy * dt; p.size = 9 + p.life * 5;
    }
    if (p.life >= p.max) p.on = false;
  }
}

static float menuEase() { return menuP < 0 ? 0 : (menuP > 1 ? 1 : menuP); }

static void drawPageUI(int pg, float mp, Col ec) {
  float al = mp;
  if (pg == PG_COLOR) {
    for (int k = 0; k < 8; k++) {
      float ang = k * 45.0f * DEG;
      float rr = MENU_RING * mp;
      float rx = rr * sinf(ang), ry = -rr * cosf(ang);
      float rad = 30.0f * mp;
      if (k == settings.colorIdx) cCircle(rx, ry, rad + 6, Col{255, 255, 255}, 0.9f);
      cCircle(rx, ry, rad, Col{PRESETS[k][0], PRESETS[k][1], PRESETS[k][2]}, 1.0f, true);
      cCircle(rx - rad * 0.32f, ry - rad * 0.45f, rad * 0.2f, Col{255, 255, 255}, 0.45f);
    }
    // volume button under the eyes
    cCircle(0, 80, 34 * mp, BTN_BG, al, true);
    iconSpeaker(-3, 80, settings.vol, ec, al);
    return;
  }
  if (pg == PG_TIMER) {
    int secs = timerRun ? (int)(((int32_t)(timerEnd - gNow) + 999) / 1000) : settings.timerMin * 60;
    if (secs < 0) secs = 0;
    int mm = secs / 60, ss = secs % 60;
    bool colon = !timerRun || ((gNow / 500) & 1) == 0;
    drawTime(-18, mm / 10, mm % 10, ss / 10, ss % 10, false, colon, ec, al);
    buttonBase(0, 97, 54, ec, true, al);
    if (timerRun) {  // stop square
      float sq[8] = {-17, 80, 17, 80, 17, 114, -17, 114};
      cPoly(sq, 4, DARK, al);
    } else {          // play triangle
      float tri[6] = {-12, 73, 24, 97, -12, 121};
      cPoly(tri, 3, DARK, al);
    }
    if (!timerRun) {
      buttonBase(-112, 97, 42, ec, false, al);
      cStroke(-128, 97, -96, 97, 7, ec, al);
      buttonBase(112, 97, 42, ec, false, al);
      cStroke(96, 97, 128, 97, 7, ec, al);
      cStroke(112, 81, 112, 113, 7, ec, al);
    }
    return;
  }
  // alarm / set time
  bool set = (pg == PG_SETTIME);
  int h = set ? setH : settings.alarmH, m = set ? setM : settings.alarmM;
  int h12 = h % 12 == 0 ? 12 : h % 12;
  drawTime(-21, h12 / 10, h12 % 10, m / 10, m % 10, h12 < 10, true, ec, al);
  const float ax[4] = {-71, -71, 71, 71}, ay[4] = {-87, 50, -87, 50};
  for (int i = 0; i < 4; i++) {
    buttonBase(ax[i], ay[i], 32, ec, false, al);
    iconTri(ax[i], ay[i], 32, i == 0 || i == 2, ec, al);
  }
  // sun / moon: AM or PM
  if (h < 12) {
    cCircle(146, -21, 10, ec, al);
    for (int i = 0; i < 8; i++) {
      float a = i * 45.0f * DEG;
      cStroke(146 + cosf(a) * 16, -21 + sinf(a) * 16, 146 + cosf(a) * 21, -21 + sinf(a) * 21, 3.5f, ec, al);
    }
  } else {
    cCircle(146, -21, 14, ec, al);
    cCircle(153, -26, 12, Col{0, 0, 0}, al);
  }
  if (set) {
    buttonBase(0, 121, 46, ec, true, al);
    cStroke(-15, 123, -4, 136, 8, DARK, al);
    cStroke(-4, 136, 18, 108, 8, DARK, al);
  } else {
    buttonBase(0, 121, 46, ec, settings.alarmOn, al);
    iconBell(0, 121, 46, settings.alarmOn ? DARK : ec, al, false);
    // clock button: filled with the accent color while the time still needs setting
    buttonBase(-120, 121, 32, ec, !rtcTrusted, al);
    iconClock(-120, 121, 32, rtcTrusted ? ec : DARK, al);
  }
}

static void buildScene() {
  nItems = 0; poolUsed = 0; extMinY = 1e9f; extMaxY = -1e9f;
  float ca = cosf(gAlpha), sa = sinf(gAlpha);
  gCa = ca; gSa = sa;
  float mp = menuEase();
  float pgS = (shownPage == PG_COLOR) ? 0.5f : (shownPage == PG_FACE ? 1.0f : 0.34f);
  float pgY = (shownPage >= PG_TIMER) ? -150.0f : 0.0f;
  float sM = 1.0f - (1.0f - pgS) * mp;
  float eyeDY = pgY * mp;
  Col ec = {(uint8_t)clampf(curCol[0], 0, 255), (uint8_t)clampf(curCol[1], 0, 255), (uint8_t)clampf(curCol[2], 0, 255)};
  uint8_t cr8 = ec.r, cg8 = ec.g, cb8 = ec.b;
  uint8_t hr = (uint8_t)(cr8 + (255 - cr8) * 0.75f), hg = (uint8_t)(cg8 + (255 - cg8) * 0.75f), hb = (uint8_t)(cb8 + (255 - cb8) * 0.75f);
  float blink = blinkAmt(gNow);
  if (sleeping || petting() || ringing) blink = 0;

  // timer progress: a thin ring around the edge that slowly empties
  if (timerRun && mp < 0.5f && timerLen > 0) {
    float frac = clampf((float)(int32_t)(timerEnd - gNow) / (float)timerLen, 0, 1);
    if (frac > 0.002f) cArc(222, 228, -PI_F / 2, -PI_F / 2 + 2 * PI_F * frac, ec, 0.5f * (1.0f - 2 * mp));
  }

  for (int e = 0; e < 2; e++) {
    float sign = (e == 0) ? -1.0f : 1.0f;
    float wmul = sp[C_W].x * (e == 0 ? sp[C_WL].x : sp[C_WR].x);
    float hmul = sp[C_H].x * (e == 0 ? sp[C_HL].x : sp[C_HR].x);
    float a = 60.0f * wmul * (1.0f + 0.10f * blink) * sM;
    float b = 76.0f * hmul * (1.0f - 0.80f * blink) * sM;
    if (b < 11.0f * sM) b = 11.0f * sM;
    float ex = CXF + sign * (95.0f + sp[C_GAP].x) * sM + sp[C_LX].x * sM + ringJit[e] * sM;
    float ey = CYF + sp[C_LY].x * sM + (e == 0 ? sp[C_YL].x : sp[C_YR].x) * sM + eyeDY;
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

    Item* body = newItem(NP);
    if (!body) break;
    makeSuper(body->p, NP, 1, a, b, expo, 0, 0, a, b, bend, shear, rot, scx, scy);
    body->sh.r = cr8; body->sh.g = cg8; body->sh.b = cb8; body->sh.alpha = 1.0f; body->sh.grad = true;
    float dvx = -sa, dvy = ca;
    float bref = fmaxf(b, 18.0f * sM);
    body->sh.g0 = -(scx * dvx + scy * dvy) / bref;
    body->sh.gx = dvx / bref; body->sh.gy = dvy / bref;
    commitItem(body);

    // soft broad highlight + crisp small highlight (light from upper left), they bend with the eye
    Item* h1 = newItem(NP / 2);
    if (h1) {
      makeSuper(h1->p, NP / 2, 2, 0.62f * a, 0.30f * b, 2.4f, -0.12f * a, -0.42f * b, a, b, bend, shear, rot, scx, scy);
      h1->sh.r = hr; h1->sh.g = hg; h1->sh.b = hb; h1->sh.alpha = 0.14f; h1->sh.grad = false;
      h1->sh.g0 = h1->sh.gx = h1->sh.gy = 0;
      commitItem(h1);
    }
    Item* h2 = newItem(NP / 2);
    if (h2) {
      makeSuper(h2->p, NP / 2, 2, 0.24f * a, 0.12f * b, 2.4f, -0.32f * a, -0.58f * b, a, b, bend, shear, rot, scx, scy);
      h2->sh.r = hr; h2->sh.g = hg; h2->sh.b = hb; h2->sh.alpha = 0.55f; h2->sh.grad = false;
      h2->sh.g0 = h2->sh.gx = h2->sh.gy = 0;
      commitItem(h2);
    }

    // swirlies when dizzy so you can tell immediately
    if (mind.dizzy > 0.35f || dizzyFx || shaking) {
      float spin = (gNow % 1200) / 1200.0f * 2 * PI_F * (e == 0 ? 1 : -1);
      float rr = fminf(a, b) * 0.55f;
      for (int s = 0; s < 2; s++) {
        float a0 = spin + s * PI_F;
        float a1 = a0 + 1.4f;
        cArc(rr * 0.25f, rr, a0, a1, Col{255, 255, 255}, 0.75f);
      }
    }
  }

  // ---- the alarm-clock hammer: pivots above his eyes and strikes them like bells
  if (hammerVis > 0.02f) {
    float th = hammerAng * DEG;
    float slide = (1.0f - hammerVis) * -90.0f;  // slides in from above
    float px = 0, py = -128.0f + slide;
    float hx = px + 112.0f * sinf(th), hy = py + 112.0f * cosf(th);
    float al = clampf(hammerVis * 1.4f, 0, 1);
    cStroke(px, py, hx, hy, 9, Col{140, 146, 164}, al);
    cCircle(hx, hy, 18, Col{228, 231, 240}, al, true);
    cCircle(hx - 6, hy - 7, 5, Col{255, 255, 255}, al * 0.6f);
    cCircle(px, py, 9, Col{105, 111, 130}, al, true);
  }

  // ---- friend prompt: ? icon when a new Charlie is nearby
  if (friendPrompt && !haveFriend) {
    float al = 0.9f;
    cCircle(0, -140, 28, Col{255, 220, 80}, al, true);
    cCircle(0, -140, 22, Col{30, 30, 40}, al);
    // simple ? made of strokes
    cArc(8, 14, -2.2f, 0.4f, Col{255, 230, 100}, al);
    cCircle(0, -128, 3.5f, Col{255, 230, 100}, al);
  }

  // ---- particles
  for (int i = 0; i < NPARTS; i++) {
    Part& p = parts[i];
    if (!p.on) continue;
    float al = fminf(1.0f, fminf(p.life / 0.3f, (p.max - p.life) / 0.5f));
    if (p.type == 3) al = clampf(1.0f - p.life / p.max, 0, 1);
    if (al <= 0) continue;
    float sx, sy;
    toScreen(p.x, p.y, ca, sa, sx, sy);
    if (p.type == 0) shapeItem(0, sx, sy, p.size, gAlpha, 255, 100, 150, al * 0.92f);
    else if (p.type == 1) shapeItem(1, sx, sy, p.size, gAlpha, 150, 170, 240, al * 0.85f);
    else if (p.type == 2) shapeItem(2, sx, sy, p.size, p.rot + gAlpha, 255, 225, 80, al * 0.95f);
    else {
      float dx = cosf(p.rot) * p.size, dy = sinf(p.rot) * p.size;
      cStroke(p.x - CXF, p.y - CYF, p.x - CXF + dx, p.y - CYF + dy, 5, Col{255, 236, 150}, al);
    }
  }

  // ---- menu pages
  if (mp > 0.02f) drawPageUI(shownPage, mp, ec);
}

// ============================================================================
// ESP-NOW peer mood (rubs off on local mind)
// ============================================================================
static void peerMood(float joy, float energy, float annoy, float love) {
  float k = haveFriend ? 0.28f : 0.12f;
  mind.joy = clampf(mind.joy + (joy - mind.joy) * k, 0, 1);
  mind.energy = clampf(mind.energy + (energy - mind.energy) * k, 0, 1);
  mind.annoy = clampf(mind.annoy + (annoy - mind.annoy) * k, 0, 1);
  mind.love = clampf(mind.love + (love - mind.love) * k, 0, 1);
  lastInteract = gNow;
  if (!haveFriend) return;
  if (gNow - lastPeerInteract < 2500) return;
  lastPeerInteract = gNow;
  if (joy > 0.7f && !cur.active && frand() < 0.55f) playAnim(pick(HAPPY_POOL, 6), gNow);
  else if (annoy > 0.55f && !cur.active && frand() < 0.4f) playAnim(pick(MAD_POOL, 6), gNow);
  else if (!cur.active && frand() < 0.35f) {
    int r = (int)(frand() * 3);
    playAnim(r == 0 ? AN_PEER_LOOK : (r == 1 ? AN_PEER_WAVE : AN_PEER_SYNC), gNow);
  }
}


// ---- friend prompt (new Charlie nearby) ----
static bool friendPrompt = false;
static uint8_t friendMac[6] = {0};
static bool haveFriend = false;
static uint32_t friendPromptT = 0;
static uint32_t lastPeerInteract = 0;

static void onNewPeer(const uint8_t* mac) {
  if (haveFriend) return;
  memcpy(friendMac, mac, 6);
  friendPrompt = true;
  friendPromptT = gNow;
  playAnim(AN_FRIEND_ASK, gNow);
  say(snd::S_CURIOUS);
}
static void becomeFriends() {
  haveFriend = true;
  friendPrompt = false;
  mind.joy = clampf(mind.joy + 0.2f, 0, 1);
  mind.love = clampf(mind.love + 0.15f, 0, 1);
  playAnim(AN_FRIEND_YES, gNow);
  say(snd::S_HAPPY);
}
static bool tapOnFriendPrompt(float ux, float uy) {
  // ? sits at upper center
  float dx = ux - 0, dy = uy - (-140);
  return dx*dx + dy*dy < 50*50;
}

// ============================================================================
// Public API
// ============================================================================
static int prevMinY = 0, prevMaxY = SCR_H - 1;

static void init(uint16_t* framebuffer, uint32_t seed) {
  fb = framebuffer;
  memset(fb, 0, (size_t)SCR_W * SCR_H * 2);
  rngState = seed ? seed : 0x1234567u;
  snd::E.rng ^= rngState;
  for (int i = 0; i < NP; i++) {
    float t = i * 2 * PI_F / NP;
    tabC[i] = cosf(t); tabS[i] = sinf(t);
  }
  buildAnims();
  mapSounds();
  for (int c = 0; c < C_COUNT; c++) { sp[c].x = sp[c].t = NEUTRAL[c]; sp[c].v = 0; }
  sp[C_H].x = 0.17f;
  memset(parts, 0, sizeof(parts));
  memset(&T, 0, sizeof(T));
  for (int c = 0; c < 3; c++) curCol[c] = PRESETS[settings.colorIdx & 7][c];
  snd::setVolume(settings.vol);
  prevMinY = 0; prevMaxY = SCR_H - 1;
}

static void start(uint32_t now) {
  gNow = now; lastInteract = now;
  nextIdle = now + 5000; nextBlink = now + 3000; nextSaccade = now + 1500; nextBabble = now + 20000;
  nextDream = now + 8000; nextSnore = now + 2000;
  playAnim(AN_BOOT, now);
}

static void update(uint32_t now, float dt, const Input& in) {
  gNow = now;
  dt = clampf(dt, 0.001f, 0.05f);
  snd::setVolume(settings.vol);
  if (in.imu) imuUpdate(now, dt, in.ax, in.ay, in.az);
  clockUpdate(now, in);
  ringUpdate(now, dt);
  touchUpdate(now, in.touch, in.tx, in.ty);

  // menu spring + auto close
  uint32_t autoClose = (page == PG_COLOR) ? 12000 : 25000;
  if (menuOpen && now - menuT0 > autoClose) closeMenu();
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
  if (ringing) {
    memcpy(tg, NEUTRAL, sizeof(tg));
    tg[C_W] = 1.04f; tg[C_H] = 1.08f; tg[C_GAP] = 2;
    springK = 650.0f; springZ = 0.30f;
  } else if (animOn) {
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
  if (petting()) tg[C_H] *= 1.0f + 0.05f * sinf(tsec * 9.0f);  // purring
  tg[C_LX] += 2.2f * sinf(tsec * 0.9f) + 1.3f * sinf(tsec * 2.1f + 1.0f);
  tg[C_LY] += 1.6f * sinf(tsec * 0.7f + 2.0f);
  if (!sleeping && !ringing) tg[C_ROLL] += 2.0f * sinf(tsec * 0.37f);
  if (mind.fear > 0.25f && !animOn) tg[C_LX] += 1.6f * mind.fear * sinf(tsec * 38.0f);  // trembling

  if (!animOn && !sleeping && !menuOpen && !petting() && !ringing) {
    float ax = gazeX, ay = gazeY;
    if (snd::mic.voiceNow) { ax = 0; ay = -3; }
    if (T.down) {
      ax = clampf(T.x * 0.22f, -34, 34); ay = clampf(T.y * 0.2f, -24, 24);
    } else if (now < lookAtUntil) {
      ax = lookAtX; ay = lookAtY;
    }
    tg[C_LX] += ax; tg[C_LY] += ay;
  }
  if (menuOpen || ringing) { tg[C_LX] = 0; tg[C_LY] = 0; }

  for (int c = 0; c < C_COUNT; c++) sp[c].t = tg[c];
  physics(dt);
  updateParts(dt);
}

// Draws the frame into the framebuffer. Returns the rows that changed:
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

// ---- helpers for the serial console / tests
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
static void testRing(int kind) { startRing(kind, gNow); }
static void openPage(int p) { setPage(p, gNow); }
static bool isRinging() { return ringing; }

}  // namespace ch
