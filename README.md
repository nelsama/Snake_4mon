# 🐍 Snake_4mon

Un clásico juego de Snake implementado en **C89 estricto** para el procesador **6502** corriendo sobre el monitor para FPGA Tang Nano 9K, con efectos de sonido generados por el chip **SID 6581**. Diseñado para ejecutarse en la FPGA **Tang Nano 9K** con monitor ROM personalizado.

![Snake Gameplay](images/screen.png)

## ✨ Características

- 🎵 **Audio SID 6581 polifónico**: Jingle de introducción, efectos al comer comida normal/bonus, melodía de Game Over, sonido al subir de nivel y **melodía de fondo** que se acelera con la partida. Usa 2 voces simultáneas (voz 1 para fondo, voz 2 para efectos).

- 🎨 **Gráficos ANSI**: Paredes con **fondo cyan sólido**, cabeza direccional (`>` `<` `^` `v`) en amarillo, cuerpo `o` verde, comida `*` roja y comida bonus `*` magenta.

- 🎮 **Controles**: WASD o flechas ANSI para movimiento. **Tecla `P`** para pausar/reanudar. Menú post-partida con `P` jugar de nuevo o `E` salir al monitor.

- ⚡ **Dificultad progresiva**: Velocidad inicial lenta (300ms) que se acelera al comer (-12ms por comida, mínimo 80ms). El nivel sube cada 5 puntos.

- 🎁 **Comida bonus**: 1 de cada 8 comidas aparece en **magenta**. Da **+3 segmentos** en vez de +1. Expira tras ~20 frames si no se come. Tiene su propio efecto de sonido.

- ♻️ **Renderizado incremental**: Solo se redibujan las celdas que cambian (3-4 por frame), reduciendo el tráfico UART ~96%.

- 📊 **Puntuación a 3 dígitos** con nivel visible: `Puntaje: 42  Nv:9`. También se muestra al finalizar.

- 💾 **Uso eficiente de RAM**: ~745 bytes de RAM utilizados. Binario de ~7KB.

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
