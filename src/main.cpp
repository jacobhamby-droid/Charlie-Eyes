/*
 * Charlie - Alive Desktop Eyes Companion
 * Board: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466 CO5300)
 *
 * Features:
 *  - Emo / Vector / Cozmo style rounded-rect eyes with shading/highlights
 *  - Rich mood system (joy, energy, annoyance) that drifts and reacts
 *  - Touch, petting (swipe), shake, pick-up reactions + sounds
 *  - Swipe UP from bottom to open menu (eye shape + color presets)
 *  - Autonomous idle animations so he feels alive
 *  - Settings saved in Preferences
 */

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <ESP_I2S.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrvCST92xx.h>

#define MASTER_VOLUME 0.55f
#define PA_ACTIVE_LEVEL HIGH

// Pins
#define LCD_CS 12
#define LCD_SCLK 38
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_RST 39
#define PIN_SDA 15
#define PIN_SCL 14
#define TP_RST 40
#define TP_INT 11
#define I2S_MCLK_PIN 42
#define I2S_BCLK_PIN 9
#define I2S_WS_PIN 45
#define I2S_DOUT_PIN 8
#define I2S_DIN_PIN 10
#define PA_CTRL_PIN 46
#define IMU_ADDR 0x6B

#define SCREEN_W 466
#define SCREEN_H 466
#define CENTER_X (SCREEN_W / 2)
#define CENTER_Y (SCREEN_H / 2)

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(bus, LCD_RST, 0, SCREEN_W, SCREEN_H, 6, 0, 0, 0);
Arduino_Canvas *gfx = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel);

uint16_t BG_COLOR = 0x0000;
uint16_t EYE_COLOR = 0x07FF; // cyan default
uint16_t HIGHLIGHT = 0xFFFF;
uint16_t GREY = 0x4208;
uint16_t RED = 0xF800;

Preferences prefs;

// Eye presets (shape)
struct Preset { const char *name; int w, h, r, spacing; };
const Preset SHAPE_PRESETS[4] = {
  {"Charlie", 110, 140, 36, 180},
  {"Emo",     130, 100, 48, 190},
  {"Vector",  115, 125, 28, 180},
  {"Cozmo",   100, 145, 22, 170}
};
uint8_t shapeIdx = 0;
float gW=110, gH=140, gR=36, gSp=180;

// Color presets
struct ColorPreset { const char *name; uint16_t col; };
const ColorPreset COLOR_PRESETS[6] = {
  {"Cyan", 0x07FF}, {"Green", 0x07E0}, {"Blue", 0x001F},
  {"Pink", 0xF81F}, {"Yellow", 0xFFE0}, {"White", 0xFFFF}
};
uint8_t colorIdx = 0;

// Sound engine (simplified but functional)
enum Snd : uint8_t { SND_NONE, SND_BOOT, SND_TAP, SND_HAPPY, SND_PURR, SND_SURPRISE, SND_ANGRY, SND_SAD, SND_YAWN, SND_TICK, SND_COUNT };
struct Note { int16_t f0, f1; uint16_t ms; uint8_t vol; };
static const Note N_BOOT[] = {{600,1000,80,70},{1000,1400,100,70}};
static const Note N_TAP[] = {{800,1200,60,70}};
static const Note N_HAPPY[] = {{900,1300,80,70},{1300,1600,100,70}};
static const Note N_PURR[] = {{280,320,150,60},{320,280,150,60}};
static const Note N_SURPRISE[] = {{500,1600,200,75}};
static const Note N_ANGRY[] = {{180,140,200,80}};
static const Note N_SAD[] = {{700,400,300,50}};
static const Note N_YAWN[] = {{600,300,400,40}};
static const Note N_TICK[] = {{1400,1400,30,50}};

struct SoundDef { const Note *notes; uint8_t n; };
#define SD(a) {a, (uint8_t)(sizeof(a)/sizeof(a[0]))}
static const SoundDef SOUNDS[SND_COUNT] = {
  {nullptr,0}, SD(N_BOOT), SD(N_TAP), SD(N_HAPPY), SD(N_PURR), SD(N_SURPRISE), SD(N_ANGRY), SD(N_SAD), SD(N_YAWN), SD(N_TICK)
};

I2SClass i2s;
QueueHandle_t soundQ = nullptr;
volatile bool muted = false;
bool audioOK = false;
const int SAMPLE_RATE = 16000;

static void playNotes(const SoundDef &sd) {
  static int16_t buf[256*2];
  float phase = 0;
  const float TWO_PI = 6.2831853f;
  for (uint8_t ni=0; ni<sd.n; ni++) {
    const Note &nt = sd.notes[ni];
    int total = (SAMPLE_RATE * nt.ms)/1000;
    int done=0;
    while (done < total) {
      int chunk = min(256, total-done);
      for (int k=0; k<chunk; k++) {
        int idx = done+k;
        int16_t v=0;
        if (nt.f0>0) {
          float t = (float)idx/total;
          float f = nt.f0 + (nt.f1-nt.f0)*t;
          phase += TWO_PI * f / SAMPLE_RATE;
          if (phase > TWO_PI) phase -= TWO_PI;
          float env = min(1.0f, idx/(0.01f*SAMPLE_RATE)) * min(1.0f, (total-idx)/(0.03f*SAMPLE_RATE));
          v = (int16_t)(sinf(phase)*0.7f*env*(nt.vol/100.0f)*MASTER_VOLUME*32767);
        }
        buf[2*k]=v; buf[2*k+1]=v;
      }
      i2s.write((uint8_t*)buf, chunk*4);
      done += chunk;
    }
  }
  memset(buf,0,sizeof(buf));
  for(int i=0;i<8;i++) i2s.write((uint8_t*)buf,256*4);
}

static void audioTask(void*) {
  uint8_t id;
  for(;;) if (xQueueReceive(soundQ,&id,portMAX_DELAY)==pdTRUE)
    if (!muted && id>0 && id<SND_COUNT) playNotes(SOUNDS[id]);
}

void sound(uint8_t id) {
  if (!audioOK || muted || !soundQ) return;
  xQueueSend(soundQ,&id,0);
}

// Simplified codec init (same sequence as before)
static uint8_t esAddr=0x18;
static bool esWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(esAddr); Wire.write(reg); Wire.write(val); return Wire.endTransmission()==0;
}
static bool initCodec() {
  Wire.beginTransmission(esAddr);
  if (Wire.endTransmission()!=0) { esAddr=0x19; }
  esWrite(0x00,0x80); esWrite(0x01,0x3F); esWrite(0x32,0xBF);
  return true;
}
static void initAudio() {
  pinMode(PA_CTRL_PIN,OUTPUT); digitalWrite(PA_CTRL_PIN,PA_ACTIVE_LEVEL);
  if (!initCodec()) return;
  i2s.setPins(I2S_BCLK_PIN,I2S_WS_PIN,I2S_DOUT_PIN,I2S_DIN_PIN,I2S_MCLK_PIN);
  if (!i2s.begin(I2S_MODE_STD,SAMPLE_RATE,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO)) return;
  soundQ = xQueueCreate(4,sizeof(uint8_t));
  xTaskCreatePinnedToCore(audioTask,"audio",4096,nullptr,2,nullptr,0);
  audioOK=true;
}

// IMU
bool imuOK=false; float ax=0,ay=0,az=1; float restX=0,restY=0,restZ=1;
static bool imuWrite(uint8_t r,uint8_t v){Wire.beginTransmission(IMU_ADDR);Wire.write(r);Wire.write(v);return Wire.endTransmission()==0;}
static void initIMU(){
  Wire.beginTransmission(IMU_ADDR); Wire.write(0); Wire.endTransmission(false);
  Wire.requestFrom((int)IMU_ADDR,1); uint8_t id=Wire.available()?Wire.read():0;
  imuWrite(0x02,0x40); imuWrite(0x03,0x16); imuWrite(0x08,0x01); delay(30);
  imuOK=(id==0x05);
}
static bool readIMU(){
  Wire.beginTransmission(IMU_ADDR); Wire.write(0x35);
  if(Wire.endTransmission(false)||Wire.requestFrom((int)IMU_ADDR,6)!=6) return false;
  int16_t rx=Wire.read()|(Wire.read()<<8); int16_t ry=Wire.read()|(Wire.read()<<8); int16_t rz=Wire.read()|(Wire.read()<<8);
  ax=rx/8192.0f; ay=ry/8192.0f; az=rz/8192.0f; return true;
}

// Touch
TouchDrvCST92xx touch;
bool touchOK=false; volatile bool touchFlag=false;
void IRAM_ATTR onTouchInt(){touchFlag=true;}
struct TouchState{bool down=false; int16_t x=0,y=0,sx=0,sy=0; uint32_t t0=0,lastSeen=0; float path=0; bool swipeHandled=false;} ts;

// Mood
enum Mood:uint8_t{M_NEUTRAL,M_HAPPY,M_SLEEPY,M_ANGRY,M_SAD,M_SURPRISED,M_LOVE,M_DIZZY,M_ASLEEP};
struct Expr{float tired,angry,happy,scale,open;};
Expr exprFor(Mood m){
  switch(m){
    case M_HAPPY: return {0,0,0.6f,1,1};
    case M_LOVE: return {0,0,0.9f,1,1};
    case M_SLEEPY: return {0.5f,0,0,0.95f,0.8f};
    case M_ANGRY: return {0,0.7f,0,0.95f,1};
    case M_SAD: return {0.6f,0,0,0.95f,1};
    case M_SURPRISED: return {0,0,0,1.15f,1};
    case M_DIZZY: return {0.2f,0,0,1,1};
    case M_ASLEEP: return {0,0,0,0.95f,0.05f};
    default: return {0,0,0,1,1};
  }
}
Expr cur=exprFor(M_NEUTRAL);
float joy=60,energy=85,annoy=0;
uint32_t lastInteract=0; bool asleep=false;
Mood baseMood=M_NEUTRAL, reactMood=M_NEUTRAL;
uint32_t reactEnd=0, reactStart=0; bool reacting=false;
bool menuOpen=false; float menuP=0;
bool held=false; uint32_t liftMs=0,stillMs=0,lastShake=0;
uint32_t peakTimes[8]; uint8_t peakCount=0; float prevDyn=0;
uint32_t lastTap=0; uint8_t tapBurst=0; uint32_t lastPurr=0; bool petting=false;
float blinkF=1,blinkTarget=1; uint8_t blinkPhase=0; uint32_t nextBlink=2000,blinkT=0;
float lookX=0,lookY=0,tLookX=0,tLookY=0; uint32_t nextLook=0;

void applyBrightness(){ panel->setBrightness(asleep?40:200); }
void react(Mood m,uint32_t ms){reactMood=m; reactStart=millis(); reactEnd=reactStart+ms; reacting=true;}
void noteInteraction(){lastInteract=millis();}
void wakeUp(bool startled){if(!asleep)return; asleep=false; energy=max(energy,40.f); applyBrightness(); sound(SND_YAWN); if(startled)react(M_SURPRISED,600);}
void goToSleep(){asleep=true; applyBrightness(); sound(SND_YAWN);}

void onTap(){
  uint32_t now=millis(); noteInteraction();
  if(asleep){wakeUp(true);return;}
  if(now-lastTap<600) tapBurst++; else tapBurst=1; lastTap=now;
  joy=clampf(joy+6,0,100);
  if(tapBurst>=5){annoy=clampf(annoy+20,0,100); react(M_ANGRY,1200); sound(SND_ANGRY); tapBurst=0;}
  else{annoy=clampf(annoy-2,0,100); react(M_HAPPY,700); sound(random(2)?SND_TAP:SND_HAPPY); blinkPhase=0; nextBlink=now;}
}
void onPickup(){noteInteraction(); wakeUp(false); energy=clampf(energy+5,0,100); react(M_SURPRISED,1000); sound(SND_SURPRISE);}
void onPutDown(){noteInteraction(); joy=clampf(joy+5,0,100); react(M_HAPPY,1200); sound(SND_HAPPY);}
void onShake(bool strong){
  noteInteraction(); wakeUp(false);
  if(strong){annoy=clampf(annoy+30,0,100); joy=clampf(joy-5,0,100); react(M_ANGRY,2000); sound(SND_ANGRY);}
  else{annoy=clampf(annoy+8,0,100); react(M_DIZZY,1500); sound(SND_SURPRISE);}
}

void updateMood(float dt,uint32_t now){
  float idle=(now-lastInteract)/1000.f;
  joy+=(55-joy)*(1-expf(-dt/100.f));
  if(idle>100) joy-=0.12f*dt;
  if(asleep) energy+=1.2f*dt; else energy-=0.07f*dt;
  annoy-=3.5f*dt;
  joy=clampf(joy,0,100); energy=clampf(energy,0,100); annoy=clampf(annoy,0,100);
  if(!asleep&&!menuOpen&&energy<18&&idle>25) goToSleep();
  if(asleep&&energy>88){asleep=false; applyBrightness(); sound(SND_YAWN);}
  Mood old=baseMood;
  if(asleep) baseMood=M_ASLEEP;
  else if(annoy>50) baseMood=M_ANGRY;
  else if(energy<30) baseMood=M_SLEEPY;
  else if(joy<30) baseMood=M_SAD;
  else if(joy>70) baseMood=M_HAPPY;
  else baseMood=M_NEUTRAL;
  if(baseMood!=old && !reacting){
    if(baseMood==M_SLEEPY) sound(SND_YAWN);
    else if(baseMood==M_SAD) sound(SND_SAD);
    else if(baseMood==M_HAPPY) sound(SND_HAPPY);
  }
}

// Menu (swipe up from bottom)
void openMenu(){if(!menuOpen){menuOpen=true; sound(SND_TICK);}}
void closeMenu(){if(menuOpen){menuOpen=false; sound(SND_TICK);}}
void selectShape(uint8_t i){shapeIdx=i; prefs.putUChar("shape",i); sound(SND_HAPPY); react(M_HAPPY,500);}
void selectColor(uint8_t i){colorIdx=i; EYE_COLOR=COLOR_PRESETS[i].col; prefs.putUChar("color",i); sound(SND_HAPPY);}

void menuTap(int x,int y){
  // simple grid of shapes and colors
  for(int i=0;i<4;i++){
    int bx=60+(i%2)*180, by=80+(i/2)*100;
    if(x>=bx&&x<=bx+140&&y>=by&&y<=by+70){selectShape(i);return;}
  }
  for(int i=0;i<6;i++){
    int bx=40+i*70, by=300;
    if(x>=bx&&x<=bx+55&&y>=by&&y<=by+40){selectColor(i);return;}
  }
  if(y>400) closeMenu();
}

void drawMenu(float p){
  int top=(int)((1-p)*SCREEN_H);
  if(top>=SCREEN_H) return;
  gfx->fillRect(0,top,SCREEN_W,SCREEN_H-top,BG_COLOR);
  gfx->setTextColor(EYE_COLOR); gfx->setTextSize(2);
  gfx->setCursor(CENTER_X-60, top+30); gfx->print("Presets");
  for(int i=0;i<4;i++){
    int bx=60+(i%2)*180, by=top+70+(i/2)*100;
    bool sel=(i==shapeIdx);
    gfx->fillRoundRect(bx,by,140,70,12,sel?EYE_COLOR:0x1082);
    gfx->drawRoundRect(bx,by,140,70,12,EYE_COLOR);
    gfx->setTextColor(sel?BG_COLOR:EYE_COLOR);
    gfx->setCursor(bx+20,by+25); gfx->print(SHAPE_PRESETS[i].name);
  }
  gfx->setTextColor(EYE_COLOR); gfx->setCursor(CENTER_X-40, top+270); gfx->print("Colors");
  for(int i=0;i<6;i++){
    int bx=40+i*70, by=top+300;
    gfx->fillRoundRect(bx,by,55,40,8,COLOR_PRESETS[i].col);
    if(i==colorIdx) gfx->drawRoundRect(bx-2,by-2,59,44,10,0xFFFF);
  }
  gfx->setTextSize(1); gfx->setCursor(CENTER_X-50, top+360); gfx->print("swipe down to close");
}

// Touch handling
void touchDown(int x,int y,uint32_t now){
  ts.down=true; ts.x=ts.sx=x; ts.y=ts.sy=y; ts.t0=now; ts.path=0; ts.swipeHandled=false;
  noteInteraction(); if(asleep&&!menuOpen){wakeUp(true); ts.swipeHandled=true;}
}
void touchMove(int x,int y,uint32_t now){
  int dx=x-ts.x, dy=y-ts.y; ts.path+=sqrtf(dx*dx+dy*dy); ts.x=x; ts.y=y;
  // swipe up from bottom to open menu
  if(!menuOpen && !ts.swipeHandled && ts.sy>SCREEN_H-120 && (ts.sy-y)>80){openMenu(); ts.swipeHandled=true;}
  else if(menuOpen && !ts.swipeHandled && (y-ts.sy)>80){closeMenu(); ts.swipeHandled=true;}
  // petting detection (back and forth)
  static int dir=0, rev=0;
  int nd=dx>5?1:(dx<-5?-1:0);
  if(nd && nd!=dir){rev++; dir=nd;}
  if(rev>=3 && ts.path>80) petting=true;
}
void touchUp(uint32_t now){
  ts.down=false;
  if(ts.swipeHandled) return;
  bool tap=(now-ts.t0)<400 && ts.path<30;
  if(menuOpen){if(tap) menuTap(ts.x,ts.y);}
  else if(tap && !petting) onTap();
  petting=false;
}
void pollTouch(uint32_t now){
  if(!touchOK) return;
  static uint32_t last=0;
  if(!(touchFlag||(ts.down&&now-last>=16))){if(ts.down&&now-ts.lastSeen>80) touchUp(now); return;}
  touchFlag=false; last=now;
  int16_t px[2],py[2];
  uint8_t n=touch.getPoint(px,py,2);
  if(n>0){
    ts.lastSeen=now;
    int x=constrain(px[0],0,SCREEN_W-1), y=constrain(py[0],0,SCREEN_H-1);
    if(!ts.down) touchDown(x,y,now); else touchMove(x,y,now);
  } else if(ts.down && now-ts.lastSeen>80) touchUp(now);
}

void pollMotion(uint32_t now){
  static uint32_t last=0; if(!imuOK||now-last<25) return;
  uint32_t dt=now-last; last=now;
  if(!readIMU()) return;
  float mag=sqrtf(ax*ax+ay*ay+az*az); float dyn=fabsf(mag-1.f);
  if(dyn>0.65f && prevDyn<=0.65f && peakCount<8) peakTimes[peakCount++]=now;
  prevDyn=dyn;
  uint8_t k=0; for(uint8_t i=0;i<peakCount;i++) if(now-peakTimes[i]<800) peakTimes[k++]=peakTimes[i];
  peakCount=k;
  if(peakCount>=3 && now-lastShake>1200){lastShake=now; peakCount=0; onShake(peakCount>=5); held=true; stillMs=0; return;}
  float dot=ax*restX+ay*restY+az*restZ;
  float cosA=clampf(dot/(max(mag,0.01f)*max(sqrtf(restX*restX+restY*restY+restZ*restZ),0.01f)),-1,1);
  float tilt=acosf(cosA)*57.3f;
  if(dyn<0.06f) stillMs+=dt; else stillMs=0;
  if(!held && stillMs>700){restX=lerpf(restX,ax,0.15f); restY=lerpf(restY,ay,0.15f); restZ=lerpf(restZ,az,0.15f);}
  if(!held){
    if((dyn>0.08f&&dyn<0.6f)||tilt>18) liftMs+=dt; else liftMs=0;
    if(liftMs>200){held=true; liftMs=0; onPickup();}
  } else if((stillMs>1000&&tilt<10)||stillMs>2000){held=false; restX=ax; restY=ay; restZ=az; onPutDown();}
}

void updateAnim(uint32_t now, float &offX, float &offY){
  if(reacting && now>=reactEnd) reacting=false;
  Mood shown=reacting?reactMood:baseMood;
  Expr t=exprFor(shown);
  float e=0.2f;
  cur.tired=lerpf(cur.tired,t.tired,e); cur.angry=lerpf(cur.angry,t.angry,e);
  cur.happy=lerpf(cur.happy,t.happy,e); cur.scale=lerpf(cur.scale,t.scale,e); cur.open=lerpf(cur.open,t.open,e);
  const Preset &p=SHAPE_PRESETS[shapeIdx];
  gW=lerpf(gW,p.w,0.15f); gH=lerpf(gH,p.h,0.15f); gR=lerpf(gR,p.r,0.15f); gSp=lerpf(gSp,p.spacing,0.15f);
  if(!asleep){
    if(blinkPhase==0 && now>=nextBlink){blinkTarget=0; blinkPhase=1; blinkT=now+80;}
    else if(blinkPhase==1 && now>=blinkT){blinkTarget=1; blinkPhase=2; blinkT=now+100;}
    else if(blinkPhase==2 && now>=blinkT){blinkPhase=0; nextBlink=now+random(2000,5500); if(random(6)==0) nextBlink=now+200;}
  } else blinkTarget=1;
  blinkF=lerpf(blinkF,blinkTarget,0.55f);
  if(petting){tLookX=clampf((ts.x-CENTER_X)*0.1f,-20,20); tLookY=clampf((ts.y-CENTER_Y)*0.08f,-15,15);}
  else if(held && imuOK){tLookX=clampf(ax*25,-20,20); tLookY=clampf(ay*20,-15,15);}
  else if(shown==M_NEUTRAL||shown==M_HAPPY||shown==M_SLEEPY||shown==M_SAD){
    if(now>=nextLook){tLookX=random(-22,23); tLookY=random(-12,13); if(random(4)==0){tLookX=tLookY=0;} nextLook=now+random(800,3000);}
    if(shown==M_SAD) tLookY=max(tLookY,6.f);
  } else {tLookX=tLookY=0;}
  lookX=lerpf(lookX,tLookX,0.2f); lookY=lerpf(lookY,tLookY,0.2f);
  float wobX=0,wobY=0;
  float rt=reacting?(now-reactStart):0;
  if(reacting && reactMood==M_DIZZY){wobX=16*cosf(rt*0.015f); wobY=12*sinf(rt*0.015f);}
  else if(reacting && reactMood==M_ANGRY) wobX=2.5f*sinf(rt*0.1f);
  else if(reacting && (reactMood==M_HAPPY||reactMood==M_LOVE) && rt<600) wobY=-5*fabsf(sinf(rt*0.012f));
  if(asleep) wobY+=1.5f*sinf(now/800.f);
  offX=lookX+wobX; offY=lookY+wobY;
}

void drawEyes(float offX, float offY){
  float sc=cur.scale;
  int w=(int)(gW*sc), fullH=(int)(gH*sc);
  int h=(int)(fullH*cur.open*blinkF); if(h<5) h=5;
  int r=min((int)(gR*sc), min(h/2,w/2));
  for(int s=0;s<2;s++){
    bool left=(s==0);
    int cx=CENTER_X+(left?-1:1)*(int)(gSp/2)+(int)offX;
    int cy=CENTER_Y+(int)offY;
    int x0=cx-w/2, y0=cy-h/2;
    // main eye
    gfx->fillRoundRect(x0,y0,w,h,r,EYE_COLOR);
    // shading / highlight (top-left lighter)
    int hlW=w/3, hlH=h/4;
    gfx->fillRoundRect(x0+8,y0+6,hlW,hlH,r/3,HIGHLIGHT);
    // lids
    int top=y0-1;
    if(cur.tired>0.03f){
      int d=(int)(h*0.5f*cur.tired);
      if(left) gfx->fillTriangle(x0,top,x0+w,top,x0,top+d,BG_COLOR);
      else gfx->fillTriangle(x0,top,x0+w,top,x0+w,top+d,BG_COLOR);
    }
    if(cur.angry>0.03f){
      int d=(int)(h*0.5f*cur.angry);
      if(left) gfx->fillTriangle(x0,top,x0+w,top,x0+w,top+d,BG_COLOR);
      else gfx->fillTriangle(x0,top,x0+w,top,x0,top+d,BG_COLOR);
    }
    if(cur.happy>0.03f){
      int off=(int)(fullH*0.45f*cur.happy);
      gfx->fillRoundRect(x0-1,y0+h-off,w+2,fullH,r,BG_COLOR);
    }
  }
}

void renderFrame(uint32_t now){
  float offX,offY; updateAnim(now,offX,offY);
  menuP=lerpf(menuP, menuOpen?1.f:0.f, 0.3f);
  if(fabsf(menuP-(menuOpen?1.f:0.f))<0.01f) menuP=menuOpen?1.f:0.f;
  gfx->fillScreen(BG_COLOR);
  if(menuP<0.99f) drawEyes(offX,offY);
  if(menuP>0.01f) drawMenu(menuP);
  gfx->flush();
}

void updatePetting(float dt,uint32_t now){
  if(petting){
    noteInteraction();
    joy=clampf(joy+5*dt,0,100); annoy=clampf(annoy-6*dt,0,100);
    if(!reacting||reactMood!=M_LOVE) react(M_LOVE,600);
    else reactEnd=now+600;
    if(now-lastPurr>700){sound(SND_PURR); lastPurr=now;}
  }
}

void handleSerial(){
  while(Serial.available()){
    char c=Serial.read();
    switch(c){
      case 'n': react(M_NEUTRAL,2000); break;
      case 'h': react(M_HAPPY,2000); break;
      case 's': react(M_SLEEPY,2000); break;
      case 'a': react(M_ANGRY,2000); break;
      case 'd': react(M_SAD,2000); break;
      case 'u': react(M_SURPRISED,2000); break;
      case 'l': react(M_LOVE,2000); break;
      case 'z': asleep?wakeUp(true):goToSleep(); break;
      case 'm': menuOpen?closeMenu():openMenu(); break;
    }
  }
}

void setup(){
  Serial.begin(115200); delay(200);
  Serial.println("Charlie alive booting...");
  prefs.begin("charlie",false);
  shapeIdx=constrain(prefs.getUChar("shape",0),0,3);
  colorIdx=constrain(prefs.getUChar("color",0),0,5);
  EYE_COLOR=COLOR_PRESETS[colorIdx].col;
  muted=prefs.getBool("mute",false);
  gW=SHAPE_PRESETS[shapeIdx].w; gH=SHAPE_PRESETS[shapeIdx].h;
  gR=SHAPE_PRESETS[shapeIdx].r; gSp=SHAPE_PRESETS[shapeIdx].spacing;
  if(!gfx->begin()){Serial.println("Display fail"); while(1)delay(1000);}
  gfx->fillScreen(BG_COLOR); gfx->flush(); applyBrightness();
  Wire.begin(PIN_SDA,PIN_SCL); Wire.setClock(400000);
  touch.setPins(TP_RST,TP_INT);
  touchOK=touch.begin(Wire,0x5A,PIN_SDA,PIN_SCL);
  if(touchOK){
    touch.setMaxCoordinates(SCREEN_W,SCREEN_H);
    pinMode(TP_INT,INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TP_INT),onTouchInt,FALLING);
  }
  initIMU(); initAudio();
  randomSeed(esp_random());
  uint32_t now=millis(); lastInteract=now; nextBlink=now+1200; nextLook=now+600;
  sound(SND_BOOT);
}

void loop(){
  static uint32_t lastFrame=0, lastMood=0;
  uint32_t now=millis();
  handleSerial();
  pollTouch(now);
  pollMotion(now);
  if(now-lastMood>=100){
    float dt=(now-lastMood)/1000.f; lastMood=now;
    updatePetting(dt,now); updateMood(dt,now);
  }
  if(now-lastFrame>=33){lastFrame=now; renderFrame(now);}
}
