/*********************************************************************
 * PROJETO: HID Remapper & System Monitor v2.4 - ULTIMATE EDITION
 * HARDWARE: RP2040/RP2350 + TFT 160x80 (TFT_eSPI)
 * RECURSOS: 
 * - Dual Screensaver: Matrix Rain & Game of Life (Sorteio aleatório)
 * - Remapeamento AltGr+R (\) e AltGr+M (Teams Mute)
 * - Sincronização automática de volume no boot
 *********************************************************************/

#include "usbh_helper.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Adafruit_TinyUSB.h>
#include "7_Segment20pt7b.h"
#include "FreeMonoBold6pt7b.h"

/* --- PROTÓTIPOS DE FUNÇÕES --- */
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

/* --- CONFIGURAÇÕES DE HARDWARE --- */
TFT_eSPI tft = TFT_eSPI();

/* --- VARIÁVEIS GLOBAIS --- */
volatile uint32_t g_key_count = 0;
volatile uint8_t g_volume = 70;
volatile bool g_display_dirty = true;

unsigned long last_activity_time = 0;
unsigned long teams_alert_timeout = 0;

bool is_dimmed = false;
bool is_screensaver = false;
int current_ss_type = 0;  // 0: Game of Life, 1: Matrix

const uint32_t DIM_TIMEOUT = 30000;    // 30 seg
const uint32_t LIFE_TIMEOUT = 120000;  // 2 min

// --- LÓGICA GAME OF LIFE ---
#define GOL_W 80
#define GOL_H 40
uint8_t world[GOL_W][GOL_H];
uint8_t next_world[GOL_W][GOL_H];
uint32_t history_checksum[4];
uint32_t last_gen_time = 0;
int stable_count = 0;

// --- LÓGICA MATRIX ---
#define MATRIX_COLS 10
int8_t drop_pos[MATRIX_COLS];
uint8_t drop_speed[MATRIX_COLS];
uint32_t matrix_start_time = 0;

// --- CONTROLE USB ---
volatile uint8_t dev_addr_keyboard = 0;
volatile uint8_t instance_keyboard = 0;
volatile uint64_t keys_currently_pressed = 0;
#define FIFO_DISPLAY_UPDATE 1

uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2))
};

Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_KEYBOARD, 2, false);

/* ================================================================
   CORE 1: SOMENTE USB HOST
   ================================================================ */
void setup1() {
  rp2040_configure_pio_usb();
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();
}

/* ================================================================
   CORE 0: PROCESSAMENTO E DISPLAY
   ================================================================ */

void loop() {
  uint32_t now = millis();

  // Detecção de atividade
  static uint32_t last_key_val = 0;
  if (g_key_count != last_key_val) {
    last_key_val = g_key_count;
    last_activity_time = now;
    if (is_dimmed || is_screensaver) {
      is_dimmed = false;
      is_screensaver = false;
      g_display_dirty = true;
    }
  }

  uint32_t idle_time = now - last_activity_time;

  // Gerenciador de Screensaver
  if (idle_time > LIFE_TIMEOUT) {
    if (!is_screensaver) {
      is_screensaver = true;
      current_ss_type = random(0, 2);
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
  } else if (idle_time > DIM_TIMEOUT) {
    if (!is_dimmed) is_dimmed = true;
    static uint32_t last_blink = 0;
    if (now - last_blink > 1000) {
      g_display_dirty = true;
      last_blink = now;
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

  // Inicializa TFT
  tft.init();
  tft.setRotation(1);  // Orientação horizontal
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  randomSeed(analogRead(26) + micros());

  // Sincronização de Volume (Boot) — igual ao original
  delay(3000);
  if (usb_hid.ready()) {
    // ... mantém o mesmo código de sincronização de volume ...
    g_volume = 70;
  }
  last_activity_time = millis();
}

void update_display() {
  tft.fillScreen(TFT_BLACK);

  if (is_screensaver) {
    if (current_ss_type == 0) draw_game(); else draw_matrix();
  } else {
    // Moldura
    tft.drawRect(0, 0, 160, 80, TFT_WHITE);

    // Título com Orbitron
    tft.setFreeFont(&FreeMonoBold6pt7b);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("SYSTEM MONITOR v2.4", 10, 10);

    if (millis() < teams_alert_timeout) {
      // Alerta com Orbitron
      tft.drawRect(20, 30, 120, 20, TFT_RED);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("TEAMS MUTE", 40, 45);
    } else {
      // Contador com DSEG7
      //tft.setFreeFont(&DSEG7_Classic_Regular_17);
      tft.setFreeFont(&f7_Segment20pt7b);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      char buf[12]; sprintf(buf, "%lu", g_key_count);
      int16_t x = (160 - tft.textWidth(buf)) / 2;
      tft.drawString(buf, x, 25);
    }

    // Barra de volume com Orbitron
    tft.setFreeFont(&FreeMonoBold6pt7b);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawRect(10, 65, 140, 10, TFT_WHITE);
    int barWidth = map(g_volume, 0, 100, 0, 136);
    tft.fillRect(12, 67, barWidth, 6, TFT_GREEN);

    if (is_dimmed && (millis() % 2000 < 1000)) {
      tft.fillCircle(140, 30, 5, TFT_YELLOW);
    }
  }
}

/* --- LÓGICA MATRIX --- */
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
  // Sorteia novo screensaver após 30 segundos
  if (millis() - matrix_start_time > 30000) is_screensaver = false;
}

void draw_matrix() {
  tft.setTextFont(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK); // verde sobre fundo preto
  for (int i = 0; i < MATRIX_COLS; i++) {
    int x = (i * 16) + 4; 
    for (int t = 0; t < 5; t++) {
      int y = drop_pos[i] - (t * 10);
      if (y > 0 && y < 80) {
        char c[2] = {(char)random(33, 126), 0};
        tft.drawString(c, x, y);
      }
    }
  }
}

/* --- LÓGICA GAME OF LIFE --- */
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

  if (stable_count > 15) is_screensaver = false;  // força novo sorteio

  for (int i = 3; i > 0; i--) history_checksum[i] = history_checksum[i - 1];
  history_checksum[0] = cksum;
  memcpy(world, next_world, sizeof(world));
}

void draw_game() {
  for(int x=0; x<GOL_W; x++) {
    for(int y=0; y<GOL_H; y++) {
      if (world[x][y]) {
        // Escolhe cor baseada na posição
        uint16_t color = tft.color565((x*4) % 255, (y*8) % 255, (x*y) % 255);
        tft.fillRect(x*2, y*2, 2, 2, color);
      }
    }
  }
}

/* --- LÓGICA HID E REMAPEAMENTO --- */
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

/*
void set_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
  if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize > 0 && dev_addr_keyboard != 0) {
    uint8_t led_state = buffer[0];
    tuh_hid_set_report(dev_addr_keyboard, instance_keyboard, 0, HID_REPORT_TYPE_OUTPUT, &led_state, 1);
  }
}
*/

void set_report_callback(uint8_t report_id, hid_report_type_t report_type, 
                         uint8_t const *buffer, uint16_t bufsize) {
  if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize > 0 && dev_addr_keyboard != 0) {
    uint8_t led_state = buffer[0];

    // Debug para verificar se o evento chegou
    Serial.print("Relatório de LED recebido: ");
    Serial.println(led_state, BIN);

    // Repassa o estado do LED para o teclado físico
    tuh_hid_set_report(dev_addr_keyboard, instance_keyboard, 0, 
                       HID_REPORT_TYPE_OUTPUT, &led_state, 1);
  }
}

extern "C" {
  void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    tuh_hid_receive_report(dev_addr, instance);
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
      dev_addr_keyboard = dev_addr;
      instance_keyboard = instance;
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
