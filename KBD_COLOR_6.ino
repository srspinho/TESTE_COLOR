/*********************************************************************
 * PROJETO: HID Remapper & System Monitor v2.5 + Pac-Man SS
 * HARDWARE: RP2040/RP2350 + TFT 160x80 (ST7735)
 *********************************************************************/

#include "usbh_helper.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Adafruit_TinyUSB.h>
#include "RobotoMono12pt7b.h"
#include "Orbitron_Bold6pt7b.h"

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

// Screensaver: Pac-Man (Novo com Sprites)
void init_pong();
void update_pong();
void draw_pong();
void init_pacman();
void update_pacman();
void draw_pacman_ss();
void drawPacManShape(int x, int y, int mouthAngle);
void drawGhostShape(int x, int y, uint16_t color, bool vulnerable);

// --- VARIÁVEIS PONG ---
float ballX, ballY, ballDX, ballDY;
int paddle1Y, paddle2Y;
int scoreL = 0, scoreR = 0;
const int paddleH = 15;
const int paddleW = 3;
bool sprite_initialized = false;

/* --- HARDWARE --- */
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);  // Sprite buffer para o Pac-Man

/* --- VARIÁVEIS GLOBAIS --- */
volatile uint32_t g_key_count = 0;
volatile uint8_t g_volume = 70;
volatile bool g_display_dirty = true;
unsigned long last_activity_time = 0;
unsigned long teams_alert_timeout = 0;
unsigned long lastGraphUpdate = 0;
bool title_drawn = false;
int lastVolume = -1;

const uint32_t SS_SWITCH_INTERVAL = 30000;  // troca a cada 30 segundos
unsigned long last_ss_switch = 0;
bool is_dimmed = false;
bool is_screensaver = false;
int current_ss_type = 0;
const uint32_t DIM_TIMEOUT = 30000;
const uint32_t LIFE_TIMEOUT = 120000;

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

volatile uint8_t g_leds = 0;  // Armazena o estado atual dos LEDs (Caps, Num, etc)

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
  // ESSA LINHA É VITAL PARA O CAPS LOCK:
  usb_hid.setReportCallback(NULL, set_report_callback);

  usb_hid.begin();

  // Sincronização de Volume (Copiado da versão que funciona)
  delay(3000);  // Aguarda o PC reconhecer o dispositivo
  if (usb_hid.ready()) {
    // Abaixa tudo para garantir o ponto zero
    for (int i = 0; i < 50; i++) {
      uint8_t vol_down[2] = { 0xEA, 0x00 };
      usb_hid.sendReport(2, vol_down, 2);
      delay(10);
      uint8_t release[2] = { 0x00, 0x00 };
      usb_hid.sendReport(2, release, 2);
      delay(10);
    }
    // Sobe para o nível desejado (70%)
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

  // Inicialização de volume e tempo
  g_volume = 70;
  last_activity_time = millis();

  // Garante que o display inicie limpo
  firstDrawAfterSS = true;
  g_display_dirty = true;
}

void loop() {
  uint32_t now = millis();

  // 1. MONITOR DE ATIVIDADE DE TECLAS
  // Comparamos o contador global de teclas para saber se algo foi digitado
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
  g_display_dirty = true;   // <-- força redesenho imediato do contador
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

  // 3. LÓGICA DO SCREENSAVER
  if (idle_time > LIFE_TIMEOUT) {
    if (!is_screensaver) {
      is_screensaver = true;
      current_ss_type = 4;  // Começa pelo Pong para testar, depois segue o ciclo
      tft.fillScreen(TFT_BLACK);
      init_pong();
      last_ss_switch = now;
    }

    // Troca sequencial de screensaver (Ciclo 0 a 4)
    if (now - last_ss_switch > SS_SWITCH_INTERVAL) {
      current_ss_type = (current_ss_type + 1) % 5;
      tft.fillScreen(TFT_BLACK);

      switch (current_ss_type) {
        case 0: init_game(); break;
        case 1: init_matrix(); break;
        case 2: init_starfield(); break;
        case 3: init_pacman(); break;
        case 4: init_pong(); break;
      }
      last_ss_switch = now;
    }

    // Define a velocidade de atualização (frame rate) de cada animação
    uint32_t speed = 30;                         // Padrão
    if (current_ss_type == 0) speed = 80;        // Game of Life
    else if (current_ss_type == 1) speed = 120;  // Matrix
    else if (current_ss_type == 4) speed = 20;   // Pong (mais rápido)

    if (now - last_gen_time > speed) {
      switch (current_ss_type) {
        case 0: update_game(); break;
        case 1: update_matrix(); break;
        case 2: update_starfield(); break;
        case 3: update_pacman(); break;
        case 4: update_pong(); break;
      }
      g_display_dirty = true;
      last_gen_time = now;
    }

  } else {
    // 4. MODO MONITOR NORMAL (Quando não está em screensaver)
    if (idle_time > DIM_TIMEOUT) {
      is_dimmed = true;
    } else {
      is_dimmed = false;
    }
  }

  // 5. PROCESSAMENTO DE ATUALIZAÇÃO DA TELA
  // Verifica se o Core 1 (USB) enviou sinal de atualização via FIFO
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
      case 0: draw_game(); break;
      case 1: draw_matrix(); break;
      case 2: draw_starfield(); break;
      case 3: draw_pacman_ss(); break;
      case 4: draw_pong(); break;
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

  // redesenha sempre que g_key_count mudou
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

  // --- Indicador Caps Lock ---
//  if (g_leds & 0x02) { // bit 1 = Caps Lock
//    tft.setTextColor(TFT_RED, TFT_BLACK);
//    tft.drawString("CAPS ON", 120, 10);
//  } else {
//    tft.fillRect(120, 10, 40, 12, TFT_BLACK); // limpa área
//  }
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

  // IA Simples seguindo a bola
  if (ballY > paddle1Y + (paddleH / 2)) paddle1Y += 1;
  else paddle1Y -= 1;
  if (ballY > paddle2Y + (paddleH / 2)) paddle2Y += 1;
  else paddle2Y -= 1;

  // Colisão Topo e Baixo
  if (ballY <= 0 || ballY >= 78) ballDY *= -1;

  // Colisão Raquete Esquerda
  if (ballX <= (paddleW + 2) && ballY >= paddle1Y && ballY <= paddle1Y + paddleH) {
    ballDX *= -1.1;  // Aumenta velocidade gradualmente
    ballX = paddleW + 3;
  }

  // Colisão Raquete Direita
  if (ballX >= 160 - (paddleW + 5) && ballY >= paddle2Y && ballY <= paddle2Y + paddleH) {
    ballDX *= -1.1;
    ballX = 160 - (paddleW + 6);
  }

  // Pontuação e Reset
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

  // Linha central pontilhada
  for (int i = 0; i < 80; i += 10) canvas.drawFastVLine(80, i, 5, TFT_DARKGREY);

  // Placar
  canvas.setFreeFont(&Orbitron_Bold6pt7b);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawNumber(scoreL, 60, 5);
  canvas.drawNumber(scoreR, 95, 5);

  // Raquetes
  canvas.fillRect(2, paddle1Y, paddleW, paddleH, TFT_WHITE);
  canvas.fillRect(160 - paddleW - 2, paddle2Y, paddleW, paddleH, TFT_WHITE);

  // Bola (Quadrada como no Atari)
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

/* ==================== OUTROS SCREENSAVERS (RESUMIDOS) ==================== */
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
  for (int i = 0; i < MATRIX_COLS; i++) {
    int x = (i * 16) + 4;
    char c[2] = { (char)random(33, 126), 0 };
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(c, x, drop_pos[i]);
  }
}

void init_starfield() {
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < 50; i++) {
    stars[i].x = random(180);
    stars[i].y = random(80);
    stars[i].speed = random(1, 5);
    stars[i].color = TFT_WHITE;
    stars[i].size = 1;
  }
}
void update_starfield() {
  for (int i = 0; i < 50; i++) {
    tft.drawPixel(stars[i].x, stars[i].y, TFT_BLACK);
    stars[i].y += stars[i].speed;
    if (stars[i].y >= 80) {
      stars[i].y = 0;
      stars[i].x = random(180);
    }
  }
}
void draw_starfield() {
  for (int i = 0; i < 50; i++) tft.drawPixel(stars[i].x, stars[i].y, stars[i].color);
}


void set_report_callback(uint8_t report_id, hid_report_type_t report_type,
                         uint8_t const *buffer, uint16_t bufsize) {
  if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize > 0 && dev_addr_keyboard != 0) {
    uint8_t led_state = buffer[0];

    // Atualiza variável global
    if (led_state != g_leds) {
      g_leds = led_state;

      // Força envio com report_id = 0
      tuh_hid_set_report(dev_addr_keyboard, instance_keyboard, 0,
                         HID_REPORT_TYPE_OUTPUT, (void *)&g_leds, 1);

      Serial.printf("LEDs atualizados: %02X\n", g_leds);
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

/* --- CALLBACKS NATIVOS DO TINYUSB (HOST) --- */
extern "C" {
  void tuh_hid_mount_cb(uint8_t d, uint8_t i, uint8_t const *desc, uint16_t l) {
    // 1. Identifica se a interface atual segue o protocolo de teclado (HID_ITF_PROTOCOL_KEYBOARD)
    uint8_t itf_protocol = tuh_hid_interface_protocol(d, i);

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
      dev_addr_keyboard = d;
      instance_keyboard = i;

      // Opcional: Adicione um log para confirmar qual instância foi montada
      Serial.printf("Teclado montado: addr %d, instance %d\n", d, i);
    }

    // 2. É crucial chamar o receive_report para TODAS as instâncias montadas,
    // mas garantir que a instância do teclado seja a prioridade para o buffer
    tuh_hid_receive_report(d, i);
  }

  void tuh_hid_report_received_cb(uint8_t d, uint8_t i, uint8_t const *r, uint16_t l) {
    if (l == sizeof(hid_keyboard_report_t)) {
      hid_keyboard_report_t const *report = (hid_keyboard_report_t const *)r;

      // Processa estatísticas para o contador de teclas
      process_kbd_report(report);

      // Envia o relatório original para o PC
      // Isso faz o Windows alternar o estado do Caps Lock internamente
      if (usb_hid.ready()) {
        usb_hid.sendReport(1, report, sizeof(hid_keyboard_report_t));
      }
    }
    // Continua ouvindo o teclado real
    tuh_hid_receive_report(d, i);
  }
}