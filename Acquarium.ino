/*********************************************************************
 * AQUÁRIO ASCII MINI v3 - ST7735 160x80
 * Versão calma e colorida
 *********************************************************************/

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);

// ====================== CONFIGURAÇÃO ======================A
#define MAX_FISH       6
#define MAX_SEAHORSES  1
#define MAX_BUBBLES    12

// ====================== ESTRUTURAS ======================
struct Fish {
  float x, y;
  float vx, vy;
  int type;
  bool active;
};

struct Seahorse {
  float x, y;
  float vx;
  bool facingRight;
  bool active;
};

struct Bubble {
  float x, y;
  float vy;
  bool active;
};

// ====================== GLIFOS ======================
const char* fishGlyphs[] = { "><>", ">')>", "o><", "><" };
const int FISH_TYPES = 4;

Fish fishPool[MAX_FISH];
Seahorse seahorses[MAX_SEAHORSES];
Bubble bubbles[MAX_BUBBLES];

unsigned long lastUpdate = 0;

// ====================== SETUP ======================
void setup() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  canvas.createSprite(160, 80);
  canvas.setTextFont(1);
  canvas.setTextSize(1);
  
  randomSeed(analogRead(26));
  initAquarium();
}

void initAquarium() {
  for (int i = 0; i < MAX_FISH; i++) spawnFish(i);
  for (int i = 0; i < MAX_SEAHORSES; i++) spawnSeahorse(i);
  for (int i = 0; i < MAX_BUBBLES; i++) spawnBubble(i, true);
}

// ====================== SPAWN ======================
void spawnFish(int i) {
  Fish& f = fishPool[i];
  f.x = random(0, 160);
  f.y = random(15, 60);
  f.vx = random(0, 2) ? random(5, 11)/10.0 : -random(5, 11)/10.0;
  f.vy = random(-5, 6)/10.0;
  f.type = random(0, FISH_TYPES);
  f.active = true;
}

void spawnSeahorse(int i) {
  Seahorse& s = seahorses[i];
  s.x = random(20, 140);
  s.y = random(28, 54);
  s.vx = random(0, 2) ? random(7, 12)/10.0 : -random(7, 12)/10.0;
  s.facingRight = (s.vx > 0);
  s.active = true;
}

void spawnBubble(int i, bool spread) {
  Bubble& b = bubbles[i];
  b.x = random(8, 152);
  b.y = spread ? random(10, 72) : random(58, 78);
  b.vy = random(7, 14) / 10.0;
  b.active = true;
}

// ====================== DESENHO ======================
void drawFish(Fish& f) {
  if (!f.active) return;
  canvas.setTextColor(TFT_CYAN);
  canvas.drawString(fishGlyphs[f.type], (int)f.x, (int)f.y);
}

void drawSeahorse(Seahorse& s) {
  if (!s.active) return;
  canvas.setTextColor(TFT_MAGENTA);
  if (s.facingRight) {
    canvas.drawString("  ^^ ", (int)s.x, (int)s.y);
    canvas.drawString(" /o) ", (int)s.x, (int)s.y + 6);
    canvas.drawString("[__-/ ", (int)s.x, (int)s.y + 12);  
    canvas.drawString("  /|  ", (int)s.x, (int)s.y + 18);
    canvas.drawString(" / |  ",(int)s.x, (int)s.y + 24);
    canvas.drawString(" \\ |  ",(int)s.x, (int)s.y + 30);
    //canvas.drawString("  ( ) ",(int)s.x, (int)s.y + 36);
    canvas.drawString("  \\_/ ",(int)s.x, (int)s.y + 36);

    //canvas.drawString("/o)", (int)s.x, (int)s.y + 6);
  } else {
    canvas.drawString("  ^^ ", (int)s.x, (int)s.y);
    canvas.drawString(" /o) ", (int)s.x, (int)s.y + 6);
    canvas.drawString("[__-/ ", (int)s.x, (int)s.y + 12);  
    canvas.drawString("  /|  ", (int)s.x, (int)s.y + 18);
    canvas.drawString(" / |  ",(int)s.x, (int)s.y + 24);
    canvas.drawString(" \\ |  ",(int)s.x, (int)s.y + 30);
    //canvas.drawString("  ( ) ",(int)s.x, (int)s.y + 36);
    canvas.drawString("  \\_/ ",(int)s.x, (int)s.y + 36);
  }
}

void drawBubbles() {
  canvas.setTextColor(TFT_WHITE);
  for (int i = 0; i < MAX_BUBBLES; i++) {
    if (bubbles[i].active) {
      canvas.drawPixel((int)bubbles[i].x, (int)bubbles[i].y, TFT_WHITE);
    }
  }
}

// ====================== ALGAS COM CORES DIFERENTES ======================
void drawSeaweed() {
  float t = millis() / 1100.0;   // Movimento mais lento
  
  // Alga 1 - Verde escuro
  float sway1 = sin(t + 0.3) * 2.2;
  canvas.setTextColor(0x03E0);           // Verde escuro
  canvas.drawString("|", 18 + (int)sway1, 52);
  canvas.drawString("|", 17 + (int)sway1*1.4, 59);
  canvas.drawString(")", 20 + (int)sway1*0.8, 67);

  // Alga 2 - Verde claro
  float sway2 = sin(t * 1.1 + 1.8) * 1.8;
  canvas.setTextColor(TFT_GREEN);
  canvas.drawString("|", 52 + (int)sway2, 50);
  canvas.drawString("|", 50 + (int)sway2*1.3, 58);
  canvas.drawString(")", 55 + (int)sway2*0.7, 66);

  // Alga 3 - Verde azulado
  float sway3 = sin(t * 0.9 + 3.2) * 2.0;
  canvas.setTextColor(0x05EB);           // Verde turquesa
  canvas.drawString("|", 98 + (int)sway3, 54);
  canvas.drawString("|", 96 + (int)sway3*1.35, 61);
  canvas.drawString(")", 100 + (int)sway3*0.6, 68);

  // Alga 4 - Verde médio
  float sway4 = sin(t * 1.05 + 0.7) * 1.6;
  canvas.setTextColor(TFT_DARKGREEN);
  canvas.drawString("|", 135 + (int)sway4, 51);
  canvas.drawString("|", 133 + (int)sway4*1.25, 59);
  canvas.drawString(")", 137 + (int)sway4*0.9, 67);
}

// ====================== UPDATE ======================
void updateFish(Fish& f) {
  f.x += f.vx;
  f.y += f.vy;
  
  if (f.x < -18) f.x = 172;
  if (f.x > 172) f.x = -18;
  
  if (f.y < 12) f.vy = 0.35;
  if (f.y > 64) f.vy = -0.35;
  
  f.vy += sin(millis()/1400.0) * 0.007;   // Movimento mais suave
}

void updateSeahorse(Seahorse& s) {
  s.x += s.vx;
  s.y += sin(millis()/850.0) * 0.55;
  
  if (s.x < -28) s.x = 178;
  if (s.x > 178) s.x = -28;
}

void updateBubbles() {
  for (int i = 0; i < MAX_BUBBLES; i++) {
    if (!bubbles[i].active) continue;
    bubbles[i].y -= bubbles[i].vy;
    bubbles[i].x += sin(millis()/520.0 + i) * 0.35;
    if (bubbles[i].y < -6) spawnBubble(i, false);
  }
}

// ====================== LOOP ======================
void loop() {
  if (millis() - lastUpdate < 65) return;   // Ainda mais calmo (~15 FPS)
  lastUpdate = millis();

  canvas.fillSprite(TFT_BLACK);
  
  // Faixa azul menor
  canvas.fillRect(0, 58, 160, 28, 0x000F);
  
  drawSeaweed();
  drawBubbles();

  for (int i = 0; i < MAX_FISH; i++) {
    if (fishPool[i].active) {
      updateFish(fishPool[i]);
      drawFish(fishPool[i]);
    }
  }

  for (int i = 0; i < MAX_SEAHORSES; i++) {
    if (seahorses[i].active) {
      updateSeahorse(seahorses[i]);
      drawSeahorse(seahorses[i]);
    }
  }

  canvas.pushSprite(0, 0);
}
