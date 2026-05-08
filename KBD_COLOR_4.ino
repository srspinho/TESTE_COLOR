/*********************************************************************
 * PROJETO: HID Remapper & System Monitor v2.5
 * HARDWARE: RP2040 + TFT 160x80
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
void init_game();
void update_game();
void draw_game();
void init_matrix();
void update_matrix();
void draw_matrix();

/* --- HARDWARE --- */
TFT_eSPI tft = TFT_eSPI();

/* --- VARIÁVEIS GLOBAIS --- */
volatile uint32_t g_key_count = 0;
volatile uint8_t g_volume = 70;
volatile bool g_display_dirty = true;

unsigned long last_activity_time = 0;
unsigned long teams_alert_timeout = 0;
unsigned long lastGraphUpdate = 0;

// Flags de estado para controle de redesenho
bool title_drawn = false;
int lastVolume = -1;

bool is_dimmed = false;
bool is_screensaver = false;
int current_ss_type = 0;

const uint32_t DIM_TIMEOUT = 30000;
const uint32_t LIFE_TIMEOUT = 120000;

// Game of Life
#define GOL_W 80
#define GOL_H 40
uint8_t world[GOL_W][GOL_H];
uint8_t next_world[GOL_W][GOL_H];
uint32_t history_checksum[4];
uint32_t last_gen_time = 0;
int stable_count = 0;

// Gráfico
#define GRAPH_WIDTH 60
#define GRAPH_HEIGHT 20
int history[GRAPH_WIDTH] = { 0 };
int maxKeys = 20;
int contadorDeTeclas = 0;
bool graphNeedsUpdate = true;

static char last_buf[12] = "";
static int16_t last_xPos = 0;


// Matrix
#define MATRIX_COLS 10
int8_t drop_pos[MATRIX_COLS];
uint8_t drop_speed[MATRIX_COLS];
uint32_t matrix_start_time = 0;

// USB
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
   CORE 0: DISPLAY
   ================================================================ */

void updateHistory(int newValue) {
  for (int i = 0; i < GRAPH_WIDTH - 1; i++) {
    history[i] = history[i + 1];
  }
  history[GRAPH_WIDTH - 1] = newValue;
}

void desenharGrafico() {
  // Limpa a área interna do gráfico para remover resíduos do screensaver [cite: 130]
  tft.fillRect(8, 58, 72, 18, TFT_BLACK);
  tft.drawRect(7, 57, 72, 20, TFT_DARKGREY);  // Moldura [cite: 131]

  for (int i = 0; i < GRAPH_WIDTH - 1; i++) {
    int h1 = map(history[i], 0, maxKeys, 0, 16);
    int h2 = map(history[i + 1], 0, maxKeys, 0, 16);
    tft.drawLine(10 + i, 72 - h1, 10 + i + 1, 72 - h2, TFT_CYAN);
  }
}

void loop() {
  uint32_t now = millis();

  // Detecção de teclas
  static uint32_t last_key_val = 0;
  if (g_key_count != last_key_val) {
    last_key_val = g_key_count;
    last_activity_time = now;
    if (is_dimmed || is_screensaver) {
      is_dimmed = false;
      is_screensaver = false;
      firstDrawAfterSS = true;
      g_display_dirty = true;
    }
    contadorDeTeclas++;
  }

  // Atualiza gráfico
  if (now - lastGraphUpdate >= 1000) {
    lastGraphUpdate = now;
    updateHistory(contadorDeTeclas);
    contadorDeTeclas = 0;
    graphNeedsUpdate = true;
    g_display_dirty = true;
  }

  uint32_t idle_time = now - last_activity_time;

  if (idle_time > LIFE_TIMEOUT) {
    if (!is_screensaver) {
      is_screensaver = true;
      current_ss_type = random(0, 2);
      tft.fillScreen(TFT_BLACK);
      if (current_ss_type == 0) init_game();
      else init_matrix();
    }
    uint32_t speed = (current_ss_type == 0) ? 80 : 120;
    if (now - last_gen_time > speed) {
      if (current_ss_type == 0) update_game();
      else update_matrix();
      g_display_dirty = true;
      last_gen_time = now;
    }
  } else {
    // Dentro do loop(), quando detecta saída do screensaver:
    if (is_screensaver) {
      is_screensaver = false;
      firstDrawAfterSS = true;
      g_display_dirty = true;

      // CORREÇÃO: resetar flags para forçar redesenho completo
      last_buf[0] = '\0';
      last_xPos = 0;
      title_drawn = false;
      lastVolume = -1;
    }
    if (idle_time > DIM_TIMEOUT) {
      if (!is_dimmed) is_dimmed = true;
      static uint32_t last_blink = 0;
      if (now - last_blink > 1000) {
        g_display_dirty = true;
        last_blink = now;
      }
    } else {
      is_dimmed = false;
    }
  }

  while (rp2040.fifo.available()) {
    if (rp2040.fifo.pop() == FIFO_DISPLAY_UPDATE) g_display_dirty = true;
  }

  if (g_display_dirty) {
    update_display();
    g_display_dirty = false;
  }
}

void setup() {

  usb_hid.setReportCallback(NULL, set_report_callback);
  usb_hid.begin();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  firstDrawAfterSS = true;  // Isso forçará a update_display a desenhar tudo no primeiro ciclo [cite: 127]
  graphNeedsUpdate = true;

  randomSeed(analogRead(26) + micros());

  // Sincronização de volume
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
  last_activity_time = millis();
}

/*
void update_display() {
  // Se estiver em modo Screensaver, desenha e sai
  if (is_screensaver) {
    if (current_ss_type == 0) draw_game();
    else draw_matrix();
    return;
  }

  // CORREÇÃO: Se acabou de voltar do Screensaver ou iniciou agora, limpa a tela física
  if (firstDrawAfterSS) {
    tft.fillScreen(TFT_BLACK);
    firstDrawAfterSS = false;
    graphNeedsUpdate = true;  // Força o desenho do gráfico no primeiro quadro
    // Limpa os buffers de comparação para forçar o redesenho dos textos
    last_buf[0] = '\0';
  }

  // 1. TÍTULO
  tft.setFreeFont(&Orbitron_Bold6pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("SYSTEM MONITOR v2", 0, 2);

  // 2. CONTADOR CENTRAL (Anti-Flicker sem Sprite)
  tft.setFreeFont(&RobotoMono_VariableFont_wght12pt7b);
  char buf[12];
  sprintf(buf, "%lu", g_key_count);
  int16_t xPos = (160 - tft.textWidth(buf)) / 2;

  // Só redesenha se o número mudou ou se a tela foi limpa agora
  if (strcmp(buf, last_buf) != 0) {
    tft.setTextColor(TFT_BLACK, TFT_BLACK);
    tft.drawString(last_buf, last_xPos, 22);  // Apaga o rastro antigo

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, xPos, 22);  // Desenha o novo

    strcpy(last_buf, buf);
    last_xPos = xPos;
  }

  // 3. TEAMS MUTE ALERT
  if (millis() < teams_alert_timeout) {
    tft.drawRect(20, 38, 120, 20, TFT_RED);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("TEAMS MUTE", 35, 43);
  } else {
    // Se o alerta acabou de sumir, limpa aquela área específica
    static bool alert_active = false;
    if (alert_active) {
      tft.fillRect(20, 38, 120, 22, TFT_BLACK);
      alert_active = false;
    }
  }

  // 4. GRÁFICO (Resolve o problema de não aparecer no início)
  if (graphNeedsUpdate) {
    desenharGrafico();
    graphNeedsUpdate = false;
  }

  // 5. BARRA DE VOLUME
  tft.setFreeFont(&Orbitron_Bold6pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("VOLUME", 84, 56);
  tft.drawRect(85, 70, 65, 8, TFT_WHITE);
  int barWidth = map(g_volume, 0, 100, 0, 61);
  tft.fillRect(87, 72, barWidth, 4, TFT_GREEN);
  tft.fillRect(87 + barWidth, 72, 61 - barWidth, 4, TFT_BLACK);  // Limpa o resto da barra

  // 6. Indicador de Night Mode (Movido para o canto inferior direito)
  if (is_dimmed) {
    if ((millis() % 2000 < 1000)) {
      // x=150 (canto direito), y=74 (alinhado com a barra de volume)
      tft.fillCircle(150, 30, 3, TFT_YELLOW);
    } else {
      tft.fillCircle(150, 30, 3, TFT_BLACK);  // Apaga para piscar
    }
  }
}
*/

void update_display() {
  // Se estiver em modo Screensaver, desenha e sai
 if (is_screensaver) {
    if (current_ss_type == 0) draw_game();
    else draw_matrix();
    return;
  }

  // Se acabou de voltar do Screensaver, força redesenho inicial
  if (firstDrawAfterSS) {
    tft.fillScreen(TFT_BLACK);
    firstDrawAfterSS = false;
    graphNeedsUpdate = true;
    last_buf[0] = '\0';  // força redesenho do contador

    // CORREÇÃO: resetar flags aqui
    title_drawn = false;
    lastVolume = -1;
  }

  // 1. TÍTULO
  if (!title_drawn) {
    tft.setFreeFont(&Orbitron_Bold6pt7b);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("SYSTEM MONITOR v2", 0, 2);
    title_drawn = true;
  }

  // 2. CONTADOR CENTRAL (anti-flicker)
  tft.setFreeFont(&RobotoMono_VariableFont_wght12pt7b);
  char buf[12];
  sprintf(buf, "%lu", g_key_count);
  int16_t xPos = (160 - tft.textWidth(buf)) / 2;

  if (strcmp(buf, last_buf) != 0) {
    // Limpa apenas a área antiga
    tft.fillRect(last_xPos, 22, tft.textWidth(last_buf), 16, TFT_BLACK);

    // Desenha o novo valor
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(buf, xPos, 22);

    strcpy(last_buf, buf);
    last_xPos = xPos;
  }

  // 3. TEAMS MUTE ALERT
  static bool alert_active = false;
  if (millis() < teams_alert_timeout) {
    if (!alert_active) {
      tft.drawRect(20, 38, 120, 20, TFT_RED);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("TEAMS MUTE", 35, 43);
      alert_active = true;
    }
  } else if (alert_active) {
    // limpa só a área do alerta
    tft.fillRect(20, 38, 120, 22, TFT_BLACK);
    alert_active = false;
  }

  // 4. GRÁFICO (só redesenha se houve atualização)
  if (graphNeedsUpdate) {
    desenharGrafico();
    graphNeedsUpdate = false;
  }

  // 5. BARRA DE VOLUME (só redesenha se mudou)
  // 5. BARRA DE VOLUME
  if (lastVolume != g_volume || firstDrawAfterSS) {
    tft.setFreeFont(&Orbitron_Bold6pt7b);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("VOLUME", 84, 56);

    tft.drawRect(85, 70, 65, 8, TFT_WHITE);
    int barWidth = map(g_volume, 0, 100, 0, 61);
    tft.fillRect(87, 72, barWidth, 4, TFT_GREEN);
    tft.fillRect(87 + barWidth, 72, 61 - barWidth, 4, TFT_BLACK);

    lastVolume = g_volume;
  }

  // 6. Indicador de Night Mode (pisca suavemente)
  static bool nightVisible = false;
  if (is_dimmed) {
    bool blinkState = (millis() % 2000 < 1000);
    if (blinkState != nightVisible) {
      tft.fillCircle(150, 30, 3, blinkState ? TFT_YELLOW : TFT_BLACK);
      nightVisible = blinkState;
    }
  } else if (nightVisible) {
    tft.fillCircle(150, 30, 3, TFT_BLACK);
    nightVisible = false;
  }
}



/* ==================== SCREENSAVERS ==================== */
void init_matrix() {
  matrix_start_time = millis();
  for (int i = 0; i < MATRIX_COLS; i++) {
    drop_pos[i] = random(-20, 0);
    drop_speed[i] = random(1, 3);
  }
}

void update_matrix() {
  for (int i = 0; i < MATRIX_COLS; i++) {
    drop_pos[i] += drop_speed[i];
    if (drop_pos[i] > 90) {
      drop_pos[i] = -random(5, 20);
      drop_speed[i] = random(1, 3);
    }
  }
  if (millis() - matrix_start_time > 30000) is_screensaver = false;
}

// Buffer para guardar último estado
char matrix_last[MATRIX_COLS][8]; // até 8 caracteres por coluna

void draw_matrix() {
  tft.setTextFont(1);

  for (int i = 0; i < MATRIX_COLS; i++) {
    int x = (i * 16) + 4;

    // Cada coluna tem sua própria velocidade
    drop_pos[i] += drop_speed[i];
    if (drop_pos[i] > 90) {
      drop_pos[i] = -random(5, 20);
      drop_speed[i] = random(1, 3); // velocidade variável
    }

    // Desenha até 8 caracteres por coluna, com intervalos
    for (int t = 0; t < 8; t++) {
      int y = drop_pos[i] - (t * 10);

      if (y > 0 && y < 80) {
        // 60% de chance de desenhar um caractere (intervalos vazios)
        if (random(100) < 60) {
          char c[2] = { (char)random(33, 126), 0 };

          // Fade gradual: mais novo = branco, depois verde claro → verde médio → verde escuro
          uint16_t color;
          if (t == 0) color = TFT_WHITE;       // cabeça brilhante
          else if (t == 1) color = TFT_GREEN;  // verde claro
          else if (t == 2) color = 0x07E0;     // verde médio
          else color = TFT_DARKGREEN;          // verde escuro

          tft.setTextColor(color);
          tft.drawString(c, x, y);
        } else {
          // Espaço vazio → não desenha nada
        }
      }
    }
  }
}


void init_game() {
  for (int x = 0; x < GOL_W; x++)
    for (int y = 0; y < GOL_H; y++)
      world[x][y] = (random(100) < 35);
  for (int i = 0; i < 4; i++) history_checksum[i] = i;
  stable_count = 0;
}

void update_game() {
  uint32_t pop = 0, cksum = 0;
  for (int x = 0; x < GOL_W; x++) {
    for (int y = 0; y < GOL_H; y++) {
      int n = 0;
      for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++) {
          if (i || j) n += world[(x + i + GOL_W) % GOL_W][(y + j + GOL_H) % GOL_H];
        }
      next_world[x][y] = world[x][y] ? (n == 2 || n == 3) : (n == 3);
      if (next_world[x][y]) {
        pop++;
        cksum += (x * 31 + y * 7);
      }
    }
  }
  bool stuck = (pop == 0);
  for (int i = 0; i < 4; i++)
    if (cksum == history_checksum[i]) stuck = true;
  if (stuck) stable_count++;
  else stable_count = 0;
  if (stable_count > 15) is_screensaver = false;

  for (int i = 3; i > 0; i--) history_checksum[i] = history_checksum[i - 1];
  history_checksum[0] = cksum;
  memcpy(world, next_world, sizeof(world));
}

// Buffer adicional para comparar estados
uint8_t world_last[GOL_W][GOL_H];

void draw_game() {
  for (int x = 0; x < GOL_W; x++) {
    for (int y = 0; y < GOL_H; y++) {
      if (world[x][y] != world_last[x][y]) {
        if (world[x][y]) {
          // célula viva → desenha colorida
          uint16_t color = tft.color565((x * 4) % 255, (y * 8) % 255, (x * y) % 255);
          tft.fillRect(x * 2, y * 2, 2, 2, color);
        } else {
          // célula morta → limpa área
          tft.fillRect(x * 2, y * 2, 2, 2, TFT_BLACK);
        }
        world_last[x][y] = world[x][y]; // atualiza buffer
      }
    }
  }
}

/* ==================== HID ==================== */
void process_kbd_report(hid_keyboard_report_t const *report) {
  uint64_t new_pressed = 0;
  bool any_new_key = false;
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t key = report->keycode[i];
    if (key != 0) new_pressed |= (1ULL << key);
  }
  uint64_t newly_pressed = new_pressed & ~keys_currently_pressed;
  if (newly_pressed) {
    g_key_count++;
    any_new_key = true;
  }
  keys_currently_pressed = new_pressed;
  if (any_new_key) rp2040.fifo.push_nb(FIFO_DISPLAY_UPDATE);
}

void remap_key(hid_keyboard_report_t const *original, hid_keyboard_report_t *remapped) {
  memcpy(remapped, original, sizeof(hid_keyboard_report_t));
  bool altGr = (original->modifier & KEYBOARD_MODIFIER_RIGHTALT);
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t key = original->keycode[i];
    if (altGr && key == HID_KEY_R) {
      remapped->modifier &= ~KEYBOARD_MODIFIER_RIGHTALT;
      remapped->keycode[i] = 0x64;
    } else if (altGr && key == HID_KEY_M) {
      remapped->modifier &= ~KEYBOARD_MODIFIER_RIGHTALT;
      remapped->modifier |= (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTSHIFT);
      teams_alert_timeout = millis() + 1500;
      rp2040.fifo.push_nb(FIFO_DISPLAY_UPDATE);
    }
  }
}

// 1. Mantenha a função FORA do bloco extern "C"
void set_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
  if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize > 0 && dev_addr_keyboard != 0) {
    uint8_t led_state = buffer[0];

    // Teste 1: Pequeno atraso para o hardware processar
    delay(1);

    // Teste 2: Forçar ID 0 (como no código que funcionava)
    // Mesmo que o PC mande ID 1, o teclado físico geralmente espera ID 0 para LEDs
    tuh_hid_set_report(dev_addr_keyboard, instance_keyboard, 0, HID_REPORT_TYPE_OUTPUT, &led_state, 1);

    //Serial.print("Repassado para o teclado! (Forçado ID 0) Estado: ");
    //Serial.println(led_state);
  }
}

extern "C" {

  // Dentro do extern "C"
  void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    tuh_hid_receive_report(dev_addr, instance);

    // Se for protocolo de teclado, salvamos essa instância específica!
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
      dev_addr_keyboard = dev_addr;
      instance_keyboard = instance;  // <-- GARANTA QUE ISSO ESTÁ AQUI
      Serial.print("Teclado montado! Addr: ");
      Serial.print(dev_addr);
      Serial.print(" Inst: ");
      Serial.println(instance);
    }
  }

  void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    if (len == sizeof(hid_keyboard_report_t)) {
      hid_keyboard_report_t const *kbd_report = (hid_keyboard_report_t const *)report;
      process_kbd_report(kbd_report);
      hid_keyboard_report_t remapped;
      remap_key(kbd_report, &remapped);
      if (usb_hid.ready()) usb_hid.sendReport(1, &remapped, sizeof(hid_keyboard_report_t));
    } else if (len == 3 && report[0] == 0x03) {
      int delta = (report[1] == 0xE9) ? 2 : (report[1] == 0xEA) ? -2
                                                                : 0;
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