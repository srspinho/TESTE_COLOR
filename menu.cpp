#include "menu.h"
#include <EEPROM.h>

// Herda o canvas criado no programa principal para não duplicar memória
extern TFT_eSprite canvas; 

// Inicializa as variáveis globais do menu
Config config;
MenuLevel current_menu_level = LEVEL_MAIN;
int current_item_index = 0;
bool is_editing_value = false;
unsigned long last_debounce_time = 0;
const unsigned long DEBOUNCE_DELAY = 150;

// Itens dos Menus
#define MAIN_MENU_COUNT 4
const char* main_menu_items[MAIN_MENU_COUNT] = {"Screensaver", "Configuracoes", "Produtividade", "Sistema"};
#define SS_MENU_COUNT 7
const char* ss_menu_items[SS_MENU_COUNT] = {"Atari", "Pong", "Asteroids", "Star Field", "Game of Life", "Matrix", "Todos"};
#define CONFIG_MENU_COUNT 3
const char* config_menu_items[CONFIG_MENU_COUNT] = {"Veloc. Gradiente", "Qtd Asteroides", "Intensidade Cores"};
#define PROD_MENU_COUNT 3
const char* prod_menu_items[PROD_MENU_COUNT] = {"Envio CTRL+Shift", "Intervalo", "Inicio"};
#define SISTEMA_MENU_COUNT 2
const char* sistema_menu_items[SISTEMA_MENU_COUNT] = {"Brilho", "Reset"};

// Protótipos internos locais
void processar_acao(char botao);
void executar_selecao();
void ajustar_valor_variavel(int direcao);
void executar_reset_placa();

void setup_menu() {
  pinMode(BOTAO_UP, INPUT_PULLUP);
  pinMode(BOTAO_DOWN, INPUT_PULLUP);
  pinMode(BOTAO_SELECT, INPUT_PULLUP);
  pinMode(BOTAO_BACK, INPUT_PULLUP);
  carregar_configuracoes();
}

void carregar_configuracoes() {
  EEPROM.begin(512);
  Config temporario;
  EEPROM.get(0, temporario);
  
  if (temporario.assinatura == 0x5A) {
    config = temporario;
  } else {
    config.assinatura = 0x5A;
    config.ss_selecionado = SS_TODOS;
    config.vel_gradiente = 5;
    config.qtd_asteroides = 20;
    config.intensidade_cores = 80;
    config.envio_ctrl_shift = false;
    config.prod_intervalo = 10;
    config.prod_inicio = 5;
    config.sistema_brilho = 100;
    salvar_configuracoes();
  }
}

void salvar_configuracoes() {
  EEPROM.put(0, config);
  EEPROM.commit();
}

void tratar_botoes() {
  if ((millis() - last_debounce_time) < DEBOUNCE_DELAY) return;

  if (digitalRead(BOTAO_UP) == LOW) { processar_acao('U'); last_debounce_time = millis(); }
  else if (digitalRead(BOTAO_DOWN) == LOW) { processar_acao('D'); last_debounce_time = millis(); }
  else if (digitalRead(BOTAO_SELECT) == LOW) { processar_acao('S'); last_debounce_time = millis(); }
  else if (digitalRead(BOTAO_BACK) == LOW) { processar_acao('B'); last_debounce_time = millis(); }
}

void processar_acao(char botao) {
  if (is_editing_value) {
    if (botao == 'B' || botao == 'S') { is_editing_value = false; salvar_configuracoes(); } 
    else if (botao == 'U') ajustar_valor_variavel(1); 
    else if (botao == 'D') ajustar_valor_variavel(-1);
    desenhar_menu();
    return;
  }

  int limite_itens = 0;
  switch (current_menu_level) {
    case LEVEL_MAIN:        limite_itens = MAIN_MENU_COUNT; break;
    case LEVEL_SUB_SS:      limite_itens = SS_MENU_COUNT; break;
    case LEVEL_SUB_CONFIG:  limite_itens = CONFIG_MENU_COUNT; break;
    case LEVEL_SUB_PROD:    limite_itens = PROD_MENU_COUNT; break;
    case LEVEL_SUB_SISTEMA: limite_itens = SISTEMA_MENU_COUNT; break;
  }

  if (botao == 'U') { current_item_index = (current_item_index <= 0) ? limite_itens - 1 : current_item_index - 1; } 
  else if (botao == 'D') { current_item_index = (current_item_index >= limite_itens - 1) ? 0 : current_item_index + 1; } 
  else if (botao == 'B') { if (current_menu_level != LEVEL_MAIN) { current_menu_level = LEVEL_MAIN; current_item_index = 0; } } 
  else if (botao == 'S') { executar_selecao(); }

  desenhar_menu();
}

void executar_selecao() {
  switch (current_menu_level) {
    case LEVEL_MAIN:
      if (current_item_index == 0) current_menu_level = LEVEL_SUB_SS;
      else if (current_item_index == 1) current_menu_level = LEVEL_SUB_CONFIG;
      else if (current_item_index == 2) current_menu_level = LEVEL_SUB_PROD;
      else if (current_item_index == 3) current_menu_level = LEVEL_SUB_SISTEMA;
      current_item_index = 0;
      break;
    case LEVEL_SUB_SS: config.ss_selecionado = current_item_index; salvar_configuracoes(); break;
    case LEVEL_SUB_CONFIG: is_editing_value = true; break;
    case LEVEL_SUB_PROD:
      if (current_item_index == 0) { config.envio_ctrl_shift = !config.envio_ctrl_shift; salvar_configuracoes(); } 
      else is_editing_value = true;
      break;
    case LEVEL_SUB_SISTEMA: if (current_item_index == 0) is_editing_value = true; else if (current_item_index == 1) executar_reset_placa(); break;
  }
}

void ajustar_valor_variavel(int direcao) {
  if (current_menu_level == LEVEL_SUB_CONFIG) {
    if (current_item_index == 0) config.vel_gradiente = constrain(config.vel_gradiente + direcao, 1, 20);
    else if (current_item_index == 1) config.qtd_asteroides = constrain(config.qtd_asteroides + direcao, 0, 100);
    else if (current_item_index == 2) config.intensidade_cores = constrain(config.intensidade_cores + direcao, 0, 100);
  } else if (current_menu_level == LEVEL_SUB_PROD) {
    if (current_item_index == 1) config.prod_intervalo = constrain(config.prod_intervalo + direcao, 1, 60);
    else if (current_item_index == 2) config.prod_inicio = constrain(config.prod_inicio + direcao, 0, 60);
  } else if (current_menu_level == LEVEL_SUB_SISTEMA) {
    if (current_item_index == 0) config.sistema_brilho = constrain(config.sistema_brilho + (direcao * 10), 0, 100);
  }
}

void desenhar_menu() {
  canvas.fillSprite(TFT_BLACK);
  const char* titulo = "MENU";
  int total_itens = 0;
  const char** itens_ponteiro = NULL;

  switch (current_menu_level) {
    case LEVEL_MAIN:        titulo = "CONFIGURACOES"; total_itens = MAIN_MENU_COUNT;    itens_ponteiro = main_menu_items; break;
    case LEVEL_SUB_SS:      titulo = "SCREENSAVER";   total_itens = SS_MENU_COUNT;      itens_ponteiro = ss_menu_items; break;
    case LEVEL_SUB_CONFIG:  titulo = "AJUSTES";      total_itens = CONFIG_MENU_COUNT;  itens_ponteiro = config_menu_items; break;
    case LEVEL_SUB_PROD:    titulo = "PRODUTIVIDADE"; total_itens = PROD_MENU_COUNT;    itens_ponteiro = prod_menu_items; break;
    case LEVEL_SUB_SISTEMA: titulo = "SISTEMA";        total_itens = SISTEMA_MENU_COUNT; itens_ponteiro = sistema_menu_items; break;
  }

  canvas.setTextFont(1);
  canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
  canvas.drawString(titulo, 6, 2);
  canvas.drawFastHLine(0, 12, 160, TFT_DARKGREY);

  int item_inicial = (current_item_index >= 4) ? current_item_index - 3 : 0;
  int y_pos = 16;

  for (int i = item_inicial; i < min(item_inicial + 4, total_itens); i++) {
    bool is_selected = (i == current_item_index);
    if (is_selected) {
      canvas.fillRoundRect(2, y_pos - 2, 156, 13, 3, is_editing_value ? TFT_RED : TFT_BLUE);
      canvas.setTextColor(TFT_WHITE, is_editing_value ? TFT_RED : TFT_BLUE);
      canvas.drawString(">", 6, y_pos);
    } else {
      canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    }

    canvas.drawString(itens_ponteiro[i], 16, y_pos);
    char val_buf[10] = "";
    int x_box = 125; int w_box = 30;
    uint16_t cor_borda_box = is_selected ? TFT_WHITE : TFT_DARKGREY;

    if (current_menu_level == LEVEL_SUB_SS) {
      canvas.drawRoundRect(138, y_pos - 2, 16, 12, 2, cor_borda_box);
      if (config.ss_selecionado == i) canvas.fillCircle(145, y_pos + 3, 3, is_selected ? TFT_WHITE : TFT_YELLOW);
    } 
    else if (current_menu_level == LEVEL_SUB_CONFIG) {
      if (i == 0) sprintf(val_buf, "%d", config.vel_gradiente);
      else if (i == 1) sprintf(val_buf, "%d", config.qtd_asteroides);
      else if (i == 2) sprintf(val_buf, "%d", config.intensidade_cores);
      canvas.drawRoundRect(x_box, y_pos - 2, w_box, 12, 2, cor_borda_box);
      canvas.drawRightString(val_buf, 150, y_pos, 1);
    } 
    else if (current_menu_level == LEVEL_SUB_PROD) {
      if (i == 0) {
        canvas.drawRoundRect(138, y_pos - 2, 16, 12, 2, cor_borda_box);
        if (config.envio_ctrl_shift) canvas.fillCircle(145, y_pos + 3, 3, is_selected ? TFT_WHITE : TFT_YELLOW);
      } else {
        sprintf(val_buf, "%d", (i == 1) ? config.prod_intervalo : config.prod_inicio);
        canvas.drawRoundRect(x_box, y_pos - 2, w_box, 12, 2, cor_borda_box);
        canvas.drawRightString(val_buf, 150, y_pos, 1);
      }
    } 
    else if (current_menu_level == LEVEL_SUB_SISTEMA) {
      if (i == 0) {
        sprintf(val_buf, "%d%%", config.sistema_brilho);
        canvas.drawRoundRect(x_box - 5, y_pos - 2, w_box + 5, 12, 2, cor_borda_box);
        canvas.drawRightString(val_buf, 151, y_pos, 1);
      }
    }
    y_pos += 15;
  }
  canvas.pushSprite(0, 0);
}

void executar_reset_placa() {
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_RED);
  canvas.drawString("REINICIANDO...", 35, 35);
  canvas.pushSprite(0, 0);
  delay(500);
   scb_hw->aircr = 0x05FA0004;
}