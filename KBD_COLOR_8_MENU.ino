/*********************************************************************
 * PROJETO: HID Remapper & System Monitor v2.5 + Pac-Man SS & Menu
 * HARDWARE: RP2040/RP2350 + TFT 160x80 (ST7735)
 *********************************************************************/

#include "usbh_helper.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Adafruit_TinyUSB.h>
#include "RobotoMono12pt7b.h"
#include "Orbitron_Bold6pt7b.h"
#include "atari.h"
#include "menu.h" // Integração com a persistência e UI do menu

/* --- PROTÓTIPOS --- */
void set_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize);
void process_kbd_report(hid_keyboard_report_t const *report);
void remap_key(hid_keyboard_report_t const *original, hid_keyboard_report_t *remapped);
void update_display();

// Screensaver: Game of Life
void init_game();
void update_game();
void draw_game();

// Screensaver: Matrix
void init_matrix();
void update_matrix();
void draw_matrix();

// Screensaver: Starfield
void init_starfield();
void update_starfield();
void draw_starfield();

// Screensaver: Pac-Man
void init_pong();
void update_pong();
void draw_pong();
void init_pacman();
void update_pacman();
void draw_pacman_ss();
void drawPacManShape(int x, int y, int mouthAngle);
void drawGhostShape(int x, int y, uint16_t color, bool vulnerable);

// Screensaver: Asteroids & Atari
void init_asteroids();
void update_asteroids();
void draw_asteroids();
void init_atari();
void update_atari();
void draw_atari_logo();
uint16_t rainbowColor(int pos);
uint16_t neonPulseColor(int pos, float intensity, int phase);

/* --- VARIÁVEIS PONG --- */
float ballX, ballY, ballDX, ballDY;
int paddle1Y, paddle2Y;
int scoreL = 0, scoreR = 0;
const int paddleH = 15;
const int paddleW = 3;
bool sprite_initialized = false;

int color_offset = 0;

// Offset global para o gradiente do logo Atari
int rainbow_offset = 0;
const int logoWidth = 74;  
const int logoHeight = 80;

/* --- HARDWARE --- */
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);  

/* --- VARIÁVEIS GLOBAIS --- */
volatile uint32_t g_key_count = 0;
volatile uint8_t g_volume = 70;
volatile bool g_display_dirty = true;
unsigned long last_activity_time = 0;
unsigned long teams_alert_timeout = 0;
unsigned long lastGraphUpdate = 0;
bool title_drawn = false;
int lastVolume = -1;

const uint32_t SS_SWITCH_INTERVAL = 30000;  // troca a cada 30 segundos se em modo SS_TODOS
unsigned long last_ss_switch = 0;
bool is_dimmed = false;
bool is_screensaver = false;
int current_ss_type = 0;
const uint32_t DIM_TIMEOUT = 30000;
const uint32_t LIFE_TIMEOUT = 120000; // Será sobrescrito pela config do menu no loop

bool g_em_modo_menu = false; // Controla se o visor exibe o menu ou o monitor

// Variáveis Pac-Man
int pac_posX = -60;
bool pac_initialized = false;

// Game of Life Vars
#define GOL_W 80
#define GOL_H 40
uint8_t world[GOL_W][GOL_H];
uint8_t next_world[GOL_W][GOL_H];
uint8_t world_last[GOL_W][GOL_H];
uint32_t history_checksum[4];
uint32_t last_gen_time = 0;
int stable_count = 0;

// Starfield Vars
struct Star {
  int x;
  int y;
  int speed;
  uint16_t color;
  int size;
};
Star stars[50];

// Asteroids Vars
struct Asteroid {
  int x, y;
  int vx, vy;
  int size;
  int points;
  int angle[10]; 
  int radius[10]; 
};
#define MAX_ASTEROIDS 20 // Limite seguro para o array fixo
Asteroid asteroids[MAX_ASTEROIDS];

// Gráfico Vars
#define GRAPH_WIDTH 60
#define GRAPH_HEIGHT 20
int history[GRAPH_WIDTH] = { 0 };
int maxKeys = 20;
int contadorDeTeclas = 0;
bool graphNeedsUpdate = true;
static char last_buf[12] = "";
static int16_t last_xPos = 0;

// Matrix Vars
#define MATRIX_COLS 10
int8_t drop_pos[MATRIX_COLS];
uint8_t drop_speed[MATRIX_COLS];
uint32_t matrix_start_time = 0;
char matrix_last[MATRIX_COLS][8];

volatile uint8_t g_leds = 0;  

// USB Vars
volatile uint8_t dev_addr_keyboard = 0;
volatile uint8_t instance_keyboard = 0;
volatile uint64_t keys_currently_pressed = 0;
#define FIFO_DISPLAY_UPDATE 1

uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2))
};
Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_KEYBOARD, 2, false);
bool firstDrawAfterSS = true;

/* ================================================================
   CORE 1: USB HOST
   ================================================================ */
void setup1() {
  rp2040_configure_pio_usb();
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();
}

/* ================================================================
   CORE 0: DISPLAY & LOGIC
   ================================================================ */
void setup() {
  usb_hid.setReportCallback(NULL, set_report_callback);
  usb_hid.begin();

  // Sincronização de Volume Nativa
  delay(3000);  
  if (usb_hid.ready()) {
    for (int i = 0; i < 50; i++) {
      uint8_t vol_down[2] = { 0xEA, 0x00 };
      usb_hid.sendReport(2, vol_down, 2);
      delay(10);
      uint8_t release[2] = { 0x00, 0x00 };
      usb_hid.sendReport(2, release, 2);
      delay(10);
    }
    for (int i = 0; i < 35; i++) {
      uint8_t vol_up[2] = { 0xE9, 0x00 };
      usb_hid.sendReport(2, vol_up, 2);
      delay(10);
      uint8_t release[2] = { 0x00, 0x00 };
      usb_hid.sendReport(2, release, 2);
      delay(10);
    }
    g_volume = 70;
  }

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  randomSeed(analogRead(26) + micros());

  // Inicializa a engine do menu.cpp (EEPROM + pinos dos botões)
  setup_menu();

  // Aplica backlight inicial se definido por hardware
  #ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, map(config.sistema_brilho, 0, 100, 0, 255));
  #endif

  g_volume = 70;
  last_activity_time = millis();
  firstDrawAfterSS = true;
  g_display_dirty = true;
}

void loop() {
  uint32_t now = millis();

  // Processa cliques e debounce físico dos botões do menu
  tratar_botoes();

  // Ajusta o hardware de brilho em tempo real com base no menu
  #ifdef TFT_BL
    analogWrite(TFT_BL, map(config.sistema_brilho, 0, 100, 0, 255));
  #endif

  if (g_em_modo_menu) {
    extern MenuLevel current_menu_level;
    // Se pressionar BACK na raiz do Menu, volta para a tela normal do contador
    if (current_menu_level == LEVEL_MAIN && digitalRead(BOTAO_BACK) == LOW) {
      g_em_modo_menu = false;
      firstDrawAfterSS = true;
      g_display_dirty = true;
      last_activity_time = millis();
      delay(200);
    }
    return; // Interrompe o processamento do monitor para focar na renderização do menu
  }

  // Se clicar em SELECT fora do menu, abre a interface de configurações
  if (digitalRead(BOTAO_SELECT) == LOW && !is_editing_value) {
    g_em_modo_menu = true;
    desenhar_menu();
    delay(200);
    return;
  }

  // 1. MONITOR DE ATIVIDADE DE TECLAS
  static uint32_t last_key_val = 0;
  if (g_key_count != last_key_val) {
    last_key_val = g_key_count;
    last_activity_time = now;

    if (is_screensaver || is_dimmed) {
      is_screensaver = false;
      is_dimmed = false;
      firstDrawAfterSS = true;
      g_display_dirty = true;
      tft.fillScreen(TFT_BLACK);
    }

    contadorDeTeclas++;
    g_display_dirty = true;  
  }

  // 2. ATUALIZAÇÃO DO GRÁFICO (A cada 1 segundo)
  if (now - lastGraphUpdate >= 1000) {
    lastGraphUpdate = now;
    for (int i = 0; i < GRAPH_WIDTH - 1; i++) history[i] = history[i + 1];
    history[GRAPH_WIDTH - 1] = contadorDeTeclas;
    contadorDeTeclas = 0;
    graphNeedsUpdate = true;
    g_display_dirty = true;
  }

  uint32_t idle_time = now - last_activity_time;
  
  // Resgata o tempo de início do screensaver configurado no menu (minutos para ms)
  unsigned long menu_life_timeout = (unsigned long)config.prod_inicio * 60 * 1000;

  // 3. LÓGICA DO SCREENSAVER
  if (idle_time > menu_life_timeout) {
    if (!is_screensaver) {
      is_screensaver = true;
      tft.fillScreen(TFT_BLACK);
      
      // Define qual screensaver vai abrir inicialmente com base no menu
      if (config.ss_selecionado == SS_TODOS) {
        current_ss_type = 4; // Começa pelo Pong padrão
      } else {
        current_ss_type = config.ss_selecionado;
      }

      switch (current_ss_type) {
        case 0: init_atari(); break; // Força mapeamento correto do enum para o loop
        case 1: init_pong(); break;
        case 2: init_asteroids(); break;
        case 3: init_starfield(); break;
        case 4: init_game(); break;
        case 5: init_matrix(); break;
        default: init_pacman(); break;
      }
      last_ss_switch = now;
    }

    // Se estiver configurado para rotacionar ("Todos"), aplica o switch sequencial
    if (config.ss_selecionado == SS_TODOS && (now - last_ss_switch > SS_SWITCH_INTERVAL)) {
      current_ss_type = (current_ss_type + 1) % 7;
      tft.fillScreen(TFT_BLACK);

      switch (current_ss_type) {
        case 0: init_atari(); break;
        case 1: init_pong(); break;
        case 2: init_asteroids(); break;
        case 3: init_starfield(); break;
        case 4: init_game(); break;
        case 5: init_matrix(); break;
        case 6: init_pacman(); break;
      }
      last_ss_switch = now;
    }

    // Taxa de atualização (frame rate) dinâmica via parâmetro "Veloc. Gradiente" do menu
    uint32_t speed = map(config.vel_gradiente, 1, 20, 150, 10);
    if (current_ss_type == 4) speed += 50; // Ajuste proporcional extra para o Game of Life
    if (current_ss_type == 5) speed += 80; // Ajuste proporcional extra para a Matrix

    if (now - last_gen_time > speed) {
      switch (current_ss_type) {
        case 0: update_atari(); break;
        case 1: update_pong(); break;
        case 2: update_asteroids(); break;
        case 3: update_starfield(); break;
        case 4: update_game(); break;
        case 5: update_matrix(); break;
        case 6: update_pacman(); break;
      }
      g_display_dirty = true;
      last_gen_time = now;
    }

  } else {
    // 4. MODO MONITOR NORMAL
    if (idle_time > DIM_TIMEOUT) {
      is_dimmed = true;
    } else {
      is_dimmed = false;
    }
  }

  // 5. PROCESSAMENTO DE ATUALIZAÇÃO DA TELA (Sinalizações via FIFO)
  while (rp2040.fifo.available()) {
    if (rp2040.fifo.pop() == FIFO_DISPLAY_UPDATE) {
      g_display_dirty = true;
    }
  }

  if (g_display_dirty) {
    update_display();
    g_display_dirty = false;
  }
}

void update_display() {
  if (is_screensaver) {
    switch (current_ss_type) {
      case 0: draw_atari_logo(); break;
      case 1: draw_pong(); break;
      case 2: draw_asteroids(); break;
      case 3: draw_starfield(); break;
      case 4: draw_game(); break;
      case 5: draw_matrix(); break;
      case 6: draw_pacman_ss(); break;
    }
    return;
  }

  if (firstDrawAfterSS) {
    tft.fillScreen(TFT_BLACK);
    firstDrawAfterSS = false;
    graphNeedsUpdate = true;
    title_drawn = false;
    lastVolume = -1;
  }

  // --- Título ---
  if (!title_drawn) {
    tft.setFreeFont(&Orbitron_Bold6pt7b);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("SYSTEM MONITOR v2", 0, 0);
    tft.drawLine(0, 14, 160, 14, TFT_DARKGREY);
    title_drawn = true;
  }

  // --- Contador (instantâneo) ---
  tft.setFreeFont(&RobotoMono_VariableFont_wght12pt7b);
  char buf[12];
  sprintf(buf, "%lu", g_key_count);
  int16_t xPos = (160 - tft.textWidth(buf)) / 2;
  uint16_t color = (g_key_count < 1000) ? TFT_GREEN : (g_key_count < 5000) ? TFT_YELLOW : TFT_RED;

  if (strcmp(buf, last_buf) != 0) {
    tft.fillRect(0, 22, 160, 20, TFT_BLACK);
    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(buf, xPos, 22);
    strcpy(last_buf, buf);
  }

  // --- Gráfico (apenas a cada 1s) ---
  if (graphNeedsUpdate) {
    tft.fillRect(8, 58, 72, 18, tft.color565(32, 32, 32));
    tft.drawRect(7, 57, 72, 20, TFT_WHITE);
    for (int i = 0; i < GRAPH_WIDTH - 1; i++) {
      int h1 = map(history[i], 0, maxKeys, 0, 16);
      int h2 = map(history[i + 1], 0, maxKeys, 0, 16);
      tft.drawLine(10 + i, 72 - h1, 11 + i, 72 - h2, TFT_GREEN);
    }
    graphNeedsUpdate = false;
  }

  // --- Volume ---
  if (lastVolume != g_volume) {
    tft.setFreeFont(&Orbitron_Bold6pt7b);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("VOLUME", 84, 56);
    tft.drawRect(85, 70, 65, 8, TFT_WHITE);
    int barWidth = map(g_volume, 0, 100, 0, 61);
    tft.fillRect(87, 72, barWidth, 4, (g_volume < 50) ? TFT_BLUE : TFT_GREEN);
    tft.fillRect(87 + barWidth, 72, 61 - barWidth, 4, TFT_BLACK);
    lastVolume = g_volume;
  }

  // --- Indicador Caps Lock & Indicador de Produtividade Ativa ---
  if (g_leds & 0x02) {  
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("CAPS ON", 120, 10);
  } else if (config.envio_ctrl_shift) {
    // Exibe sinalizador discreto de Auto-produtividade ativa se o Caps estiver desligado
    tft.setFreeFont(NULL);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("P.ON", 135, 2);
  } else {
    tft.fillRect(120, 2, 40, 12, TFT_BLACK);  
  }
}

/* ==================== SCREENSAVER: ASTEROIDS ==================== */
void init_asteroids() {
  tft.fillScreen(TFT_BLACK);
  // Usa dinamicamente o parâmetro "Qtd Asteroides" do menu com limite de segurança do array
  int total_asteroides = constrain(config.qtd_asteroides, 1, MAX_ASTEROIDS);
  
  for (int i = 0; i < total_asteroides; i++) {
    asteroids[i].x = random(20, tft.width()-20);
    asteroids[i].y = random(20, tft.height()-20);
    asteroids[i].vx = random(-3, 4);
    asteroids[i].vy = random(-3, 4);
    if (asteroids[i].vx == 0 && asteroids[i].vy == 0) { asteroids[i].vx = 1; asteroids[i].vy = 1; }
    asteroids[i].size = random(10, 20);
    asteroids[i].points = random(6, 10);
    for (int p = 0; p < asteroids[i].points; p++) {
      asteroids[i].angle[p] = (360 / asteroids[i].points) * p + random(-15, 15);
      asteroids[i].radius[p] = asteroids[i].size + random(-3, 3);
    }
  }
}

void update_asteroids() {
  color_offset += 2;
  if (color_offset > 2000) color_offset = 0;

  int total_asteroides = constrain(config.qtd_asteroides, 1, MAX_ASTEROIDS);
  for (int i = 0; i < total_asteroides; i++) {
    asteroids[i].x += asteroids[i].vx;
    asteroids[i].y += asteroids[i].vy;

    if (asteroids[i].x < 0 || asteroids[i].x > tft.width()) asteroids[i].vx *= -1;
    if (asteroids[i].y < 0 || asteroids[i].y > tft.height()) asteroids[i].vy *= -1;
  }
}

void draw_asteroids() {
  tft.fillScreen(TFT_BLACK);
  int total_asteroides = constrain(config.qtd_asteroides, 1, MAX_ASTEROIDS);
  for (int i = 0; i < total_asteroides; i++) {
    uint16_t color = rainbowColor(asteroids[i].x + asteroids[i].y);
    for (int p = 0; p < asteroids[i].points; p++) {
      int x1 = asteroids[i].x + cos(radians(asteroids[i].angle[p])) * asteroids[i].radius[p];
      int y1 = asteroids[i].y + sin(radians(asteroids[i].angle[p])) * asteroids[i].radius[p];
      int x2 = asteroids[i].x + cos(radians(asteroids[i].angle[(p+1)%asteroids[i].points])) * asteroids[i].radius[(p+1)%asteroids[i].points];
      int y2 = asteroids[i].y + sin(radians(asteroids[i].angle[(p+1)%asteroids[i].points])) * asteroids[i].radius[(p+1)%asteroids[i].points];
      tft.drawLine(x1, y1, x2, y2, color);
    }
  }
}

/* ==================== SCREENSAVER: ATARI RAINBOW ==================== */
void init_atari() {
  tft.fillScreen(TFT_BLACK);
  rainbow_offset = 0;
}

uint16_t rainbowColor(int pos) {
  byte r = (sin(0.05 * (pos + color_offset) + 0) * 127) + 128;
  byte g = (sin(0.05 * (pos + color_offset) + 2) * 127) + 128;
  byte b = (sin(0.05 * (pos + color_offset) + 4) * 127) + 128;
  return tft.color565(r, g, b);
}

uint16_t neonPulseColor(int pos, float intensity, int phase) {
  float pulse = (sin(0.02 * color_offset + phase) + 1.0) / 2.0;
  // Integração real: Converte a intensidade do Menu (0-100) para multiplicador float (0.0 a 1.0)
  float menu_mult = (float)config.intensidade_cores / 100.0;
  
  byte r = (sin(0.05 * (pos + color_offset) + 0) * 127 + 128) * pulse * intensity * menu_mult;
  byte g = (sin(0.05 * (pos + color_offset) + 2) * 127 + 128) * pulse * intensity * menu_mult;
  byte b = (sin(0.05 * (pos + color_offset) + 4) * 127 + 128) * pulse * intensity * menu_mult;
  return tft.color565(r, g, b);
}

void update_atari() {
  color_offset += 2;  
  if (color_offset > 2000) color_offset = 0;
}

void draw_atari_logo() {
  int x0 = 43;
  int y0 = 0;

  for (int y = 0; y < logoHeight; y++) {
    for (int x = 0; x < logoWidth; x++) {
      uint16_t baseColor = atari[y * logoWidth + x];
      if (baseColor != TFT_BLACK) {
        uint16_t color = rainbowColor(x + y + color_offset);
        tft.drawPixel(x0 + x, y0 + y, color);
      }
    }
  }

  tft.setFreeFont(&Orbitron_Bold6pt7b);
  tft.setTextColor(rainbowColor(color_offset + 60), TFT_BLACK);
  tft.drawString("ATARI", x0 + 13, y0 + logoHeight - 12);
}

/* ==================== SCREENSAVER: PONG ==================== */
void init_pong() {
  if (!sprite_initialized) {
    canvas.createSprite(160, 80);
    sprite_initialized = true;
  }
  ballX = 80;
  ballY = 40;
  ballDX = (random(0, 2) == 0) ? 1.5 : -1.5;
  ballDY = 1.2;
  paddle1Y = 30;
  paddle2Y = 30;
}

void update_pong() {
  ballX += ballDX;
  ballY += ballDY;

  if (ballY > paddle1Y + (paddleH / 2)) paddle1Y += 1;
  else paddle1Y -= 1;
  if (ballY > paddle2Y + (paddleH / 2)) paddle2Y += 1;
  else paddle2Y -= 1;

  if (ballY <= 0 || ballY >= 78) ballDY *= -1;

  if (ballX <= (paddleW + 2) && ballY >= paddle1Y && ballY <= paddle1Y + paddleH) {
    ballDX *= -1.1;  
    ballX = paddleW + 3;
  }

  if (ballX >= 160 - (paddleW + 5) && ballY >= paddle2Y && ballY <= paddle2Y + paddleH) {
    ballDX *= -1.1;
    ballX = 160 - (paddleW + 6);
  }

  if (ballX < 0) {
    scoreR++;
    init_pong();
  }
  if (ballX > 160) {
    scoreL++;
    init_pong();
  }
}

void draw_pong() {
  canvas.fillSprite(TFT_BLACK);
  for (int i = 0; i < 80; i += 10) canvas.drawFastVLine(80, i, 5, TFT_DARKGREY);

  canvas.setFreeFont(&Orbitron_Bold6pt7b);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawNumber(scoreL, 60, 5);
  canvas.drawNumber(scoreR, 95, 5);

  canvas.fillRect(2, paddle1Y, paddleW, paddleH, TFT_WHITE);
  canvas.fillRect(160 - paddleW - 2, paddle2Y, paddleW, paddleH, TFT_WHITE);
  canvas.fillRect((int)ballX, (int)ballY, 3, 3, TFT_WHITE);

  canvas.pushSprite(0, 0);
}

/* ==================== SCREENSAVER: PAC-MAN ==================== */
void init_pacman() {
  if (!pac_initialized) {
    canvas.createSprite(160, 80);
    pac_initialized = true;
  }
  pac_posX = -60;
}

void update_pacman() {
  pac_posX += 3;
  if (pac_posX > 220) pac_posX = -60;
}

void draw_pacman_ss() {
  canvas.fillSprite(TFT_BLACK);
  int mouthSize = abs(sin(millis() / 150.0) * 15);
  bool isVulnerable = (pac_posX > 110);

  for (int i = 15; i < 160; i += 20) {
    if (i > (pac_posX + 10)) {
      if (i > 130) canvas.fillCircle(i, 40, 4, TFT_WHITE);
      else canvas.fillCircle(i, 40, 2, TFT_WHITE);
    }
  }
  drawGhostShape(pac_posX - 45, 40, TFT_RED, isVulnerable);
  drawPacManShape(pac_posX, 40, mouthSize);
  canvas.pushSprite(0, 0);
}

void drawPacManShape(int x, int y, int mouthAngle) {
  canvas.fillCircle(x, y, 15, TFT_YELLOW);
  if (mouthAngle > 0) {
    canvas.fillTriangle(x, y, x + 20, y - mouthAngle, x + 20, y + mouthAngle, TFT_BLACK);
  }
}

void drawGhostShape(int x, int y, uint16_t color, bool vulnerable) {
  uint16_t gColor = vulnerable ? TFT_BLUE : color;
  canvas.fillRect(x - 12, y - 5, 24, 18, gColor);
  canvas.fillCircle(x, y - 5, 12, gColor);
  if (!vulnerable) {
    canvas.fillCircle(x - 5, y - 6, 3, TFT_WHITE);
    canvas.fillCircle(x + 5, y - 6, 3, TFT_WHITE);
    canvas.fillCircle(x - 5, y - 6, 1, TFT_BLUE);
    canvas.fillCircle(x + 5, y - 6, 1, TFT_BLUE);
  } else {
    canvas.fillCircle(x - 5, y - 6, 2, TFT_WHITE);
    canvas.fillCircle(x + 5, y - 6, 2, TFT_WHITE);
    canvas.drawFastHLine(x - 6, y + 5, 12, TFT_WHITE);
  }
}

/* ==================== SCREENSAVER: GAME OF LIFE ==================== */
void init_game() {
  tft.fillScreen(TFT_BLACK);
  for (int x = 0; x < GOL_W; x++)
    for (int y = 0; y < GOL_H; y++) world[x][y] = (random(100) < 35);
  for (int i = 0; i < 4; i++) history_checksum[i] = i;
  stable_count = 0;
}

void update_game() {
  uint32_t pop = 0, cksum = 0;
  for (int x = 0; x < GOL_W; x++) {
    for (int y = 0; y < GOL_H; y++) {
      int n = 0;
      for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++)
          if (i || j) n += world[(x + i + GOL_W) % GOL_W][(y + j + GOL_H) % GOL_H];
      next_world[x][y] = world[x][y] ? (n == 2 || n == 3) : (n == 3);
      if (next_world[x][y]) {
        pop++;
        cksum += (x * 31 + y * 7);
      }
    }
  }
  if (pop == 0) stable_count = 20;
  memcpy(world, next_world, sizeof(world));
}

void draw_game() {
  for (int x = 0; x < GOL_W; x++) {
    for (int y = 0; y < GOL_H; y++) {
      if (world[x][y] != world_last[x][y]) {
        tft.fillRect(x * 2, y * 2, 2, 2, world[x][y] ? tft.color565(x * 4, y * 8, 128) : TFT_BLACK);
        world_last[x][y] = world[x][y];
      }
    }
  }
}

/* ==================== SCREENSAVER: MATRIX ==================== */
void init_matrix() {
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < MATRIX_COLS; i++) {
    drop_pos[i] = random(-30, 0);
    drop_speed[i] = random(1, 4);
  }
}
void update_matrix() {
  for (int i = 0; i < MATRIX_COLS; i++) {
    drop_pos[i] += drop_speed[i];
    if (drop_pos[i] > 90) drop_pos[i] = -20;
  }
}
void draw_matrix() {
  tft.setTextFont(1);
  // Usa o parâmetro "Intensidade Cores" (0-100) para modular a cor verde da chuva
  uint8_t brilho_verde = map(config.intensidade_cores, 0, 100, 40, 255);
  uint16_t cor_matrix = tft.color565(0, brilho_verde, 0);

  for (int i = 0; i < MATRIX_COLS; i++) {
    int x = (i * 16) + 4;
    char c[2] = { (char)random(33, 126), 0 };
    tft.setTextColor(cor_matrix, TFT_BLACK);
    tft.drawString(c, x, drop_pos[i]);
  }
}

/* ==================== SCREENSAVER: STARFIELD ==================== */
void init_starfield() {
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < 50; i++) {
    stars[i].x = random(0, 180);
    stars[i].y = random(0, 80);
    stars[i].size = random(1, 3);
    if (stars[i].size == 1) stars[i].speed = random(1, 3);
    else stars[i].speed = random(2, 5);

    int choice = random(0, 3);
    if (choice == 0) stars[i].color = TFT_WHITE;
    else if (choice == 1) stars[i].color = tft.color565(255, 255, 128); 
    else stars[i].color = tft.color565(128, 200, 255); 
  }
}

void update_starfield() {
  for (int i = 0; i < 50; i++) {
    tft.fillRect(stars[i].x, stars[i].y, stars[i].size, stars[i].size, TFT_BLACK);
    stars[i].y += stars[i].speed;

    if (stars[i].y >= 80) {
      stars[i].y = 0;
      stars[i].x = random(0, 180);
      stars[i].size = random(1, 3);
      if (stars[i].size == 1) stars[i].speed = random(1, 3);
      else stars[i].speed = random(2, 5);

      int choice = random(0, 3);
      if (choice == 0) stars[i].color = TFT_WHITE;
      else if (choice == 1) stars[i].color = tft.color565(255, 255, 128);
      else stars[i].color = tft.color565(128, 200, 255);
    }
    tft.fillRect(stars[i].x, stars[i].y, stars[i].size, stars[i].size, stars[i].color);
  }
}

void draw_starfield() {
  for (int i = 0; i < 50; i++) {
    tft.fillRect(stars[i].x, stars[i].y, stars[i].size, stars[i].size, stars[i].color);
  }
}

/* ================================================================
   DRIVERS HID E CALLBACKS NATIVOS DO TINYUSB HOST/DEVICE
   ================================================================ */
void set_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
  if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize > 0 && dev_addr_keyboard != 0) {
    uint8_t led_state = buffer[0];
    if (led_state != g_leds) {
      g_leds = led_state;
      tuh_hid_set_report(dev_addr_keyboard, instance_keyboard, 0, HID_REPORT_TYPE_OUTPUT, (void *)&g_leds, 1);
      Serial.printf("LEDs atualizados: %02X\n", g_leds);
    }
  }
}

void remap_key(hid_keyboard_report_t const *original, hid_keyboard_report_t *remapped) {
  memcpy(remapped, original, sizeof(hid_keyboard_report_t));
  bool altGr = (original->modifier & KEYBOARD_MODIFIER_RIGHTALT);
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t key = original->keycode[i];
    if (altGr && key == HID_KEY_R) {
      remapped->modifier &= ~KEYBOARD_MODIFIER_RIGHTALT;
      remapped->keycode[i] = 0x64; // Remapeia para barra invertida '\'
    } else if (altGr && key == HID_KEY_M) {
      remapped->modifier &= ~KEYBOARD_MODIFIER_RIGHTALT;
      remapped->modifier |= (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTSHIFT);
      teams_alert_timeout = millis() + 1500;
      rp2040.fifo.push_nb(FIFO_DISPLAY_UPDATE);
    }
  }
}

void process_kbd_report(hid_keyboard_report_t const *report) {
  static uint64_t last_pressed = 0;
  uint64_t current_pressed = 0;
  for (int i = 0; i < 6; i++)
    if (report->keycode[i]) current_pressed |= (1ULL << report->keycode[i]);
  if (current_pressed & ~last_pressed) g_key_count++;
  last_pressed = current_pressed;
}

extern "C" {
  void tuh_hid_mount_cb(uint8_t d, uint8_t i, uint8_t const *desc, uint16_t l) {
    uint8_t itf_protocol = tuh_hid_interface_protocol(d, i);
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
      dev_addr_keyboard = d;
      instance_keyboard = i;
      Serial.printf("Teclado montado: addr %d, instance %d\n", d, i);
    }
    tuh_hid_receive_report(d, i);
  }

  void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    if (len == sizeof(hid_keyboard_report_t)) {
      hid_keyboard_report_t const *kbd_report = (hid_keyboard_report_t const *)report;
      process_kbd_report(kbd_report);
      hid_keyboard_report_t remapped;
      remap_key(kbd_report, &remapped);
      if (usb_hid.ready()) usb_hid.sendReport(1, &remapped, sizeof(hid_keyboard_report_t));
    } 
    else if (len == 3 && report[0] == 0x03) {
      int delta = (report[1] == 0xE9) ? 2 : (report[1] == 0xEA) ? -2 : 0;
      if (delta != 0) {
        g_volume = constrain(g_volume + delta, 0, 100);
        rp2040.fifo.push_nb(FIFO_DISPLAY_UPDATE);
      }
      uint8_t media_data[2] = { report[1], report[2] };
      if (usb_hid.ready()) usb_hid.sendReport(2, media_data, 2);
    }
    tuh_hid_receive_report(dev_addr, instance);
  }
}