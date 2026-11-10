#ifndef PESTINHA_UI_H
#define PESTINHA_UI_H

/* Interface do modo desktop: menu de clique direito, tela de privacidade e
   memoria, aviso de primeira execucao e atalho global de panico (Ctrl+Alt+P).
   Desenhada por software com a fonte embutida (nao depende de fontes do X). */
typedef enum {
    UI_NONE = 0,
    UI_CARESS, UI_POKE, UI_WORK, UI_DISMISS, UI_CORNER, UI_VISIT, UI_RELEASE,
    UI_MISCHIEF, UI_THROW,
    UI_BALL, UI_PLATFORMS, UI_SENSORS, UI_SIZE, UI_CLEAR_FX,
    UI_SENSORS_ON, UI_SENSORS_OFF, UI_FORGET,
    UI_HIDE, UI_QUIT
} UiAction;

typedef struct { int sensors, ball, platforms, size; } UiState;   /* size: 0 pequeno, 1 medio, 2 grande */

typedef struct Ui Ui;

Ui *ui_open(void);
void ui_close(Ui *ui);
/* Tenta registrar Ctrl+Alt+P na tela inteira. Devolve 1 se conseguiu. */
int ui_hotkey(Ui *ui);
void ui_set_state(Ui *ui, const UiState *state);
void ui_menu_open(Ui *ui, int x, int y);
/* first_run = 1: aviso de boas-vindas (ligar sensores / agora nao). */
void ui_privacy_open(Ui *ui, int first_run);
int ui_busy(Ui *ui);                       /* menu ou janela aberta */
/* Processa eventos; devolve 1 e a acao quando o usuario escolheu algo. */
int ui_poll(Ui *ui, UiAction *action);
void ui_raise(Ui *ui);

#endif
