# Parcial 1: Controlador de Iluminación con Consola Serial

- **Estudiante:** Thalía Aminisha Pimentel
- **Matrícula:** 20250531
- **Asignatura:** Microcontroladores (TMC-207) - Primer Parcial C3-2026
- **Placa de desarrollo:** NXP FRDM-MCXA156 (ARM Cortex-M33 @ 96 MHz)
- **Reto asignado:** Controlador de iluminación dinámico con modulación PWM por hardware, máquina de estados finita (FSM), antirrebote por interrupción GPIO y telemetría por consola serial LPUART.

---

## 1. Descripción del Proyecto

El sistema implementa un controlador embebido para la regulación y supervisión de una luminaria LED conectada al pin 15 del conector Arduino J1 (`P3_12` / `CT1_MAT2`), incorporando:
- **Base de tiempo de alta precisión:** Temporizador periódico de 1 ms (`CTIMER0`) para antirrebote y control temporal del sistema.
- **Salida PWM por hardware:** Modulación por ancho de pulsos (`CTIMER1`) a frecuencias de 500 Hz, 1000 Hz o 2000 Hz con ciclo de trabajo ajustable de 0% a 100%.
- **Clasificador de pulsaciones por interrupción:** Entrada digital con filtro antirrebote de 30 ms que discrimina pulsaciones cortas (< 1.5 s) y largas (≥ 1.5 s) usando los pulsadores `SW3` (`P0_6`) y `SW2` (`P1_7`).
- **Máquina de Estados Finitos (FSM):** Modos MANUAL, AUTO (rampa triangular suave de 10% a 90%) y PAUSA segura (salida a 0% continuo).
- **Consola serial interactiva:** Comunicación bidireccional LPUART a 115200 bps con búfer circular no bloqueante para comandos (`STATUS`, `MODE`, `DUTY`, `FREQ`, `PAUSE`, `RESUME`, `STREAM`).

---

## 2. Estructura del Repositorio

```
Microthalia/
├── .gitignore              # Reglas de exclusión de binarios y temporales
├── CMakeLists.txt          # Configuración de compilación con MCUXpresso SDK
├── CMakePresets.json       # Presets de CMake para debug y release
├── app_challenge.h         # Definiciones de hardware, pines, constantes y enums
├── led_blinky.c            # Código fuente principal de la aplicación
├── readme.md               # Documentación general y reporte de estado
├── analisis/
│   └── analisis.md         # Análisis técnico, mapa de periféricos y máquina de estados
├── pruebas/
│   └── README.md           # Plan de validación, casos de prueba y comandos
├── evidencias/
│   └── README.md           # Mediciones de osciloscopio, pruebas de escritorio y logs
└── frdmmcxa156/            # Archivos de soporte de placa y controladores de pines
    ├── board/              # Relojes, multiplexado de pines y periféricos
    └── led_blinky/         # Rutinas de inicialización de hardware de la placa
```

---

## 3. Compilación y Ejecución

### Requisitos
- Compilador: ARM GNU Toolchain (`arm-none-eabi-gcc` v14.2.rel1 o superior)
- Generador: Ninja v1.13+ y CMake v3.22+
- SDK: NXP MCUXpresso SDK para FRDM-MCXA156

### Compilación desde terminal (Git Bash / PowerShell)
```bash
# Configuración con preset Debug
cmake --preset debug -B debug

# Compilación del binario ejecutable
cmake --build debug
```

### Configuración del Puerto Serial
- Baudrate: **115200**
- Data bits: **8**
- Parity: **None**
- Stop bits: **1**
- Flow control: **None**

---

## 4. Estado Actual del Repositorio

- **Hito 1:** Setup inicial, mapa de pines base, reloj FRO96M, neutralización de traps de SysTick y esqueleto de consola serial. (Completado)
- **Hito 2:** Interrupciones GPIO y clasificador de pulsaciones con antirrebote (30 ms). (Completado)
- **Hito 3:** Base de tiempo CTIMER0 (1 ms) y modulación PWM por hardware con CTIMER1 (500 Hz / 1 kHz / 2 kHz, ciclo 0%-100%). (Completado)
- **Hito 4:** Máquina de estados FSM, rampa triangular y parser de comandos UART. (Completado)
- **Hito 5:** Sincronización de flags, concurrencia y optimización de secciones críticas. (Completado)
- **Hito 6:** Pruebas unitarias, evidencias de ejecución y documentación final. *(Próximo avance)*
