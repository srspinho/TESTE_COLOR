#include <TFT_eSPI.h>
#include <SPI.h>
#include "usbh_helper.h"
#include "7seg20.h"
#include "ani.h"
#include "Orbitron_Medium_16.h"

/* --- HARDWARE --- */
TFT_eSPI tft = TFT_eSPI();
#define BTN_INV 10
#define BTN_BRIGHT 11
#define TFT_BL_PIN 8

/* --- GLOBAIS --- */
volatile uint32_t g_key_count = 0;
int c = 1;
int frame = 0;
int bright[] = { 20, 60, 120, 180, 255 };

bool volume_initialized = false;
uint32_t init_timer = 0;

volatile uint8_t dev_addr_keyboard = 0;
volatile uint64_t keys_currently_pressed = 0;

/* --- DESCRITOR USB --- */
uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2))
};
Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_KEYBOARD, 2, false);

/* --- CORE 1: USB HOST (TECLADO EXTERNO) --- */
void setup1() {
  rp2040_configure_pio_usb();
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();
}

/* --- CORE 0: INTERFACE E DEVICE (PC) --- */
void setup() {
  Serial.begin(115200);

  // 1. Inicializa Vídeo
  pinMode(TFT_BL_PIN, OUTPUT);
  tft.init();
  tft.setRotation(3);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);
  analogWrite(TFT_BL_PIN, bright[c]);

  // 2. Inicializa USB Device
  usb_hid.begin();
  
  // 3. AGUARDO CRÍTICO (Igual ao código estável)
  // Dá tempo para o driver HID enumerar no Windows
  delay(4000); 

  pinMode(BTN_INV, INPUT_PULLUP);
  pinMode(BTN_BRIGHT, INPUT_PULLUP);

  tft.setTextColor(0xD340, TFT_BLACK);
  tft.setFreeFont(&Orbitron_Medium_16);
  tft.drawString("HID MONITOR", 6, 1);

  update_ui();
}

void loop() {
  // Mantém a pilha USB Device viva
  tud_task(); 

  // 1. Inicialização do Volume (Lógica sincronizada)
  if (!volume_initialized && usb_hid.ready()) {
    Serial.println("USB Ready! Sincronizando volume...");
    for (int i = 0; i < 50; i++) {
      uint8_t vol_down[2] = { 0xEA, 0x00 };
      usb_hid.sendReport(2, vol_down, 2);
      delay(10);
      uint8_t release[2] = { 0x00, 0x00 };
      usb_hid.sendReport(2, release, 2);
      delay(10);
    }
    volume_initialized = true;
    update_ui();
  }

  // 2. Processamento de Teclas via FIFO
  while (rp2040.fifo.available() >= 3) {
    uint32_t signal = rp2040.fifo.pop();
    if (signal == 0xDEADBEEF) {
      uint32_t p1 = rp2040.fifo.pop();
      uint32_t p2 = rp2040.fifo.pop();

      hid_keyboard_report_t to_pc;
      memcpy(((uint8_t *)&to_pc), &p1, 4);
      memcpy(((uint8_t *)&to_pc) + 4, &p2, 4);

      if (usb_hid.ready()) {
        usb_hid.sendReport(1, &to_pc, sizeof(hid_keyboard_report_t));
      }
      update_ui();
    }
  }

  // 3. Animação (Frequência controlada)
  static uint32_t last_anim = 0;
  if (millis() - last_anim > 40) {
    tft.pushImage(112, 19, 40, 40, ani[frame]);
    frame = (frame + 1) % 151;
    last_anim = millis();
  }

  check_local_buttons();
}

void update_ui() {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(&DSEG7_Classic_Bold_30);
  char buf[12];
  sprintf(buf, "%lu", g_key_count);
  tft.fillRect(2, 40, 100, 35, TFT_BLACK);
  tft.drawString(buf, 2, 46);

  // Barras de Brilho
  for (int i = 0; i < c + 1; i++) {
    tft.fillRect(146, 77 - (i * 4), 8, 2, 0x0614);
  }

  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  if (dev_addr_keyboard != 0) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("KBD OK", 6, 25);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("NO KBD", 6, 25);
  }
}

void check_local_buttons() {
  static uint32_t last_btn = 0;
  if (millis() - last_btn < 200) return;

  if (digitalRead(BTN_INV) == LOW) {
    tft.invertDisplay(true);
    last_btn = millis();
  }
  if (digitalRead(BTN_BRIGHT) == LOW) {
    c = (c + 1) % 5;
    analogWrite(TFT_BL_PIN, bright[c]);
    tft.fillRect(146, 60, 8, 20, TFT_BLACK);
    update_ui();
    last_btn = millis();
  }
}

void remap_key(hid_keyboard_report_t const *original, hid_keyboard_report_t *remapped) {
  memcpy(remapped, original, sizeof(hid_keyboard_report_t));
  bool altGr = (original->modifier & KEYBOARD_MODIFIER_RIGHTALT);
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t key = original->keycode[i];
    if (altGr && key == HID_KEY_R) {
      remapped->modifier &= ~KEYBOARD_MODIFIER_RIGHTALT;
      remapped->keycode[i] = 0x64; // Backslash
    }
  }
}

extern "C" {
  void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    dev_addr_keyboard = dev_addr;
    tuh_hid_receive_report(dev_addr, instance);
  }

  void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    if (len == sizeof(hid_keyboard_report_t)) {
      hid_keyboard_report_t const *kbd_report = (hid_keyboard_report_t const *)report;

      // Contador de Teclas
      uint64_t current_keys = 0;
      for (uint8_t i = 0; i < 6; i++) {
        if (kbd_report->keycode[i] != 0) current_keys |= (1ULL << kbd_report->keycode[i]);
      }
      if (current_keys > keys_currently_pressed) g_key_count++;
      keys_currently_pressed = current_keys;

      // Remapeamento
      hid_keyboard_report_t remapped;
      remap_key(kbd_report, &remapped);

      uint32_t p1, p2;
      memcpy(&p1, ((uint8_t *)&remapped), 4);
      memcpy(&p2, ((uint8_t *)&remapped) + 4, 4);

      rp2040.fifo.push_nb(0xDEADBEEF);
      rp2040.fifo.push_nb(p1);
      rp2040.fifo.push_nb(p2);
    }
    tuh_hid_receive_report(dev_addr, instance);
  }
}