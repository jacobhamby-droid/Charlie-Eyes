// PC-side simulator: runs the Charlie core and dumps frames as raw RGB565 for inspection.
// usage: sim <scenario> <outprefix>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../src/charlie_core.h"
using namespace ch;

static uint16_t FB[SCR_W * SCR_H];
static void dump(const char* path) {
  FILE* f = fopen(path, "wb");
  fwrite(FB, 2, SCR_W * SCR_H, f);
  fclose(f);
}

static uint32_t NOW = 0;
static Input in;
static void step(int ms) {
  for (int i = 0; i < ms; i += 8) {
    NOW += 8;
    update(NOW, 0.008f, in);
    int a, b; render(a, b);
  }
}
static char path[256];
static const char* PFX;
static void snap(const char* tag) { snprintf(path, sizeof(path), "%s_%s.rgb565", PFX, tag); dump(path); }

static void gravity(float deg) {  // simulate holding the device rotated by deg
  float r = deg * DEG;
  in.imu = true; in.ax = cosf(r) * 9.81f; in.ay = sinf(r) * 9.81f; in.az = 0.0f;
}

int main(int argc, char** argv) {
  std::string sc = argc > 1 ? argv[1] : "idle";
  PFX = argc > 2 ? argv[2] : "out";
  init(FB, 42);
  start(0);
  gravity(0);

  if (sc == "idle") {
    step(1200); snap("a"); step(1500); snap("b"); step(2000); snap("c");
  } else if (sc == "boot") {
    for (int i = 0; i < 12; i++) { step(120); char t[8]; snprintf(t, 8, "%02d", i); snap(t); }
  } else if (sc == "anims") {
    step(2000);
    for (int id = 1; id < AN_COUNT; id++) {
      forceAnim(id);
      // grab the animation at ~3 moments
      step(160); char t[64];
      snprintf(t, 64, "%02d_%s_1", id, animName(id)); snap(t);
      step(220); snprintf(t, 64, "%02d_%s_2", id, animName(id)); snap(t);
      step(260); snprintf(t, 64, "%02d_%s_3", id, animName(id)); snap(t);
      step(2600);
      in.touch = false;
    }
  } else if (sc == "menu") {
    step(2500);
    // swipe down from the top
    in.touch = true; in.tx = 233; in.ty = 40; step(16);
    for (int i = 0; i < 12; i++) { in.ty += 14; step(16); }
    in.touch = false; step(900); snap("open");
    // tap swatch #3 (pink); in upright coords: ring radius 168 at 135deg
    float ang = 2 * 45.0f * DEG;
    in.touch = true; in.tx = 233 + 168 * sinf(ang); in.ty = 233 - 168 * cosf(ang); step(80);
    in.touch = false; step(1500); snap("picked");
    // swipe up to close
    in.touch = true; in.tx = 233; in.ty = 420; step(16);
    for (int i = 0; i < 12; i++) { in.ty -= 14; step(16); }
    in.touch = false; step(1200); snap("closed");
  } else if (sc == "rotate") {
    const float angs[4] = {0, 90, 180, 270};
    for (int i = 0; i < 4; i++) {
      gravity(angs[i]); step(1500);
      char t[8]; snprintf(t, 8, "r%d", (int)angs[i]); snap(t);
    }
  } else if (sc == "pet") {
    step(2000);
    in.touch = true; in.tx = 150; in.ty = 200; step(16);
    for (int k = 0; k < 6; k++) {
      for (int i = 0; i < 10; i++) { in.tx += 12; step(16); }
      for (int i = 0; i < 10; i++) { in.tx -= 12; step(16); }
    }
    snap("petting");
    in.touch = false; step(2500); snap("after");
  } else if (sc == "poke") {
    step(2000);
    for (int i = 0; i < 6; i++) {
      in.touch = true; in.tx = 200; in.ty = 233; step(60); in.touch = false; step(130);
      char t[8]; snprintf(t, 8, "p%d", i); snap(t);
      step(260);
    }
  } else if (sc == "shake") {
    step(2000);
    for (int i = 0; i < 40; i++) {
      float s = (i & 1) ? 2.2f : -1.8f;
      in.ax = 9.81f * (1 + s); in.ay = 9.81f * 0.4f * s; in.az = 0;
      step(40);
      if (i == 20) snap("shaking");
    }
    gravity(0); step(300); snap("after");
    step(1500); snap("recovered");
  } else if (sc == "pages") {
    step(2500);
    in.rtcOK = true; in.hh = 6; in.mm = 58; in.ss = 0;
    // swipe right from left edge -> timer
    in.touch = true; in.tx = 12; in.ty = 233; step(16);
    for (int i = 0; i < 12; i++) { in.tx += 14; step(16); }
    in.touch = false; step(900); snap("timer");
    // tap START (0,97)
    in.touch = true; in.tx = 233; in.ty = 233 + 97; step(80); in.touch = false; step(1200); snap("timer_running");
    // swipe left from right edge -> alarm
    in.touch = true; in.tx = 454; in.ty = 233; step(16);
    for (int i = 0; i < 12; i++) { in.tx -= 14; step(16); }
    in.touch = false; step(900); snap("alarm");
    // clock button -> set time
    in.touch = true; in.tx = 233 - 120; in.ty = 233 + 121; step(80); in.touch = false; step(900); snap("settime");
  } else if (sc == "ring") {
    step(2500);
    testRing(1);
    for (int i = 0; i < 14; i++) { step(60); char t[8]; snprintf(t, 8, "r%02d", i); snap(t); }
    step(1200);
    in.touch = true; in.tx = 233; in.ty = 300; step(60); in.touch = false; step(150); snap("dismiss");
    step(1500); snap("after");
  } else if (sc == "mic") {
    step(2500);
    snd::mic.nameSeq++; step(120); snap("name1"); step(250); snap("name2"); step(900); snap("name3");
    step(3000);
    snd::mic.loudAmt = 0.9f; snd::mic.loudSeq++; step(100); snap("loud1"); step(200); snap("loud2"); step(1200); snap("loud3");
  } else if (sc == "bench") {
    step(1000);
    int cnt = 0, maxRows = 0; long totRows = 0;
    for (int i = 0; i < 600; i++) {
      NOW += 16; update(NOW, 0.016f, in);
      int a, b; render(a, b);
      totRows += (b - a + 1); if (b - a + 1 > maxRows) maxRows = b - a + 1; cnt++;
    }
    printf("avg rows/frame %ld, max %d\n", totRows / cnt, maxRows);
  }
  return 0;
}
