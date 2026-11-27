// Charlie sound: soft R2-D2-style voice synth, alarm bells, and microphone analysis.
// Hardware free (no Arduino/IDF calls) so it can be tested on a PC.
//
//   audio task:  mic block in  -> snd::micFeed()      (loud-noise / voice / name-like detection)
//                              <- snd::render()       (speaker block out)
//   main loop:   snd::play(id) requests a sound, snd::mic.* sequence counters report mic events
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace snd {

static const int SR = 16000;  // sample rate
static const float TWO_PI = 6.2831853f;

enum Id : uint8_t {
  S_NONE = 0, S_BOOT, S_HAPPY, S_GIGGLE, S_CURIOUS, S_SAD, S_ANGRY, S_SURPRISE, S_SCARED, S_YES, S_NO,
  S_DIZZY, S_YAWN, S_SIGH, S_PURR, S_LOVE, S_SNORE, S_WAKE, S_THINK, S_SNEEZE, S_BLIP, S_SING, S_PERK,
  S_LISTEN, S_GRUMBLE, S_EXCITED, S_TAP, S_UP, S_DOWN, S_CONFIRM, S_CANCEL, S_ALARMSET, S_OUCH, S_TICKLE,
  S_PROUD, S_SHY, S_SCAN, S_RELIEF, S_BELL_L, S_BELL_R, S_COUNT
};

struct Note {
  uint16_t f0, f1, ms;   // start/end frequency (Hz) and length
  uint8_t amp, fm, vib;  // loudness 0-255, FM bleep depth (x0.1), vibrato depth (Hz)
  uint8_t flags;
  uint16_t gap;          // silence after the note (ms)
};
enum { F_GROWL = 1, F_NOISE = 2 };
#define NT(f0, f1, ms, amp, fm, vib, fl, gap) {f0, f1, ms, amp, fm, vib, fl, gap}

// ---- phrases (all kept quiet and round: sine + a little FM, soft attack and release) ----------
static const Note P_BOOT[] = {NT(400, 900, 110, 150, 10, 0, 0, 20), NT(1100, 1500, 90, 140, 8, 0, 0, 30),
                              NT(900, 700, 70, 130, 6, 0, 0, 25), NT(1300, 1900, 130, 150, 12, 6, 0, 0)};
static const Note P_HAPPY[] = {NT(700, 1100, 70, 150, 8, 0, 0, 15), NT(900, 1400, 70, 150, 8, 0, 0, 15),
                               NT(1100, 1800, 120, 150, 10, 10, 0, 0)};
static const Note P_GIGGLE[] = {NT(900, 1250, 45, 130, 6, 0, 0, 35), NT(1000, 1400, 45, 130, 6, 0, 0, 35),
                                NT(1100, 1550, 45, 130, 6, 0, 0, 35), NT(1250, 1800, 70, 130, 6, 6, 0, 0)};
static const Note P_CURIOUS[] = {NT(500, 900, 140, 140, 8, 0, 0, 40), NT(900, 1800, 190, 150, 10, 0, 0, 0)};
static const Note P_SAD[] = {NT(700, 350, 380, 120, 4, 8, 0, 40), NT(450, 250, 460, 110, 3, 10, 0, 0)};
static const Note P_ANGRY[] = {NT(190, 140, 170, 150, 0, 0, F_GROWL, 50), NT(230, 120, 260, 150, 0, 0, F_GROWL, 0)};
static const Note P_SURPRISE[] = {NT(800, 1900, 90, 160, 10, 0, 0, 20), NT(1900, 1700, 120, 150, 6, 14, 0, 0)};
static const Note P_SCARED[] = {NT(1300, 1700, 60, 130, 10, 25, 0, 20), NT(1400, 1800, 60, 130, 10, 25, 0, 20),
                                NT(1300, 1700, 60, 130, 10, 25, 0, 20), NT(1500, 1300, 120, 120, 10, 30, 0, 0)};
static const Note P_YES[] = {NT(700, 900, 80, 150, 4, 0, 0, 45), NT(1000, 1350, 120, 150, 4, 0, 0, 0)};
static const Note P_NO[] = {NT(450, 300, 150, 140, 6, 0, 0, 45), NT(380, 230, 210, 140, 6, 0, 0, 0)};
static const Note P_DIZZY[] = {NT(500, 1000, 260, 130, 12, 40, 0, 0), NT(1000, 500, 260, 130, 12, 40, 0, 0),
                               NT(500, 900, 220, 120, 12, 40, 0, 0)};
static const Note P_YAWN[] = {NT(420, 260, 700, 100, 3, 6, F_NOISE, 0)};
static const Note P_SIGH[] = {NT(520, 300, 450, 90, 2, 0, F_NOISE, 0)};
static const Note P_PURR[] = {NT(190, 210, 650, 85, 0, 0, F_GROWL, 0)};
static const Note P_LOVE[] = {NT(800, 1200, 200, 130, 4, 12, 0, 20), NT(1200, 1000, 280, 130, 4, 14, 0, 0)};
static const Note P_SNORE[] = {NT(150, 110, 520, 60, 0, 0, F_GROWL | F_NOISE, 0)};
static const Note P_WAKE[] = {NT(500, 700, 100, 120, 3, 0, 0, 30), NT(800, 1200, 150, 140, 6, 0, 0, 0)};
static const Note P_THINK[] = {NT(600, 700, 60, 110, 8, 20, 0, 30), NT(900, 700, 60, 110, 8, 20, 0, 30),
                               NT(700, 1000, 90, 110, 8, 20, 0, 0)};
static const Note P_SNEEZE[] = {NT(500, 1500, 70, 150, 10, 0, 0, 80), NT(1500, 300, 170, 160, 10, 0, F_NOISE, 0)};
static const Note P_BLIP[] = {NT(1500, 1800, 55, 130, 6, 0, 0, 0)};
static const Note P_PERK[] = {NT(900, 1200, 60, 150, 6, 0, 0, 25), NT(1300, 2100, 160, 160, 10, 0, 0, 0)};
static const Note P_LISTEN[] = {NT(1100, 1350, 75, 100, 5, 0, 0, 0)};
static const Note P_GRUMBLE[] = {NT(240, 180, 300, 110, 2, 0, F_GROWL, 50), NT(200, 160, 280, 100, 2, 0, F_GROWL, 0)};
static const Note P_EXCITED[] = {NT(600, 800, 55, 150, 6, 0, 0, 10), NT(800, 1000, 55, 150, 6, 0, 0, 10),
                                 NT(1000, 1300, 55, 150, 6, 0, 0, 10), NT(1300, 1700, 55, 150, 6, 0, 0, 10),
                                 NT(1700, 2200, 130, 160, 8, 8, 0, 0)};
static const Note P_TAP[] = {NT(1400, 1650, 35, 110, 3, 0, 0, 0)};
static const Note P_UP[] = {NT(1100, 1500, 55, 120, 4, 0, 0, 0)};
static const Note P_DOWN[] = {NT(1500, 1100, 55, 120, 4, 0, 0, 0)};
static const Note P_CONFIRM[] = {NT(900, 1200, 70, 140, 4, 0, 0, 20), NT(1300, 1800, 110, 150, 4, 0, 0, 0)};
static const Note P_CANCEL[] = {NT(900, 500, 150, 130, 4, 0, 0, 0)};
static const Note P_ALARMSET[] = {NT(1000, 1000, 70, 130, 3, 0, 0, 25), NT(1400, 1400, 70, 130, 3, 0, 0, 25),
                                  NT(1800, 1800, 130, 140, 3, 8, 0, 0)};
static const Note P_OUCH[] = {NT(1600, 900, 90, 150, 8, 12, 0, 30), NT(700, 500, 130, 120, 4, 10, 0, 0)};
static const Note P_TICKLE[] = {NT(1000, 1400, 40, 130, 6, 0, 0, 25), NT(1100, 1500, 40, 130, 6, 0, 0, 25),
                                NT(1000, 1400, 40, 130, 6, 0, 0, 25), NT(1200, 1650, 40, 130, 6, 0, 0, 25),
                                NT(1100, 1500, 40, 130, 6, 0, 0, 25), NT(1300, 1900, 90, 130, 6, 10, 0, 0)};
static const Note P_PROUD[] = {NT(700, 900, 90, 140, 4, 0, 0, 25), NT(900, 1300, 90, 140, 4, 0, 0, 25),
                               NT(1300, 1300, 220, 140, 4, 8, 0, 0)};
static const Note P_SHY[] = {NT(1300, 1000, 130, 100, 3, 8, 0, 30), NT(1000, 800, 150, 90, 3, 8, 0, 0)};
static const Note P_SCAN[] = {NT(500, 1200, 420, 90, 6, 0, 0, 40), NT(1200, 500, 420, 90, 6, 0, 0, 0)};
static const Note P_RELIEF[] = {NT(800, 500, 320, 100, 3, 0, F_NOISE, 60), NT(600, 900, 160, 120, 4, 0, 0, 0)};

struct Phrase { const Note* n; uint8_t cnt; uint8_t pri; };
#define PHR(a, pri) {a, (uint8_t)(sizeof(a) / sizeof(Note)), pri}
static const Phrase PH[S_COUNT] = {
  {nullptr, 0, 0}, PHR(P_BOOT, 1), PHR(P_HAPPY, 1), PHR(P_GIGGLE, 1), PHR(P_CURIOUS, 1), PHR(P_SAD, 1),
  PHR(P_ANGRY, 1), PHR(P_SURPRISE, 2), PHR(P_SCARED, 2), PHR(P_YES, 1), PHR(P_NO, 1), PHR(P_DIZZY, 1),
  PHR(P_YAWN, 1), PHR(P_SIGH, 1), PHR(P_PURR, 0), PHR(P_LOVE, 1), PHR(P_SNORE, 0), PHR(P_WAKE, 1),
  PHR(P_THINK, 1), PHR(P_SNEEZE, 1), PHR(P_BLIP, 0), {nullptr, 0, 1}, PHR(P_PERK, 2), PHR(P_LISTEN, 0),
  PHR(P_GRUMBLE, 1), PHR(P_EXCITED, 1), PHR(P_TAP, 0), PHR(P_UP, 0), PHR(P_DOWN, 0), PHR(P_CONFIRM, 1),
  PHR(P_CANCEL, 1), PHR(P_ALARMSET, 1), PHR(P_OUCH, 1), PHR(P_TICKLE, 1), PHR(P_PROUD, 1), PHR(P_SHY, 1),
  PHR(P_SCAN, 0), PHR(P_RELIEF, 1), {nullptr, 0, 3}, {nullptr, 0, 3}};

// ============================================================================
// Engine
// ============================================================================
struct Bell {
  bool on;
  float t, f;
  float ph[4], env[4];
};

struct Engine {
  const Note* seq = nullptr;
  int cnt = 0, idx = 0, pri = 0;
  int noteLeft = 0, noteTotal = 0, gapLeft = 0;
  bool inGap = false;
  float ph = 0, vph = 0, gph = 0;
  uint32_t rng = 0x2545F491u;
  Note sing[9];
  Bell bells[4];
  uint8_t qid[8];
  volatile uint8_t qh = 0, qt = 0;
  volatile int vol = 2;           // 0 mute .. 3 loud
  volatile bool active = false;   // true while anything audible is playing
  int holdoff = 0;                // samples of "still audible" tail after the last sound
};
static Engine E;

static inline float nrand() {
  E.rng ^= E.rng << 13; E.rng ^= E.rng >> 17; E.rng ^= E.rng << 5;
  return (E.rng & 0xFFFF) / 32768.0f - 1.0f;
}

static void setVolume(int v) { E.vol = v < 0 ? 0 : (v > 3 ? 3 : v); }

static void play(Id id) {
  uint8_t nh = (E.qh + 1) & 7;
  if (nh == E.qt) return;  // queue full: drop
  E.qid[E.qh] = id;
  E.qh = nh;
}

static float volGain() {
  static const float G[4] = {0.0f, 0.22f, 0.42f, 0.70f};
  return G[E.vol & 3];
}
static float bellGain() {  // alarms are always audible, even when muted
  static const float G[4] = {0.50f, 0.50f, 0.58f, 0.80f};
  return G[E.vol & 3];
}

static void startNote() {
  const Note& n = E.seq[E.idx];
  E.noteTotal = E.noteLeft = (int)n.ms * SR / 1000;
  E.inGap = false;
  E.vph = 0;
}

static void startPhrase(Id id) {
  const Phrase* p = &PH[id];
  if (id == S_SING) {
    static const uint16_t SC[8] = {523, 587, 659, 784, 880, 1047, 1175, 1319};
    int n = 6 + (int)((nrand() + 1.0f) * 1.5f);
    if (n > 9) n = 9;
    int k = 3;
    for (int i = 0; i < n; i++) {
      k += (int)(nrand() * 2.2f);
      if (k < 0) k = 1;
      if (k > 7) k = 6;
      uint16_t f = SC[k];
      E.sing[i] = Note{f, (uint16_t)(f + (nrand() > 0 ? 30 : -30)), (uint16_t)(90 + (int)((nrand() + 1) * 45)), 120, 4, 8, 0,
                       (uint16_t)(20 + (int)((nrand() + 1) * 20))};
    }
    E.seq = E.sing; E.cnt = n; E.pri = 1;
  } else {
    if (!p->n) return;
    if (E.seq && p->pri < E.pri) return;  // don't let a quiet sound cut a more important one
    E.seq = p->n; E.cnt = p->cnt; E.pri = p->pri;
  }
  E.idx = 0;
  E.ph = 0;
  startNote();
}

static void strikeBell(float f) {
  static const float R[4] = {1.0f, 2.76f, 5.40f, 8.93f};
  static const float A[4] = {1.0f, 0.50f, 0.24f, 0.10f};
  int slot = 0;
  for (int i = 0; i < 4; i++) {
    if (!E.bells[i].on) { slot = i; break; }
    if (E.bells[i].t > E.bells[slot].t) slot = i;
  }
  Bell& b = E.bells[slot];
  b.on = true; b.t = 0; b.f = f;
  for (int k = 0; k < 4; k++) { b.ph[k] = 0; b.env[k] = A[k]; }
  (void)R;
}

static float bellSample() {
  static const float R[4] = {1.0f, 2.76f, 5.40f, 8.93f};
  static const float TAU[4] = {0.85f, 0.55f, 0.32f, 0.18f};  // seconds
  float out = 0;
  for (int i = 0; i < 4; i++) {
    Bell& b = E.bells[i];
    if (!b.on) continue;
    float s = 0;
    for (int k = 0; k < 4; k++) {
      b.ph[k] += TWO_PI * b.f * R[k] / SR;
      if (b.ph[k] > TWO_PI) b.ph[k] -= TWO_PI;
      b.env[k] *= expf(-1.0f / (TAU[k] * SR));
      s += b.env[k] * sinf(b.ph[k]);
    }
    float att = b.t / 0.004f;  // soft 4 ms strike, no click
    if (att > 1) att = 1;
    b.t += 1.0f / SR;
    out += s * att * 0.42f;
    if (b.t > 2.2f) b.on = false;
  }
  return out;
}

static float voiceSample() {
  if (!E.seq) return 0;
  if (E.inGap) {
    if (--E.gapLeft <= 0) {
      E.idx++;
      if (E.idx >= E.cnt) { E.seq = nullptr; E.pri = 0; return 0; }
      startNote();
    }
    return 0;
  }
  const Note& n = E.seq[E.idx];
  float u = 1.0f - (float)E.noteLeft / (float)E.noteTotal;
  float f = n.f0 + (n.f1 - n.f0) * u;
  if (n.vib) { E.vph += TWO_PI * 11.0f / SR; f += n.vib * sinf(E.vph); }
  E.ph += TWO_PI * f / SR;
  if (E.ph > TWO_PI) E.ph -= TWO_PI;
  float s = sinf(E.ph + n.fm * 0.1f * sinf(2.0f * E.ph));
  s += 0.16f * sinf(3.0f * E.ph);
  if (n.flags & F_GROWL) {
    E.gph += TWO_PI * 52.0f / SR;
    if (E.gph > TWO_PI) E.gph -= TWO_PI;
    float saw = E.ph / TWO_PI * 2.0f - 1.0f;
    s = 1.7f * (0.45f * s + 0.35f * saw) * (0.65f + 0.35f * sinf(E.gph));
  }
  if (n.flags & F_NOISE) s = 0.6f * s + 0.35f * nrand();
  int el = E.noteTotal - E.noteLeft;
  float att = el / (0.008f * SR);
  if (att > 1) att = 1;
  float rel = E.noteLeft / (0.020f * SR);
  if (rel > 1) rel = 1;
  float env = att * rel;
  env = env * env * (3.0f - 2.0f * env);
  s *= env * (n.amp / 255.0f);
  if (--E.noteLeft <= 0) {
    if (n.gap) { E.inGap = true; E.gapLeft = (int)n.gap * SR / 1000; }
    else {
      E.idx++;
      if (E.idx >= E.cnt) { E.seq = nullptr; E.pri = 0; }
      else startNote();
    }
  }
  return s;
}

// Fills n mono samples for the speaker.
static void render(int16_t* out, int n) {
  while (E.qt != E.qh) {
    Id id = (Id)E.qid[E.qt];
    E.qt = (E.qt + 1) & 7;
    if (id == S_BELL_L) strikeBell(1318.5f);
    else if (id == S_BELL_R) strikeBell(1568.0f);
    else if (id != S_NONE && E.vol > 0) startPhrase(id);
  }
  float g = volGain();
  float gb = bellGain();
  bool any = (E.seq != nullptr);
  for (int i = 0; i < n; i++) {
    float v = voiceSample() * g;
    float b = bellSample() * gb;
    float x = v + b;
    x = tanhf(x * 1.1f) * 0.92f;  // gentle limiter: loud but never harsh
    int s = (int)(x * 30000.0f);
    out[i] = (int16_t)s;
  }
  for (int i = 0; i < 4; i++) if (E.bells[i].on) any = true;
  if (any) E.holdoff = SR / 5;
  else if (E.holdoff > 0) E.holdoff -= n;
  E.active = any || E.holdoff > 0;
}

// ============================================================================
// Microphone analysis (16 kHz mono, 256-sample hops = 16 ms)
// ============================================================================
static const int HOP = 256;

// ---- tuning knobs (raise a threshold to make him less jumpy) ----
#ifndef MIC_LOUD_ABS
#define MIC_LOUD_ABS 2600.0f   // minimum RMS of a "bang" (int16 units)
#endif
#ifndef MIC_LOUD_RATIO
#define MIC_LOUD_RATIO 5.0f    // bang must be this many times louder than the last ~100 ms
#endif
#ifndef MIC_VOICE_ABS
#define MIC_VOICE_ABS 380.0f   // minimum RMS to count as speech-like
#endif

struct MicOut {
  volatile uint32_t loudSeq = 0;   // +1 for every loud noise (bang, clap)
  volatile float loudAmt = 0;      // 0..1 how loud
  volatile uint32_t voiceSeq = 0;  // +1 for every spoken phrase
  volatile uint32_t nameSeq = 0;   // +1 for every "Char-lie"-shaped word
  volatile bool voiceNow = false;  // someone is talking right now
  volatile float rms = 0, floorv = 60;
};
static MicOut mic;

struct MicState {
  float hpx = 0, hpy = 0;
  float win[512];
  bool winInit = false;
  float hist[7];
  int hn = 0;
  float floorv = 60.0f;
  int hold = 0;
  uint32_t loudCool = 0;
  uint32_t t = 0;  // hops
  // speech segmenter
  bool inSpeech = false;
  int actRun = 0, quiet = 0, tot = 0, voiced = 0, peaks = 0;
  float env = 0, localMax = 0, localMin = 0;
  bool falling = false;
  float peakVal[4];
  int lastPeakT = 0;
};
static MicState M;

static float voicing(const float* w512) {
  float d[256];
  float mean = 0;
  for (int i = 0; i < 256; i++) { d[i] = 0.5f * (w512[2 * i] + w512[2 * i + 1]); mean += d[i]; }
  mean /= 256.0f;
  float e0 = 1e-3f;
  for (int i = 0; i < 256; i++) { d[i] -= mean; e0 += d[i] * d[i]; }
  float best = 0;
  for (int lag = 26; lag <= 100; lag++) {
    float c = 0;
    for (int i = 0; i + lag < 256; i++) c += d[i] * d[i + lag];
    c = c / e0 * (256.0f / (256.0f - lag));
    if (c > best) best = c;
  }
  return best;
}

static void speechReset() {
  M.inSpeech = false; M.actRun = 0; M.quiet = 0; M.tot = 0; M.voiced = 0; M.peaks = 0;
  M.env = 0; M.localMax = 0; M.localMin = 0; M.falling = false; mic.voiceNow = false;
}

// selfActive: the speaker is (or just was) making sound, so the mic would only hear Charlie himself.
static void micFeed(const int16_t* in, int n, bool selfActive) {
  if (n != HOP) return;
  if (!M.winInit) { memset(M.win, 0, sizeof(M.win)); memset(M.hist, 0, sizeof(M.hist)); M.winInit = true; }
  memmove(M.win, M.win + HOP, HOP * sizeof(float));
  double sq = 0;
  for (int i = 0; i < HOP; i++) {
    float x = (float)in[i];
    float y = x - M.hpx + 0.995f * M.hpy;  // DC blocker
    M.hpx = x; M.hpy = y;
    M.win[HOP + i] = y;
    sq += (double)y * y;
  }
  float rms = sqrtf((float)(sq / HOP));
  M.t++;
  mic.rms = rms;

  if (selfActive) { M.hold = 8; }
  if (M.hold > 0) {  // ignore the mic while he talks (and for ~130 ms after)
    M.hold--;
    speechReset();
    for (int i = 0; i < 7; i++) M.hist[i] = M.floorv;
    return;
  }

  M.floorv += (rms < M.floorv ? 0.15f : 0.0015f) * (rms - M.floorv);
  if (M.floorv < 25.0f) M.floorv = 25.0f;
  mic.floorv = M.floorv;

  // ---- loud noise: a sudden jump well above both the floor and the last ~100 ms
  float avg = 0;
  for (int i = 0; i < 6; i++) avg += M.hist[i];
  avg /= 6.0f;
  float ref = avg > M.floorv ? avg : M.floorv;
  float loudMin = MIC_LOUD_ABS > 10.0f * M.floorv ? MIC_LOUD_ABS : 10.0f * M.floorv;
  if (rms > loudMin && rms > MIC_LOUD_RATIO * ref && M.t > M.loudCool) {
    float a = (rms - MIC_LOUD_ABS) / 14000.0f;
    mic.loudAmt = a < 0.15f ? 0.15f : (a > 1 ? 1 : a);
    mic.loudSeq = mic.loudSeq + 1;
    M.loudCool = M.t + 62;  // ~1 s
    speechReset();
  }
  memmove(M.hist, M.hist + 1, 6 * sizeof(float));
  M.hist[6] = rms;

  // ---- speech: energy segments, voicing, syllable count
  float thr = MIC_VOICE_ABS > 2.6f * M.floorv ? MIC_VOICE_ABS : 2.6f * M.floorv;
  bool active = rms > thr && rms < 9000.0f;
  bool voiced = false;
  if (active) voiced = voicing(M.win) > 0.5f;

  if (!M.inSpeech) {
    M.actRun = active ? M.actRun + 1 : 0;
    if (M.actRun >= 3) {
      M.inSpeech = true; M.quiet = 0; M.tot = 3; M.voiced = voiced ? 3 : 0; M.peaks = 0;
      M.env = rms; M.localMax = rms; M.localMin = rms; M.falling = false; M.lastPeakT = 0;
    }
    return;
  }

  M.tot++;
  if (active) { M.quiet = 0; if (voiced) M.voiced++; } else M.quiet++;
  mic.voiceNow = (M.voiced >= 8);

  // syllable peaks on an envelope that favours voiced sound (fricatives like "ch" count little)
  float e = active ? (voiced ? rms : rms * 0.35f) : rms * 0.2f;
  M.env = 0.55f * M.env + 0.45f * e;
  if (!M.falling) {
    if (M.env > M.localMax) M.localMax = M.env;
    if (M.env < M.localMax * 0.78f && M.localMax > thr * 0.9f && M.tot - M.lastPeakT >= 4) {
      if (M.peaks < 4) M.peakVal[M.peaks] = M.localMax;
      M.peaks++; M.lastPeakT = M.tot;
      M.falling = true; M.localMin = M.env;
    }
  } else {
    if (M.env < M.localMin) M.localMin = M.env;
    if (M.env > M.localMin * 1.3f + 1.0f) { M.falling = false; M.localMax = M.env; }
  }

  if (M.quiet >= 11) {  // ~180 ms of silence: the phrase is over
    int act = M.tot - M.quiet;
    float durMs = act * 16.0f;
    float vf = act > 0 ? (float)M.voiced / act : 0;
    if (M.t > M.loudCool) {
      if (durMs >= 300 && durMs <= 1400 && vf >= 0.4f && M.peaks >= 2 && M.peaks <= 3) {
        mic.nameSeq = mic.nameSeq + 1;
      } else if (durMs >= 350 && vf >= 0.3f) {
        mic.voiceSeq = mic.voiceSeq + 1;
      }
    }
    speechReset();
  }
}

}  // namespace snd
