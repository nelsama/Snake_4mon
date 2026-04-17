/**
 * SNAKE 6502 + SID 6581 Polifónico - C89 Estricto
 * Compatible con Monitor 6502 v2.2.0+
 * - Colores ANSI para terminal (paredes, serpiente, comida, UI)
 * - Jingles de introducción y Game Over
 * - Sonido ADSR corto, pantalla limpia
 */
#include <stdint.h>
#include "romapi.h"

/* ==========================================================================
   SECUENCIAS ANSI (Colores de Terminal)
   ========================================================================== */
#define A_RESET "\033[0m"
#define A_WALL  "\033[36m"   /* Cyan */
#define A_HEAD  "\033[1;33m" /* Amarillo Brillante */
#define A_BODY  "\033[32m"   /* Verde */
#define A_FOOD  "\033[31m"   /* Rojo */
#define A_SCORE "\033[37m"   /* Blanco */
#define A_TITLE "\033[1;32m" /* Verde Brillante */
#define A_MENU  "\033[33m"   /* Amarillo */

/* ==========================================================================
   REGISTROS SID 6581 (Voces 1 y 2)
   ========================================================================== */
#define SID_BASE 0xD400
#define SID_FREQ_LO_1 (*(volatile uint8_t *)(SID_BASE + 0))
#define SID_FREQ_HI_1 (*(volatile uint8_t *)(SID_BASE + 1))
#define SID_PW_LO_1   (*(volatile uint8_t *)(SID_BASE + 2))
#define SID_PW_HI_1   (*(volatile uint8_t *)(SID_BASE + 3))
#define SID_CTRL_1    (*(volatile uint8_t *)(SID_BASE + 4))
#define SID_AD_1      (*(volatile uint8_t *)(SID_BASE + 5))
#define SID_SR_1      (*(volatile uint8_t *)(SID_BASE + 6))

#define SID_FREQ_LO_2 (*(volatile uint8_t *)(SID_BASE + 7))
#define SID_FREQ_HI_2 (*(volatile uint8_t *)(SID_BASE + 8))
#define SID_PW_LO_2   (*(volatile uint8_t *)(SID_BASE + 9))
#define SID_PW_HI_2   (*(volatile uint8_t *)(SID_BASE + 10))
#define SID_CTRL_2    (*(volatile uint8_t *)(SID_BASE + 11))
#define SID_AD_2      (*(volatile uint8_t *)(SID_BASE + 12))
#define SID_SR_2      (*(volatile uint8_t *)(SID_BASE + 13))

#define SID_VOL       (*(volatile uint8_t *)(SID_BASE + 0x18))

/* Notas (aprox. para reloj ~1MHz) */
#define NOTE_E3 0x03D0
#define NOTE_G3 0x0490
#define NOTE_C4 0x0620
#define NOTE_E4 0x07BA
#define NOTE_G4 0x0930
#define NOTE_C5 0x0C44

/* ==========================================================================
   CONFIGURACIÓN DEL JUEGO
   ========================================================================== */
#define WIDTH  20
#define HEIGHT 12
#define MAX_LEN (WIDTH * HEIGHT)

/* Estado global */
static uint8_t grid[WIDTH * HEIGHT];
static uint8_t sx[MAX_LEN], sy[MAX_LEN];
static uint8_t len, fx, fy;
static int8_t  dx, dy;
static uint8_t running, seed;

/* Prototipos */
static void sid_init(void);
static void sid_play_poly(uint16_t f1, uint16_t f2, uint8_t ms);
static void sid_play_jingle(const uint16_t *n1, const uint16_t *n2, const uint8_t *d, uint8_t count);
static void sid_off(void);
static void reset_game(void);
static uint8_t prand(void);
static void spawn_food(void);
static void draw(void);
static void handle_input(void);
static void update(void);

/* ==========================================================================
   MOTOR DE SONIDO SID
   ========================================================================== */
static void sid_init(void) {
    SID_VOL = 0x0F;
    SID_PW_LO_1 = 0x00; SID_PW_HI_1 = 0x08;
    SID_PW_LO_2 = 0x00; SID_PW_HI_2 = 0x08;
    SID_AD_1 = 0x02; SID_SR_1 = 0x01;
    SID_AD_2 = 0x02; SID_SR_2 = 0x01;
}

static void sid_play_poly(uint16_t f1, uint16_t f2, uint8_t ms) {
    SID_FREQ_LO_1 = (uint8_t)f1;
    SID_FREQ_HI_1 = (uint8_t)(f1 >> 8);
    SID_FREQ_LO_2 = (uint8_t)f2;
    SID_FREQ_HI_2 = (uint8_t)(f2 >> 8);
    rom_delay_ms(2);
    SID_CTRL_1 = 0x41;
    SID_CTRL_2 = 0x41;
    rom_delay_ms(ms);
    SID_CTRL_1 = 0x40;
    SID_CTRL_2 = 0x40;
    rom_delay_ms(5);
}

static void sid_play_jingle(const uint16_t *n1, const uint16_t *n2, const uint8_t *d, uint8_t count) {
    uint8_t i;
    for (i = 0; i < count; i++) {
        sid_play_poly(n1[i], n2[i], d[i]);
    }
}

static void sid_off(void) {
    SID_CTRL_1 = 0x00;
    SID_CTRL_2 = 0x00;
    SID_VOL = 0x00;
}

/* ==========================================================================
   JINGLES
   ========================================================================== */
static const uint16_t intro_n1[] = { NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5, NOTE_C5 };
static const uint16_t intro_n2[] = { NOTE_G3, NOTE_C4, NOTE_E4, NOTE_G4, NOTE_E4 };
static const uint8_t  intro_d[]  = { 50, 50, 50, 70, 100 };

static const uint16_t go_n1[] = { NOTE_G4, NOTE_E4, NOTE_C4, NOTE_G3 };
static const uint16_t go_n2[] = { NOTE_E4, NOTE_C4, NOTE_G3, NOTE_E3 };
static const uint8_t  go_d[]  = { 80, 80, 100, 140 };

/* ==========================================================================
   LÓGICA DEL JUEGO
   ========================================================================== */
static void reset_game(void) {
    uint8_t i;
    len = 3; dx = 1; dy = 0; running = 1; seed = 123;
    for (i = 0; i < WIDTH * HEIGHT; i++) grid[i] = 0;
    sx[0]=5; sy[0]=5; sx[1]=4; sy[1]=5; sx[2]=3; sy[2]=5;
    for (i = 0; i < len; i++) grid[sy[i]*WIDTH+sx[i]] = 1;
    spawn_food();
}

static uint8_t prand(void) {
    seed = (seed * 137 + 17) % 251;
    return seed;
}

static void spawn_food(void) {
    uint8_t attempts = 0;
    do {
        fx = 1 + prand() % (WIDTH - 2);
        fy = 1 + prand() % (HEIGHT - 2);
        attempts++;
    } while (grid[fy * WIDTH + fx] != 0 && attempts < 50);
    grid[fy * WIDTH + fx] = 2;
}

static void draw(void) {
    uint8_t y, x, s, c;
    rom_uart_puts("\033[H"); /* Cursor al inicio */
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            if (y == 0 || y == HEIGHT-1 || x == 0 || x == WIDTH-1) {
                rom_uart_puts(A_WALL); rom_uart_putc('#');
            } else {
                c = grid[y * WIDTH + x];
                if (c == 2) { rom_uart_puts(A_FOOD); rom_uart_putc('*'); }
                else if (c == 1) {
                    if (x==sx[0] && y==sy[0]) { rom_uart_puts(A_HEAD); rom_uart_putc('@'); }
                    else { rom_uart_puts(A_BODY); rom_uart_putc('o'); }
                } else {
                    rom_uart_puts(A_RESET); rom_uart_putc(' ');
                }
            }
        }
        rom_uart_puts(A_RESET "\033[K\r\n"); /* Reset + Clear Line + NL */
    }
    rom_uart_puts(A_SCORE "Score: ");
    s = len - 3;
    rom_uart_putc('0' + s / 10);
    rom_uart_putc('0' + s % 10);
    rom_uart_puts(A_RESET "\033[K\r\n");
}

static void handle_input(void) {
    char c;
    if (!rom_uart_rx_ready()) return;
    c = rom_uart_getc();

    if (c == 'w' || c == 'W') { if (dy !=  1) { dx = 0; dy = -1; } }
    else if (c == 's' || c == 'S') { if (dy != -1) { dx = 0; dy =  1; } }
    else if (c == 'a' || c == 'A') { if (dx !=  1) { dx = -1; dy = 0; } }
    else if (c == 'd' || c == 'D') { if (dx != -1) { dx =  1; dy = 0; } }
    else if (c == 0x1B) {
        rom_delay_ms(15);
        if (rom_uart_rx_ready() && rom_uart_getc() == '[') {
            rom_delay_ms(15);
            if (!rom_uart_rx_ready()) return;
            c = rom_uart_getc();
            if (c == 'A' && dy !=  1) { dx = 0; dy = -1; }
            else if (c == 'B' && dy != -1) { dx = 0; dy =  1; }
            else if (c == 'D' && dx !=  1) { dx = -1; dy = 0; }
            else if (c == 'C' && dx != -1) { dx =  1; dy = 0; }
        }
    }
}

static void update(void) {
    uint8_t hx, hy, tx, ty, i;
    hx = sx[0] + dx;
    hy = sy[0] + dy;

    if (hx == 0 || hx >= WIDTH-1 || hy == 0 || hy >= HEIGHT-1) { running = 0; return; }
    if (grid[hy * WIDTH + hx] == 1) { running = 0; return; }

    tx = sx[len-1]; ty = sy[len-1];
    for (i = len-1; i > 0; i--) {
        sx[i] = sx[i-1]; sy[i] = sy[i-1];
    }
    sx[0] = hx; sy[0] = hy;
    grid[ty * WIDTH + tx] = 0;
    grid[hy * WIDTH + hx] = 1;

    if (hx == fx && hy == fy) {
        if (len < MAX_LEN) {
            len++;
            sx[len-1] = tx; sy[len-1] = ty;
            grid[ty * WIDTH + tx] = 1;
        }
        spawn_food();
        sid_play_poly(NOTE_C4, NOTE_E4, 25);
    }
}

/* ==========================================================================
   PUNTO DE ENTRADA
   ========================================================================== */
int main(void) {
    char c;
    uint8_t i, s;
    
    sid_init();
    rom_uart_puts("\033[?25l\033[2J\033[H");
    
    /* PANTALLA DE INICIO CON INSTRUCCIONES */
    rom_uart_puts(A_TITLE "  SNAKE 6502 + SID 6581\r\n\r\n" A_RESET);
    rom_uart_puts(A_WALL "  CONTROLS:\r\n"
                  "    WASD / Arrows : Move\r\n"
                  "    Avoid walls & yourself\r\n"
                  "    Eat * to grow & score\r\n\r\n" A_RESET);
    rom_uart_puts("  Press any key to start...\r\n");
    while (!rom_uart_rx_ready()); rom_uart_getc();
    rom_delay_ms(300);
    
    /* Limpiar completamente antes del juego */
    rom_uart_puts("\033[2J\033[H");
    
    /* Jingle de introducción */
    sid_play_jingle(intro_n1, intro_n2, intro_d, 5);
    rom_delay_ms(200);
    
    while (1) {
        reset_game();
        draw();

        /* Game Loop */
        while (running) {
            handle_input();
            update();
            if (running) draw();
            rom_delay_ms(180);
        }

        /* Pantalla de Game Over limpia */
        rom_uart_puts("\033[2J\033[H");
        rom_uart_puts(A_FOOD "\r\n   *** GAME OVER ***\r\n\r\n" A_RESET);
        s = len - 3;
        rom_uart_puts(A_MENU "   Final Score: " A_HEAD);
        rom_uart_putc('0' + s/10);
        rom_uart_putc('0' + s%10);
        rom_uart_puts(A_RESET "\r\n\r\n");
        
        sid_play_jingle(go_n1, go_n2, go_d, 4);
        rom_delay_ms(100);
        
        rom_uart_puts(A_WALL "   [P] Play Again\r\n"
                      "   [E] Exit to Monitor\r\n" A_RESET);
        rom_uart_puts(A_MENU "   > " A_RESET);
        
        while (1) {
            if (!rom_uart_rx_ready()) continue;
            c = rom_uart_getc();
            if (c == 'p' || c == 'P') {
                rom_uart_puts("\033[2J\033[H");
                break;
            }
            if (c == 'e' || c == 'E') {
                sid_off();
                rom_uart_puts("\r\n   Exiting to monitor...\r\n");
                rom_delay_ms(500);
                rom_uart_puts("\033[?25h\033[2J\033[H");
                return 0;
            }
        }
    }
}