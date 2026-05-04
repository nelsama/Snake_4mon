# 🐍 Snake_4mon

Un clásico juego de Snake implementado en **C89 estricto** para el procesador **6502** corriendo sobre el monitor para FPGA Tang Nano 9K, con efectos de sonido generados por el chip **SID 6581**. Diseñado para ejecutarse en la FPGA **Tang Nano 9K** con monitor ROM personalizado.

![Snake Gameplay](images/screen.png)

## ✨ Características

### 🎵 Audio SID 6581 multicanal
- **Voz 1**: Melodía de fondo continua (latido grave que se acelera con la velocidad del juego). Nunca se interrumpe.
- **Voz 2**: Efectos de sonido (comer comida, food bonus, aceleración). Suenan sin cortar el fondo.
- Jingles polifónicos en introducción y Game Over usando ambas voces.

### 🎨 Gráficos ANSI
- Paredes con **fondo cyan sólido** (bloques pintados)
- Cabeza **direccional**: `>` `<` `^` `v` según el movimiento
- Cuerpo `o` en verde, comida `*` roja, comida bonus `*` magenta

### 🎮 Controles
- **WASD** o **Flechas** para movimiento
- **Tecla `P`** para pausar/reanudar la partida
- Menú post-partida: `P` jugar de nuevo, `E` salir al monitor
- Textos e interfaz en español

### ⚡ Dificultad progresiva
- Velocidad inicial lenta (**300ms**) que se acelera al comer (**-12ms** por comida)
- Velocidad máxima: **80ms**
- **Nivel** sube cada 5 puntos: `Puntaje: 42  Nv:9`

### 🎁 Comida bonus
- 1 de cada 8 comidas aparece en **magenta**
- Da **+3 segmentos** en vez de +1
- Expira tras ~20 frames si no se come
- Tiene su propio efecto de sonido

### ♻️ Renderizado incremental
- Solo se redibujan las celdas que cambian (3-4 por frame)
- **~96% menos tráfico UART** vs redibujo completo

### 💾 Eficiencia
- ~745 bytes de RAM utilizados
- Binario de ~7KB (carga en $0800)
- Lookup tables para `gotoxy` (evita divisiones en 6502)

---

## 🛠️ Especificaciones Técnicas

### Hardware Objetivo
- **CPU**: MOS 6502 @ 3.375MHz
- **Audio**: SID 6581 mapeado en `$D400-$D41F`
- **Plataforma**: Tang Nano 9K FPGA con Monitor 6502 v2.2.0+
- **Display**: Terminal serie compatible con códigos ANSI

### Software
- **Compilador**: CC65 (`cl65`)
- **Lenguaje**: C89 estricto (ANSI C) + ensamblador para startup
- **Dependencias**: `romapi.h` (Jump Table en `$BF00` del monitor ROM)

## 📋 Requisitos

### Para Compilar
- [CC65](https://cc65.github.io/) compilador cruzado para 6502 instalado
- Make (en Windows: incluido con Git Bash, WSL o Make for Windows)
- Ruta de CC65 configurada en el `makefile`

### Para Ejecutar
- Tang Nano 9K con bitstream del Monitor 6502 v2.2.0+ cargado
- Terminal compatible con ANSI escape codes (PuTTY, TeraTerm, Linux console, macOS Terminal)
- Conexión serial configurada (115200 baudios recomendado)
- SD Card formateada en FAT16/32 (opcional, también soporta XMODEM)

## 🚀 Instalación y Compilación

1. **Clonar el repositorio**:
```bash
git clone https://github.com/nelsama/Snake_4mon.git
cd Snake_4mon
```

2. **Compilar**:
```bash
make
```

3. **Copiar a SD** como `SNAKE.BIN`

4. **En el monitor**:
```
LOAD SNAKE.BIN 0800
R 0800
```
