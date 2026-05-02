/*********************************************************************
 * PROJETO: HID Remapper & System Monitor v2.4 - ULTIMATE EDITION
 * HARDWARE: RP2040/RP2350 + OLED 128x64 I2C (SSD1306)
 * RECURSOS: 
 * - Dual Screensaver: Matrix Rain & Game of Life (Sorteio aleatório)
 * - Remapeamento AltGr+R (\) e AltGr+M (Teams Mute)
 * - Sincronização automática de volume no boot
 *********************************************************************/

#include "usbh_helper.h"
#include <U8g2lib.h>
#include <Wire.h>

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
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

/* --- VARIÁVEIS GLOBAIS --- */
volatile uint32_t g_key_count = 0;
volatile uint8_t  g_volume = 70; 
volatile bool     g_display_dirty = true;

unsigned long last_volume_time = 0;
unsigned long last_activity_time = 0;
unsigned long teams_alert_timeout = 0;

bool is_dimmed = false;
bool is_screensaver = false;
int current_ss_type = 0; // 0: Game of Life, 1: Matrix

const uint32_t DIM_TIMEOUT = 30000;      // 30 seg
const uint32_t LIFE_TIMEOUT = 120000;    // 2 min

// --- LÓGICA GAME OF LIFE ---
#define GOL_W 64
#define GOL_H 32
uint8_t world[GOL_W][GOL_H];
uint8_t next_world[GOL_W][GOL_H];
uint32_t history_checksum[4];
uint32_t last_gen_time = 0;
int stable_count = 0;
uint32_t last_pop = 0;

// --- LÓGICA MATRIX ---
#define MATRIX_COLS 10
int8_t drop_pos[MATRIX_COLS];
uint8_t drop_speed[MATRIX_COLS];
uint32_t matrix_start_time = 0;

// --- CONTROLE USB ---
volatile uint8_t dev_addr_keyboard = 0;
volatile uint8_t instance_keyboard = 0;
volatile uint64_t keys_currently_pressed = 0;
#define FIFO_DISPLAY_UPDATE   1

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
  Wire.setSDA(4);
  Wire.setSCL(5);
  u8g2.begin();
  u8g2.setBusClock(800000UL);
  u8g2.setContrast(255);
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();
}

/* ================================================================
   CORE 0: PROCESSAMENTO E DISPLAY
   ================================================================ */
void setup() {
  usb_hid.setReportCallback(NULL, set_report_callback);
  usb_hid.begin();
  
  // Semente de aleatoriedade real
  randomSeed(analogRead(26) + micros());

  // Sincronização de Volume (Boot)
  delay(3000); 
  if (usb_hid.ready()) {
    for(int i=0; i<50; i++) {
      uint8_t vol_down[2] = {0xEA, 0x00}; 
      usb_hid.sendReport(2, vol_down, 2);
      delay(10);
      uint8_t release[2] = {0x00, 0x00};
      usb_hid.sendReport(2, release, 2);
      delay(10);
    }
    for(int i=0; i<35; i++) {
      uint8_t vol_up[2] = {0xE9, 0x00};
      usb_hid.sendReport(2, vol_up, 2);
      delay(10);
      uint8_t release[2] = {0x00, 0x00};
      usb_hid.sendReport(2, release, 2);
      delay(10);
    }
    g_volume = 70;
  }
  last_activity_time = millis();
}

void loop() {
  uint32_t now = millis();

  // Detecção de atividade
  static uint32_t last_key_val = 0;
  if (g_key_count != last_key_val) {
    last_key_val = g_key_count;
    last_activity_time = now;
    if (is_dimmed || is_screensaver) {
      u8g2.setContrast(255);
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
      current_ss_type = random(0, 2); // Sorteia: 0=Life, 1=Matrix
      if(current_ss_type == 0) init_game(); else init_matrix();
    }
    
    // Velocidade dos efeitos
    uint32_t speed = (current_ss_type == 0) ? 80 : 120; 
    if (now - last_gen_time > speed) {
      if (current_ss_type == 0) update_game(); else update_matrix();
      g_display_dirty = true;
      last_gen_time = now;
    }
  } 
  else if (idle_time > DIM_TIMEOUT) {
    if (!is_dimmed) {
      u8g2.setContrast(15);
      is_dimmed = true;
    }
    static uint32_t last_blink = 0;
    if (now - last_blink > 1000) { g_display_dirty = true; last_blink = now; }
  }

  // FIFO e Display
  while (rp2040.fifo.available()) {
    if (rp2040.fifo.pop() == FIFO_DISPLAY_UPDATE) g_display_dirty = true;
  }

  if (g_display_dirty) {
    update_display();
    g_display_dirty = false;
  }
}

/* --- RENDERIZAÇÃO --- */
void update_display() {
  u8g2.clearBuffer();

  if (is_screensaver) {
    if (current_ss_type == 0) draw_game(); else draw_matrix();
  } else {
    u8g2.drawRFrame(0, 0, 128, 64, 4);
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(11, 12, "SYSTEM MONITOR v2.4");
    u8g2.drawHLine(4, 17, 120);

    if (millis() < teams_alert_timeout) {
      u8g2.drawRFrame(18, 24, 92, 20, 3);
      u8g2.drawStr(29, 37, "TEAMS MUTE");
    } else {
      u8g2.setFont(u8g2_font_VCR_OSD_mn);
      char buf[12]; sprintf(buf, "%lu", g_key_count);
      u8g2.drawStr((128 - u8g2.getStrWidth(buf)) / 2, 48, buf);
    }

    u8g2.drawRFrame(8, 53, 112, 8, 2);
    u8g2.drawRBox(11, 55, map(g_volume, 0, 100, 0, 106), 4, 1);

    if (is_dimmed && (millis() % 2000 < 1000)) {
        u8g2.drawCircle(115, 27, 5);
        u8g2.setDrawColor(0);
        u8g2.drawCircle(111, 24, 5, U8G2_DRAW_ALL);
        u8g2.setDrawColor(1);
    }
  }
  u8g2.sendBuffer();
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
    if (drop_pos[i] > 75) { 
      drop_pos[i] = -random(5, 20); 
      drop_speed[i] = random(1, 3); 
    }
  }
  // Sorteia novo screensaver após 30 segundos
  if (millis() - matrix_start_time > 30000) is_screensaver = false;
}

void draw_matrix() {
  u8g2.setFont(u8g2_font_4x6_tf); 
  
  for (int i = 0; i < MATRIX_COLS; i++) {
    // Aumentamos o multiplicador para 12 para espalhar as colunas
    // O +4 é só um pequeno recuo da borda esquerda
    int x = (i * 12) + 4; 
    
    for (int t = 0; t < 5; t++) { 
      int y = drop_pos[i] - (t * 8);
      
      if (y > 0 && y < 64) {
        char c = (char)random(33, 126);
        u8g2.drawStr(x, y, &c);
      }
    }
    
    // Limpeza do rastro (ajustada para a nova largura da coluna)
    int y_cleanup = drop_pos[i] - (5 * 8); 
    if (y_cleanup > 0 && y_cleanup < 64) {
        u8g2.setDrawColor(0);
        u8g2.drawBox(x, y_cleanup - 6, 8, 8); // Box um pouco maior para garantir a limpeza
        u8g2.setDrawColor(1);
    }
  }
}

/* --- LÓGICA GAME OF LIFE --- */
void init_game() {
  for(int x=0; x<GOL_W; x++) for(int y=0; y<GOL_H; y++) world[x][y] = (random(100) < 35);
  for(int i=0; i<4; i++) history_checksum[i] = i;
  stable_count = 0;
}

void update_game() {
  uint32_t pop = 0, cksum = 0;
  for (int x = 0; x < GOL_W; x++) {
    for (int y = 0; y < GOL_H; y++) {
      int n = 0;
      for (int i = -1; i <= 1; i++) 
        for (int j = -1; j <= 1; j++) {
          if (i || j) n += world[(x+i+GOL_W)%GOL_W][(y+j+GOL_H)%GOL_H];
        }
      next_world[x][y] = world[x][y] ? (n==2||n==3) : (n==3);
      if (next_world[x][y]) { pop++; cksum += (x*31 + y*7); }
    }
  }
  bool stuck = (pop == 0);
  for(int i=0; i<4; i++) if (cksum == history_checksum[i]) stuck = true;
  if (stuck) stable_count++; else stable_count = 0;
  
  if (stable_count > 15) is_screensaver = false; // Força sorteio de novo SS
  
  for(int i=3; i>0; i--) history_checksum[i] = history_checksum[i-1];
  history_checksum[0] = cksum;
  memcpy(world, next_world, sizeof(world));
}

void draw_game() {
  for(int x=0; x<GOL_W; x++) 
    for(int y=0; y<GOL_H; y++) 
      if (world[x][y]) u8g2.drawBox(x*2, y*2, 2, 2);
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
    }
    else if (altGr && key == HID_KEY_M) {
      remapped->modifier &= ~KEYBOARD_MODIFIER_RIGHTALT;
      remapped->modifier |= (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTSHIFT);
      teams_alert_timeout = millis() + 1500;
      rp2040.fifo.push_nb(FIFO_DISPLAY_UPDATE);
    }
  }
}

void set_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
  if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize > 0 && dev_addr_keyboard != 0) {
    uint8_t led_state = buffer[0];
    tuh_hid_set_report(dev_addr_keyboard, instance_keyboard, 0, HID_REPORT_TYPE_OUTPUT, &led_state, 1);
  }
}

extern "C" {
  void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    tuh_hid_receive_report(dev_addr, instance);
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
      dev_addr_keyboard = dev_addr; instance_keyboard = instance;
    }
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
      uint8_t media_data[2] = {report[1], report[2]};
      if (usb_hid.ready()) usb_hid.sendReport(2, media_data, 2);
    }
    tuh_hid_receive_report(dev_addr, instance);
  }
}