// Renders every sound to a WAV, reports levels, and tests the microphone detectors on synthetic signals.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include "../src/charlie_sound.h"
using namespace snd;

static void wav(const char* path, const std::vector<int16_t>& d) {
  FILE* f = fopen(path, "wb");
  uint32_t n = d.size() * 2, sr = SR, br = SR * 2, fs = 36 + n; uint16_t one = 1, two = 2, bits = 16, fmt = 1;
  uint32_t sixteen = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&fs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
  fwrite(&fmt, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
  fwrite(&two, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
  fwrite(d.data(), 2, d.size(), f); fclose(f);
}

static std::vector<int16_t> synth(Id id, int ms) {
  std::vector<int16_t> out;
  play(id);
  int total = ms * SR / 1000;
  int16_t buf[HOP];
  for (int i = 0; i < total; i += HOP) { render(buf, HOP); out.insert(out.end(), buf, buf + HOP); }
  return out;
}

static uint32_t rs = 777;
static float rnd() { rs = rs * 1664525u + 1013904223u; return ((rs >> 8) & 0xFFFF) / 32768.0f - 1.0f; }

// feed a signal to the mic analyzer, return event counts
struct Ev { int loud, voice, name; };
static Ev feed(const std::vector<float>& sig) {
  M = MicState(); mic.loudSeq = 0; mic.voiceSeq = 0; mic.nameSeq = 0;
  std::vector<int16_t> pad(sig.size() + 16000 * 2);
  for (size_t i = 0; i < pad.size(); i++) {
    float v = (i >= 8000 && i - 8000 < sig.size()) ? sig[i - 8000] : 0;
    v += 25.0f * rnd();  // mic noise floor
    pad[i] = (int16_t)(v > 32000 ? 32000 : (v < -32000 ? -32000 : v));
  }
  for (size_t i = 0; i + HOP <= pad.size(); i += HOP) micFeed(&pad[i], HOP, false);
  return {(int)mic.loudSeq, (int)mic.voiceSeq, (int)mic.nameSeq};
}

// harmonic "vowel": f0 gliding, amplitude envelope
static void vowel(std::vector<float>& s, float ms, float f0a, float f0b, float amp, float rise = 0.15f, float fall = 0.25f) {
  int n = (int)(ms * SR / 1000); static float ph = 0;
  for (int i = 0; i < n; i++) {
    float u = (float)i / n, f0 = f0a + (f0b - f0a) * u;
    ph += TWO_PI * f0 / SR; if (ph > TWO_PI) ph -= TWO_PI;
    float v = 0; for (int h = 1; h <= 8; h++) v += sinf(h * ph) / h;
    float env = fminf(1, u / rise) * fminf(1, (1 - u) / fall);
    s.push_back(v * amp * env);
  }
}
static void noiseBurst(std::vector<float>& s, float ms, float amp) {
  int n = (int)(ms * SR / 1000); float p = 0;
  for (int i = 0; i < n; i++) { float x = rnd(); s.push_back((x - p) * amp * 0.7f); p = x; }  // high-passed
}
static void silence(std::vector<float>& s, float ms) { s.insert(s.end(), (size_t)(ms * SR / 1000), 0.0f); }

int main() {
  // ---- 1. every sound: level + length
  const char* names[] = {"none","boot","happy","giggle","curious","sad","angry","surprise","scared","yes","no","dizzy","yawn","sigh","purr","love","snore","wake","think","sneeze","blip","sing","perk","listen","grumble","excited","tap","up","down","confirm","cancel","alarmset","ouch","tickle","proud","shy","scan","relief","bellL","bellR"};
  setVolume(2);
  std::vector<int16_t> all;
  printf("%-10s %6s %8s\n", "sound", "ms", "peak%");
  for (int id = 1; id < S_COUNT; id++) {
    auto d = synth((Id)id, 1400);
    int pk = 0, last = 0;
    for (size_t i = 0; i < d.size(); i++) { int a = abs(d[i]); if (a > pk) pk = a; if (a > 300) last = i; }
    printf("%-10s %6d %7.1f%%\n", names[id], last * 1000 / SR, pk * 100.0 / 32768);
    all.insert(all.end(), d.begin(), d.end());
  }
  wav("out/all_sounds.wav", all);

  // alarm burst: bells alternating at 8/s for 1.4 s at each volume
  for (int v = 0; v <= 3; v++) {
    setVolume(v);
    std::vector<int16_t> d; int16_t buf[HOP]; int pk = 0;
    for (int blk = 0; blk < 16000 * 2 / HOP; blk++) {
      if (blk % 7 == 0) play((blk / 7) & 1 ? S_BELL_R : S_BELL_L);
      render(buf, HOP); d.insert(d.end(), buf, buf + HOP);
    }
    for (auto x : d) if (abs(x) > pk) pk = abs(x);
    printf("alarm bells, volume %d: peak %.1f%%\n", v, pk * 100.0 / 32768);
    if (v == 2) wav("out/alarm.wav", d);
  }
  setVolume(2);

  // ---- 2. microphone detectors
  printf("\nmic tests (loud, voice, name events):\n");
  { std::vector<float> s; silence(s, 500); for (int i = 0; i < 2400; i++) s.push_back(rnd() * 26000 * expf(-i / 500.0f)); silence(s, 800);
    Ev e = feed(s); printf("  clap/bang         -> loud=%d voice=%d name=%d   (want loud=1)\n", e.loud, e.voice, e.name); }
  { std::vector<float> s; silence(s, 300); noiseBurst(s, 70, 1500); vowel(s, 230, 130, 118, 2600, 0.1f, 0.3f); silence(s, 60);
    vowel(s, 190, 140, 175, 2000, 0.2f, 0.4f); silence(s, 700);
    Ev e = feed(s); printf("  'Char-lee' (2 syl) -> loud=%d voice=%d name=%d   (want name=1)\n", e.loud, e.voice, e.name); }
  { std::vector<float> s; silence(s, 300); noiseBurst(s, 70, 1500); vowel(s, 200, 125, 118, 2400); silence(s, 70); vowel(s, 170, 135, 165, 2000);
    silence(s, 500); noiseBurst(s, 70, 1500); vowel(s, 230, 140, 128, 2600); silence(s, 60); vowel(s, 190, 150, 185, 2100); silence(s, 700);
    Ev e = feed(s); printf("  'Charlie Charlie'  -> loud=%d voice=%d name=%d   (want name=2)\n", e.loud, e.voice, e.name); }
  { std::vector<float> s; silence(s, 300); vowel(s, 1800, 120, 110, 2400, 0.1f, 0.1f); silence(s, 700);
    Ev e = feed(s); printf("  long steady 'aaah' -> loud=%d voice=%d name=%d   (want voice=1, name=0)\n", e.loud, e.voice, e.name); }
  { std::vector<float> s; silence(s, 300); vowel(s, 160, 130, 125, 2400); silence(s, 70); vowel(s, 150, 125, 150, 2200); silence(s, 70); vowel(s, 150, 150, 120, 2200); silence(s, 70);
    vowel(s, 150, 120, 150, 2200); silence(s, 70); vowel(s, 200, 150, 110, 2200); silence(s, 700);
    Ev e = feed(s); printf("  5-syllable phrase  -> loud=%d voice=%d name=%d   (want name=0)\n", e.loud, e.voice, e.name); }
  { std::vector<float> s; for (int i = 0; i < 16000 * 4; i++) s.push_back(rnd() * 400);
    Ev e = feed(s); printf("  quiet room noise   -> loud=%d voice=%d name=%d   (want all 0)\n", e.loud, e.voice, e.name); }
  { std::vector<float> s; for (int i = 0; i < 16000 * 4; i++) s.push_back(rnd() * 1800 * (0.7f + 0.3f * sinf(i * 0.0003f)));
    Ev e = feed(s); printf("  steady hiss/fan    -> loud=%d voice=%d name=%d   (want all 0)\n", e.loud, e.voice, e.name); }
  { std::vector<float> s; silence(s, 300); for (int k = 0; k < 4; k++) { vowel(s, 1000, 700 + 100 * k, 900, 1500, 0.05f, 0.05f); silence(s, 300); }
    Ev e = feed(s); printf("  music-ish tones    -> loud=%d voice=%d name=%d   (want name=0)\n", e.loud, e.voice, e.name); }
  // Charlie's own sounds must not trigger himself: feed his output (loudly) back as mic input with selfActive set
  { M = MicState(); mic.loudSeq = 0; mic.voiceSeq = 0; mic.nameSeq = 0; setVolume(3);
    int16_t buf[HOP], in[HOP]; int total = 0;
    for (int blk = 0; blk < 16000 * 6 / HOP; blk++) {
      if (blk % 40 == 0) play((Id)(2 + (blk / 40) % 10));
      render(buf, HOP);
      for (int i = 0; i < HOP; i++) in[i] = (int16_t)(buf[i] * 0.9f + 20 * rnd());
      micFeed(in, HOP, E.active); total++;
    }
    printf("  own chirps heard   -> loud=%d voice=%d name=%d   (want all 0)\n", (int)mic.loudSeq, (int)mic.voiceSeq, (int)mic.nameSeq); }
  return 0;
}
