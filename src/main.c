/**
 * SNAKE 6502 + SID 6581 - C89 Estricto
 * Compatible con Monitor 6502 v2.2.0+
 * 
 * Caracteristicas:
 * - Paredes con fondo cyan solido (ANSI bg)
 * - Cabeza direccional > < ^ v segun movimiento
 * - Cuerpo o verde, comida * roja, bonus * magenta
 * - Dificultad progresiva (300ms a 80ms)
 * - Nivel sube cada 5 puntos
 * - Comida bonus: +3 segmentos, expira en ~20 frames
 * - Pausa con tecla P
 * - Renderizado incremental (~96% menos trafico UART)
 * - Melodia de fondo SID que se acelera con el juego
 * - Sonidos: comer, bonus, aceleracion, game over, intro
 * - Textos e interfaz en español
 */
#include <stdint.h>
#include "romapi.h"

/* ==========================================================================
   SECUENCIAS ANSI (Colores de Terminal)
   ========================================================================== */
#define A_RESET   "\033[0m"
#define A_WALL    "\033[36m"   /* Cyan texto */
#define A_WALL_BG "\033[46m"   /* Fondo Cyan */
#define A_HEAD    "\033[1;33m" /* Amarillo Brillante */
#define A_BODY    "\033[32m"   /* Verde */
#define A_FOOD    "\033[31m"   /* Rojo */
#define A_BONUS   "\033[1;35m" /* Magenta Brillante */
#define A_SCORE   "\033[37m"   /* Blanco */
#define A_TITLE   "\033[1;32m" /* Verde Brillante */
#define A_MENU    "\033[33m"   /* Amarillo */
#define A_FLASH   "\033[41;37m"/* Fondo rojo, texto blanco */

/* ==========================================================================
   CARACTERES GRÁFICOS (ASCII seguros para cualquier terminal)
   ========================================================================== */
#define G_WALL    ' '
#define G_WALL_BG /* nada, se usa A_WALL_BG */
#define G_HEAD_R  '>'
#define G_HEAD_L  '<'
#define G_HEAD_U  '^'
#define G_HEAD_D  'v'
#define G_BODY    'o'
#define G_FOOD    '*'

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
#define NOTE_E5 0x0F3A

/* Notas graves para la melodía de fondo */
#define NOTE_C3 0x0310
#define NOTE_G2 0x0248
#define NOTE_D3 0x0378
#define NOTE_A2 0x02B0

/* ==========================================================================
   CONFIGURACIÓN DEL JUEGO
   ========================================================================== */
#define WIDTH  20
#define HEIGHT 12
#define MAX_LEN ((WIDTH-2) * (HEIGHT-2))

/* ==========================================================================
   VELOCIDAD / DIFICULTAD PROGRESIVA
   ========================================================================== */
#define DELAY_START  300  /* ms - velocidad inicial (lenta) */
#define DELAY_MIN     80  /* ms - velocidad máxima */
#define DELAY_STEP    12  /* ms - reducción por comida */

/* ==========================================================================
   COMIDA BONUS
   ========================================================================== */
#define BONUS_CHANCE   8  /* 1 de cada 8 comidas es bonus */
#define BONUS_FRAMES  20  /* frames antes de que expire */
#define BONUS_EXTRA    3  /* segmentos extra que da */

/* Estado global */
static uint8_t grid[WIDTH * HEIGHT];
static uint8_t sx[MAX_LEN], sy[MAX_LEN];
static uint8_t len, fx, fy;
static int8_t  dx, dy;
static uint8_t running, seed;
static uint16_t delay_ms;          /* Delay entre frames (dificultad) */

/* Estado para renderizado incremental */
static uint8_t dirty_x[4], dirty_y[4], dirty_n;
static uint8_t dirty_score, first_draw;

/* Estado para comida bonus */
static uint8_t bonus_active;       /* 1 = hay comida bonus activa */
static uint8_t bonus_timer;        /* frames restantes de bonus */

/* Estado para pausa */
static uint8_t paused;

/* Estado para melodía de fondo */
static uint16_t bg_tick;           /* contador de frames musicales */

/* Prototipos */
static void sid_init(void);
static void sid_play_poly(uint16_t f1, uint16_t f2, uint8_t ms);
static void sid_play_jingle(const uint16_t *n1, const uint16_t *n2, const uint8_t *d, uint8_t count);
static void sid_off(void);
static void sid_bg_melody(void);
static void reset_game(void);
static uint8_t prand(void);
static void spawn_food(void);
static void gotoxy(uint8_t x, uint8_t y);
static void draw_cell(uint8_t x, uint8_t y);
static void draw_pause_overlay(void);
static void draw_flash_death(void);
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
    /* Voz 1: ADSR para melodia de fondo (sustain alto para que se oiga) */
    SID_AD_1 = 0x04;    /* Attack=0 (2ms), Decay=4 (48ms) */
    SID_SR_1 = 0x82;    /* Sustain=8 (medio), Release=2 (120ms) */
    /* Voz 2: ADSR para efectos (corto y seco) */
    SID_AD_2 = 0x02;    /* Attack=0 (2ms), Decay=2 (24ms) */
    SID_SR_2 = 0x01;    /* Sustain=0, Release=1 (24ms) */
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
   MELODÍA DE FONDO (Voz 1 - latido grave que se acelera)
   ========================================================================== */
static void sid_bg_melody(void) {
    static const uint16_t bg_notes[] = { NOTE_C3, NOTE_G2, NOTE_D3, NOTE_A2 };
    uint8_t rate, idx;
    
    /* La frecuencia del latido aumenta con la velocidad */
    rate = 3 + delay_ms / 60;  /* ~8 frames al inicio, ~4 al maximo */
    
    idx = (bg_tick / rate) & 3;
    
    /* Al inicio de cada nota: cargar frecuencia y activar Gate */
    if (bg_tick % rate == 0) {
        SID_FREQ_LO_1 = (uint8_t)bg_notes[idx];
        SID_FREQ_HI_1 = (uint8_t)(bg_notes[idx] >> 8);
        SID_CTRL_1 = 0x41;  /* Gate ON */
    }
    
    /* Apagar Gate justo antes de la siguiente nota */
    if (rate > 1 && bg_tick % rate == rate - 1) {
        SID_CTRL_1 = 0x40;  /* Gate OFF - inicia Release */
    }
    
    bg_tick++;
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

/* Efecto de aceleración (ascendente rápido) */
static const uint16_t speed_n1[] = { NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5 };
static const uint16_t speed_n2[] = { NOTE_E4, NOTE_G4, NOTE_C5, NOTE_E5 };
static const uint8_t  speed_d[]  = { 20, 20, 25, 35 };

/* Efecto de comida bonus */
static const uint16_t bonus_n1[] = { NOTE_C5, NOTE_E5, NOTE_C5 };
static const uint16_t bonus_n2[] = { NOTE_G4, NOTE_C5, NOTE_G4 };
static const uint8_t  bonus_d[]  = { 15, 20, 30 };

/* ==========================================================================
   LÓGICA DEL JUEGO
   ========================================================================== */
static void reset_game(void) {
    uint8_t i;
    len = 3; dx = 1; dy = 0; running = 1; seed = 123;
    paused = 0; bonus_active = 0; bonus_timer = 0;
    delay_ms = DELAY_START; bg_tick = 0;
    for (i = 0; i < WIDTH * HEIGHT; i++) grid[i] = 0;
    sx[0]=5; sy[0]=5; sx[1]=4; sy[1]=5; sx[2]=3; sy[2]=5;
    for (i = 0; i < len; i++) grid[sy[i]*WIDTH+sx[i]] = 1;
    spawn_food();
    first_draw = 1;
    dirty_score = 0;
}

static uint8_t prand(void) {
    seed = (seed * 137 + 17) % 251;
    return seed;
}

static void spawn_food(void) {
    uint8_t attempts = 0;
    bonus_active = 0;
    do {
        fx = 1 + prand() % (WIDTH - 2);
        fy = 1 + prand() % (HEIGHT - 2);
        attempts++;
    } while (grid[fy * WIDTH + fx] != 0 && attempts < 200);
    
    /* Decidir si es comida bonus */
    if (prand() % BONUS_CHANCE == 0) {
        grid[fy * WIDTH + fx] = 3;  /* 3 = comida bonus */
        bonus_active = 1;
        bonus_timer = BONUS_FRAMES;
    } else {
        grid[fy * WIDTH + fx] = 2;  /* 2 = comida normal */
    }
}

/* ==========================================================================
   RENDERIZADO EFICIENTE (Incremental)
   ========================================================================== */

/* Lookup tables para gotoxy - evita divisiones en 6502 */
/* pos10_tens[n-1] = dígito de decenas para posición n (0 = no imprimir) */
/* pos10_ones[n-1] = dígito de unidades para posición n */
static const uint8_t pos10_tens[] = { 0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,2 };
static const uint8_t pos10_ones[] = { 1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0 };

/* Posiciona el cursor ANSI en la celda de grid (x, y) */
static void gotoxy(uint8_t x, uint8_t y) {
    uint8_t n;
    rom_uart_putc(0x1B);
    rom_uart_putc('[');
    n = y + 1;  /* row: 1..HEIGHT */
    if (n > 9) rom_uart_putc('0' + pos10_tens[n - 1]);
    rom_uart_putc('0' + pos10_ones[n - 1]);
    rom_uart_putc(';');
    n = x + 1;  /* col: 1..WIDTH */
    if (n > 9) rom_uart_putc('0' + pos10_tens[n - 1]);
    rom_uart_putc('0' + pos10_ones[n - 1]);
    rom_uart_putc('H');
}

/* Retorna el carácter de cabeza según la dirección */
static uint8_t head_char(void) {
    if (dx == 1)  return G_HEAD_R;  /* > apunta derecha */
    if (dx == -1) return G_HEAD_L;  /* < apunta izquierda */
    if (dy == -1) return G_HEAD_U;  /* ^ apunta arriba */
    return G_HEAD_D;                 /* v apunta abajo */
}

/* Dibuja una celda individual en (x, y) usando posicionamiento */
static void draw_cell(uint8_t x, uint8_t y) {
    uint8_t c;
    gotoxy(x, y);
    if (y == 0 || y == HEIGHT-1 || x == 0 || x == WIDTH-1) {
        rom_uart_puts(A_WALL_BG);
        rom_uart_putc(' ');
    } else {
        c = grid[y * WIDTH + x];
        if (c == 2 || c == 3) {
            if (c == 3) { rom_uart_puts(A_BONUS); }
            else        { rom_uart_puts(A_FOOD); }
            rom_uart_putc(G_FOOD);
        } else if (c == 1) {
            if (x == sx[0] && y == sy[0]) {
                rom_uart_puts(A_HEAD);
                rom_uart_putc(head_char());
            } else {
                rom_uart_puts(A_BODY);
                rom_uart_putc(G_BODY);
            }
        } else {
            rom_uart_puts(A_RESET);
            rom_uart_putc(' ');
        }
    }
}

/* Dibujo completo del grid (solo al inicio de cada partida - secuencial) */
static void draw_full(void) {
    uint8_t y, x, c, s, level;
    rom_uart_puts("\033[H");
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            if (y == 0 || y == HEIGHT-1 || x == 0 || x == WIDTH-1) {
                rom_uart_puts(A_WALL_BG);
                rom_uart_putc(' ');
            } else {
                c = grid[y * WIDTH + x];
                if (c == 2 || c == 3) {
                    if (c == 3) { rom_uart_puts(A_BONUS); }
                    else        { rom_uart_puts(A_FOOD); }
                    rom_uart_putc(G_FOOD);
                } else if (c == 1) {
                    if (x == sx[0] && y == sy[0]) {
                        rom_uart_puts(A_HEAD);
                        rom_uart_putc(head_char());
                    } else {
                        rom_uart_puts(A_BODY);
                        rom_uart_putc(G_BODY);
                    }
                } else {
                    rom_uart_puts(A_RESET);
                    rom_uart_putc(' ');
                }
            }
        }
        rom_uart_puts(A_RESET "\033[K\r\n");
    }
    s = len - 3;
    level = s / 5 + 1;
    rom_uart_puts(A_SCORE "Puntaje: ");
    if (s >= 100) rom_uart_putc('0' + s / 100);
    if (s >= 10)  rom_uart_putc('0' + (s / 10) % 10);
    rom_uart_putc('0' + s % 10);
    rom_uart_puts("  Nv:");
    if (level >= 10) rom_uart_putc('0' + level / 10);
    rom_uart_putc('0' + level % 10);
    rom_uart_puts(A_RESET "\033[K\r\n");
}

/* Overlay de pausa sobre el grid */
static void draw_pause_overlay(void) {
    gotoxy(2, 5);
    rom_uart_puts(A_FLASH "  ** PAUSA **  " A_RESET);
    gotoxy(2, 6);
    rom_uart_puts(A_MENU "  [P] Seguir   " A_RESET);
}

/* Efecto flash al morir */
static void draw_flash_death(void) {
    uint8_t i;
    for (i = 0; i < 3; i++) {
        rom_uart_puts("\033[41m\033[2J\033[H");
        rom_uart_puts(A_FLASH "\r\n\r\n   *** FIN DEL JUEGO ***\r\n" A_RESET);
        rom_delay_ms(100);
        rom_uart_puts("\033[0m\033[2J\033[H");
        rom_delay_ms(100);
    }
}

/* Actualización incremental: solo redibuja las celdas sucias */
static void draw(void) {
    uint8_t i, s, level;
    
    if (first_draw) {
        first_draw = 0;
        draw_full();
        return;
    }
    
    /* Redibujar solo las celdas que cambiaron */
    for (i = 0; i < dirty_n; i++) {
        draw_cell(dirty_x[i], dirty_y[i]);
    }
    
    /* Score + nivel solo cuando cambia */
    if (dirty_score) {
        dirty_score = 0;
        s = len - 3;
        level = s / 5 + 1;
        gotoxy(1, HEIGHT);
        rom_uart_puts(A_SCORE "Puntaje: ");
        if (s >= 100) rom_uart_putc('0' + s / 100);
        if (s >= 10)  rom_uart_putc('0' + (s / 10) % 10);
        rom_uart_putc('0' + s % 10);
        rom_uart_puts("  Nv:");
        if (level >= 10) rom_uart_putc('0' + level / 10);
        rom_uart_putc('0' + level % 10);
        rom_uart_puts(A_RESET "\033[K");
    }
    
    /* Mostrar overlay de pausa si aplica */
    if (paused) {
        draw_pause_overlay();
    }
}

/* ==========================================================================
   ENTRADA (con pausa)
   ========================================================================== */
static void handle_input(void) {
    char c;
    if (!rom_uart_rx_ready()) return;
    c = rom_uart_getc();

    /* Tecla P: pausar/reanudar (siempre disponible) */
    if (c == 'p' || c == 'P') {
        paused = !paused;
        if (paused) {
            draw_pause_overlay();
        } else {
            /* Al reanudar, redibujar todo para limpiar el overlay */
            first_draw = 1;
            draw();
        }
        return;
    }
    
    /* Si está en pausa, ignorar otras teclas */
    if (paused) return;
    
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

/* ==========================================================================
   ACTUALIZACIÓN DEL ESTADO
   ========================================================================== */
static void update(void) {
    uint8_t hx, hy, tx, ty, i;
    uint8_t cell_type;
    uint8_t new_level;
    
    /* No actualizar si está en pausa */
    if (paused) return;
    
    /* Manejar temporizador de comida bonus */
    if (bonus_active) {
        if (bonus_timer > 0) {
            bonus_timer--;
            if (bonus_timer == 0) {
                /* Bonus expiró: respawnear y forzar redibujo completo */
                grid[fy * WIDTH + fx] = 0;
                bonus_active = 0;
                spawn_food();
                first_draw = 1;
            }
        }
    }
    
    hx = sx[0] + dx;
    hy = sy[0] + dy;

    /* Colisión con pared */
    if (hx == 0 || hx >= WIDTH-1 || hy == 0 || hy >= HEIGHT-1) { running = 0; return; }
    /* Colisión con cuerpo */
    if (grid[hy * WIDTH + hx] == 1) { running = 0; return; }

    tx = sx[len-1]; ty = sy[len-1];
    
    /* Registrar celdas sucias ANTES de modificar el estado */
    dirty_n = 0;
    dirty_x[dirty_n] = sx[0]; dirty_y[dirty_n] = sy[0]; dirty_n++; /* cabeza vieja → cuerpo */
    dirty_x[dirty_n] = hx;    dirty_y[dirty_n] = hy;    dirty_n++; /* cabeza nueva */
    
    /* Guardar tipo de celda donde va la cabeza */
    cell_type = grid[hy * WIDTH + hx];
    
    for (i = len-1; i > 0; i--) {
        sx[i] = sx[i-1]; sy[i] = sy[i-1];
    }
    sx[0] = hx; sy[0] = hy;
    grid[ty * WIDTH + tx] = 0;
    grid[hy * WIDTH + hx] = 1;

    /* Comer comida normal (2) o bonus (3) */
    if (cell_type == 2 || cell_type == 3) {
        uint8_t was_bonus = (cell_type == 3);
        
        if (was_bonus) {
            bonus_active = 0;
            len += BONUS_EXTRA;
            if (len > MAX_LEN) len = MAX_LEN;
            /* Extender serpiente con segmentos extra en la cola */
            for (i = 0; i < BONUS_EXTRA && len - 1 - i > 0; i++) {
                sx[len-1-i] = tx; sy[len-1-i] = ty;
                grid[ty * WIDTH + tx] = 1;
            }
            /* Sonido de bonus */
            sid_play_jingle(bonus_n1, bonus_n2, bonus_d, 3);
        } else {
            len++;
            sx[len-1] = tx; sy[len-1] = ty;
            grid[ty * WIDTH + tx] = 1;
            sid_play_poly(NOTE_C4, NOTE_E4, 25);
        }
        
        /* Acelerar juego */
        new_level = (DELAY_START - delay_ms) / DELAY_STEP;
        if (delay_ms > DELAY_MIN) delay_ms -= DELAY_STEP;
        
        /* Sonido de aceleración si se subió de nivel */
        if ((DELAY_START - delay_ms) / DELAY_STEP > new_level) {
            sid_play_jingle(speed_n1, speed_n2, speed_d, 4);
        }
        
        spawn_food();
        /* Marcar nueva comida como sucia */
        dirty_x[dirty_n] = fx; dirty_y[dirty_n] = fy; dirty_n++;
        dirty_score = 1;
    } else {
        /* Marcar cola vieja como sucia (se borró del grid) */
        dirty_x[dirty_n] = tx; dirty_y[dirty_n] = ty; dirty_n++;
    }
}

/* ==========================================================================
   PUNTO DE ENTRADA
   ========================================================================== */
int main(void) {
    char c;
    uint8_t s, level;
    
    sid_init();
    rom_uart_puts("\033[?25l\033[2J\033[H");
    
    /* PANTALLA DE INICIO CON INSTRUCCIONES */
    rom_uart_puts(A_TITLE "  SNAKE 6502 + SID 6581\r\n\r\n" A_RESET);
    rom_uart_puts(A_WALL "  CONTROLES:\r\n"
                  "    WASD / Flechas : Movimiento\r\n"
                  "    Evita paredes y tu cuerpo\r\n"
                  "    Come " A_FOOD "*" A_WALL " para crecer y sumar puntos\r\n"
                  "    [P] Pausa / Seguir\r\n"
                  "    " A_BONUS "*" A_WALL " Comida bonus = +3 puntos\r\n"
                  "    Velocidad aumenta con tu puntuacion\r\n\r\n" A_RESET);
    rom_uart_puts("  Presiona una tecla para empezar...\r\n");
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
            sid_bg_melody();       /* Melodía de fondo (voz 1) */
            handle_input();
            update();
            if (running) draw();
            rom_delay_ms(delay_ms);
        }
        
        /* Apagar melodía de fondo */
        SID_CTRL_1 = 0x00;
        
        /* Efecto flash de Game Over */
        draw_flash_death();

        first_draw = 1;

        /* Pantalla de Game Over limpia */
        rom_uart_puts("\033[2J\033[H");
        s = len - 3;
        level = s / 5 + 1;
        rom_uart_puts(A_FOOD "\r\n   *** FIN DEL JUEGO ***\r\n\r\n" A_RESET);
        rom_uart_puts(A_SCORE "   Puntaje Final: " A_HEAD);
        if (s >= 100) rom_uart_putc('0' + s / 100);
        if (s >= 10)  rom_uart_putc('0' + (s / 10) % 10);
        rom_uart_putc('0' + s % 10);
        rom_uart_puts(A_MENU "   Nivel: " A_HEAD);
        if (level >= 10) rom_uart_putc('0' + level / 10);
        rom_uart_putc('0' + level % 10);
        rom_uart_puts(A_RESET "\r\n\r\n");
        
        sid_play_jingle(go_n1, go_n2, go_d, 4);
        rom_delay_ms(100);
        
        rom_uart_puts(A_WALL "   [P] Jugar de nuevo\r\n"
                      "   [E] Salir al monitor\r\n" A_RESET);
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
                rom_uart_puts("\r\n   Saliendo al monitor...\r\n");
                rom_delay_ms(500);
                rom_uart_puts("\033[?25h\033[2J\033[H");
                return 0;
            }
        }
    }
}
