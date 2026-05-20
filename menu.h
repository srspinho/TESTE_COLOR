#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include <TFT_eSPI.h>

// --- PINOS DOS BOTÕES ---
#define BOTAO_UP     2
#define BOTAO_DOWN   3
#define BOTAO_SELECT 4
#define BOTAO_BACK   5

// --- ENUMS DO MENU ---
enum MenuLevel { LEVEL_MAIN, LEVEL_SUB_SS, LEVEL_SUB_CONFIG, LEVEL_SUB_PROD, LEVEL_SUB_SISTEMA };
enum ScreensaverType { SS_ATARI, SS_PONG, SS_ASTEROIDS, SS_STARFIELD, SS_GOL, SS_MATRIX, SS_TODOS };

// --- ESTRUTURA DE CONFIGURAÇÃO (Disponível para o projeto todo) ---
struct Config {
  uint8_t assinatura;
  uint8_t ss_selecionado;
  int vel_gradiente;
  int qtd_asteroides;
  int intensidade_cores;
  bool envio_ctrl_shift;
  int prod_intervalo;
  int prod_inicio;
  int sistema_brilho;
};

// Tornando a variável config visível em outros arquivos
extern Config config;
extern bool is_editing_value;
extern MenuLevel current_menu_level; // <-- ADICIONE ESTA LINHA
extern int current_item_index;       // <-- ADICIONE ESTA LINHA

// --- PROTÓTIPOS DAS FUNÇÕES ---
void setup_menu();
void tratar_botoes();
void desenhar_menu();
void carregar_configuracoes();
void salvar_configuracoes();

#endif