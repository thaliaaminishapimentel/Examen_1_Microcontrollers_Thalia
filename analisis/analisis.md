# Análisis Técnico y Diseño de la Solución
**Asignatura:** Microcontroladores / Algoritmos en C  
**Estudiante:** Thalía Aminisha Pimentel  
**Matrícula:** 20250531  
**Placa:** NXP FRDM-MCXA156  
**Reto:** Controlador de iluminación con consola serial, PWM por hardware y máquina de estados  

---

## 1. Tabla de Recursos de Hardware y Periféricos

| Función | Señal / Pin | Conector | Polaridad | Periférico / Canal | Reloj Efectivo | Prescaler / Registro Período | IRQ |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **LED de estado** | `P3_13` | Onboard RGB (Verde) | Activo en BAJO (Cátodo a pin) | `GPIO3` Pin 13 | System Clock (96 MHz) | N/A (GPIO digital) | N/A |
| **Pulsador usuario** | `P0_6` | Onboard `SW3` | Activo en BAJO (Pull-up interno) | `GPIO0` Pin 6 | System Clock (96 MHz) | N/A (Entrada con filtro digital) | `GPIO0_IRQn` (Prioridad 3) |
| **Salida PWM LED** | `P3_12` | `J1` pin 15 | Activo en ALTO (Ánodo a pin, cátodo a GND con 1 kΩ) | `CTIMER1` / Match 2 (`CT1_MAT2`, ALT4) y `GPIO3_12` (ALT0) | FRO_HF (48/96 MHz real por `CLOCK_GetCTimerClkFreq(1)`) | $PR = \frac{f_{clk}}{10^6} - 1$<br>$MR3 = \frac{10^6}{f_{pwm}} - 1$ | N/A |
| **Tick de 1 ms** | Interno | N/A | N/A | `CTIMER0` / Match 0 (`MR0`) | FRO_HF (48/96 MHz real por `CLOCK_GetCTimerClkFreq(0)`) | $PR = \frac{f_{clk}}{10^6} - 1$<br>$MR0 = 1000 - 1$ | `CTIMER0_IRQn` (Prioridad 2) |
| **Consola Serial** | `P0_2` (RX)<br>`P0_3` (TX) | MCU-Link USB (Virtual COM) | Lógica estándar UART | `LPUART0` | FRO12M (12 MHz) | Baudrate: 115200 bps<br>8 bits, No Paridad, 1 Stop | `LPUART0_IRQn` (Prioridad 3) |

> **Ausencia de conflictos de Pin Mux:**  
> - `P3_12` utiliza multiplexado ALT4 (`CT1_MAT2`) durante operación PWM y ALT0 (`GPIO3`) únicamente al forzar 0% o 100% de duty constante.  
> - `P3_13` utiliza multiplexado ALT0 (`GPIO3`) de forma fija para el LED de estado verde.  
> - `P0_6` utiliza multiplexado ALT0 (`GPIO0`) con pull-up para el pulsador `SW3`.  
> - `P0_2` y `P0_3` utilizan ALT2 (`LPUART0_RX` y `LPUART0_TX`) conectados al MCU-Link.  
> Cada función cuenta con pines, canales e interrupciones totalmente aislados sin solapamiento.

---

## 2. Diagrama de la Máquina de Estados Finito (FSM)

```
        +-------------------------------------------------------+
        |                                                       |
        v                                                       |
+-------------------+   Pulsación corta (30ms - 1500ms)    +-------------------+
|      MANUAL       | -----------------------------------> |       AUTO        |
| - PWM: Frec. fija | <----------------------------------- | - PWM: Rampa      |
|   (500/1000/2000) |    Pulsación corta / MODE MANUAL     |   10% a 90% (3.2s)|
| - Duty: 25% (def) |                                      | - LED: Parpadeo   |
| - LED: Fijo ON    |                                      |   cada 500 ms     |
+-------------------+                                      |   (1 Hz)          |
        |                                                  +-------------------+
        | Pulsación larga                                            | Pulsación larga
        | (>= 1500 ms)                                               | (>= 1500 ms)
        | o comando PAUSE                                            | o comando PAUSE
        v                                                            v
+------------------------------------------------------------------------------+
|                                    PAUSA                                     |
| - PWM: 0% continuo (salida forzada a 0V DC sin pulsos residuales)            |
| - LED: Parpadeo rápido cada 125 ms (4 Hz)                                    |
| - Memoria de Estado: Conserva modo previo (MANUAL o AUTO), duty manual       |
|   guardado, duty actual de la rampa, dirección y parámetros de frecuencia    |
| - Salida de Pausa: Pulsación larga o comando RESUME -> Estado Previo         |
|   (continúa rampa sin recuperar pasos perdidos del tiempo transcurrido)      |
| - Nota: Pulsación corta en PAUSA es estrictamente ignorada                   |
+------------------------------------------------------------------------------+
```

---

## 3. Temporización y Cálculos de Reloj Periférico

### 3.1. Cálculo Dinámico de Reloj y Prescaler (Sin Suposiciones)
El SDK permite inicializar el núcleo con `BOARD_BootClockFRO96M()`, dejando `FRO_HF` a 96 MHz o 48 MHz. Para evitar desvíos de frecuencia superiores al 2%, el firmware consulta el reloj real mediante:
$$f_{ctimer} = \text{CLOCK\_GetCTimerClkFreq}(id)$$

El prescaler se configura para lograr una base de tiempo universal de **1 MHz** (1 microsegundo por conteo):
$$PR = \frac{f_{ctimer}}{1\,000\,000} - 1$$
- Si $f_{ctimer} = 96\,\text{MHz} \implies PR = 95$.
- Si $f_{ctimer} = 48\,\text{MHz} \implies PR = 47$.

### 3.2. CTIMER0: Base de Tiempo Periférica de 1 ms
- Período: $MR0 = \frac{1\,000\,000\,\mu\text{s}}{1000\,\text{Hz}} - 1 = 999$.
- Registro de Control de Coincidencia (`MCR`): Bit 0 (`MR0I` = 1, interrupción) y Bit 1 (`MR0R` = 1, reinicio de contador TC).
- ISR `APP_TICK_IRQHandler`: Limpia el flag escribiendo en `CTIMER0->IR = CTIMER_IR_MR0INT_MASK` e incrementa la variable atómica de 32 bits `g_systemMs`.
- **Independencia Total:** CTIMER0 y CTIMER1 son bloques de silicio independientes; cambiar la frecuencia del PWM no afecta en absoluto el tick de 1 ms.

### 3.3. CTIMER1: Frecuencias PWM y Tratamiento de Extremos (0% y 100%)
Registros de período según frecuencia ($f_{pwm}$):
- **500 Hz:** $s\_pwmPeriodCounts = \frac{1\,000\,000}{500} = 2000$ conteos $\implies MR3 = 1999$.
- **1000 Hz:** $s\_pwmPeriodCounts = \frac{1\,000\,000}{1000} = 1000$ conteos $\implies MR3 = 999$.
- **2000 Hz:** $s\_pwmPeriodCounts = \frac{1\,000\,000}{2000} = 500$ conteos $\implies MR3 = 499$.
Error de frecuencia medido: **0.00%** (cumple $\le 2\%$).

**Tratamiento de 0% y 100% de Duty:**
Los temporizadores CTIMER basados en coincidencias estándar pueden emitir transiciones residuales de 1 ciclo de reloj en 0% o 100%. Para eliminar 100% de pulsos espurios:
1. **En Duty = 0%:** Se conmuta el pin `P3_12` a función GPIO (ALT0) forzando salida digital en nivel BAJO (0 V continuo).
2. **En Duty = 100%:** Se conmuta el pin `P3_12` a función GPIO (ALT0) forzando salida digital en nivel ALTO (3.3 V continuo).
3. **En Duty entre 1% y 99%:** Se conmuta a función `CT1_MAT2` (ALT4), programando el registro de coincidencia:
   $$MR2 = \frac{s\_pwmPeriodCounts \times (100 - \text{duty})}{100}$$

---

## 4. GPIO, Antirrebote y Medición Temporal
1. **ISR de Flanco Corta:**
   - La ISR `APP_BTN_IRQHandler` solo verifica el pin `P0_6`, borra el flag y levanta la variable `g_buttonEdgePending`. No ejecuta PRINTF ni bucles.
   - El consumo en `main` se realiza mediante `TakeButtonEdge()` con deshabilitación temporal de interrupciones globales.
2. **Filtro Antirrebote de 30 ms:**
   - En cada tick de 1 ms se lee el pin `P0_6`.
   - Cualquier variación resetea el contador `s_debounceCounter = 0`.
   - Únicamente si el nivel permanece constante por 30 ms continuos se valida la transición lógica.
3. **Clasificación y Conteo:**
   - **Pulsación Corta:** Validada si la duración en estado presionado fue $\ge 30\,\text{ms}$ y $< 1500\,\text{ms}$, ejecutándose al soltar el botón.
   - **Pulsación Larga:** Se ejecuta de forma inmediata al alcanzar $1500\,\text{ms}$ continuo. Al soltar el botón, no genera pulsación corta.
   - Mantener el botón presionado no genera repeticiones.
   - Se mantienen contadores independientes para eventos cortos y largos (`s_shortPressCount`, `s_longPressCount`).

---

## 5. Rampa Triangular en Modo AUTO
- Inicia en 10% y asciende en pasos de 1 punto porcentual cada 20 ms.
- Tramo de subida (10% a 90% = 80 pasos): $80 \times 20\,\text{ms} = 1600\,\text{ms} = 1.6\,\text{s}$.
- Tramo de bajada (90% a 10% = 80 pasos): $80 \times 20\,\text{ms} = 1600\,\text{ms} = 1.6\,\text{s}$.
- Período total del ciclo triangular: **3200 ms (3.2 s)**.
- En PAUSA se conservan el valor actual y el sentido de avance (`s_autoDuty`, `s_autoAscending`). Al enviar `RESUME`, se sincroniza `s_lastRampTimeMs = now` para continuar exactamente desde el punto pausado sin recuperar pasos perdidos.

---

## 6. Consola UART no Bloqueante y Protocolo
- **Búfer de Recepción:** Búfer circular de 128 bytes atendido por interrupción `LPUART0_IRQHandler`.
- **Búfer de Transmisión:** Búfer circular de 512 bytes atendido por interrupción con bandera `TDRE`, garantizando que la salida de telemetría periódica o respuestas no bloqueen el superloop.
- **Terminadores:** Reconoce `\r`, `\n` y `\r\n` produciendo una única ejecución por orden.
- **Protección ante Desbordamiento:** Si una línea supera 63 caracteres sin terminador, activa el descarte (`s_lineOverflowDiscard = true`), cuenta el error de desbordamiento y al recibir el fin de línea emite `ERR OVERFLOW\r\n`, resincronizando para procesar normalmente la siguiente orden.
- **Validación Estricta:** No utiliza `atoi`. Verifica carácter por carácter para rechazar `DUTY 20abc`, `DUTY -1`, `DUTY 101` y argumentos superfluos.

---

## 7. Invarianza ante Desbordamiento de Temporizador (`UINT32_MAX`)
Todas las comparaciones de temporización en el superloop emplean aritmética modular sin signo de 32 bits:
```c
if ((uint32_t)(now - lastService) >= INTERVAL_MS)
```
Cuando `g_systemMs` llega a `0xFFFFFFFF` y se incrementa a `0x00000000`, la diferencia `(uint32_t)(0x00000005 - 0xFFFFFFFB)` evalúa exactamente a `10`, asegurando continuidad total y sin congelamiento.
El comando `TEST OVERFLOW` permite inyectar `g_systemMs = 0xFFFFFFE0` para demostrar la transición en vivo.

---

## 8. Modelo de Concurrencia y Secciones Críticas
1. **Acceso Atómico a Banderas GPIO (`TakeButtonEdge`):**
   - La bandera `g_buttonEdgePending` se limpia y consume dentro de una sección crítica enmascarada con `DisableGlobalIRQ()` / `EnableGlobalIRQ()`, garantizando que ningún flanco quede sin registrar.
2. **Protección de Punteros en Búferes Circulares:**
   - En recepción, la extracción de caracteres en `Uart_GetRxByte` protege la comprobación de `s_rxHead` y el avance de `s_rxTail` mediante deshabilitación atómica de interrupciones para prevenir colisiones con la ISR de `LPUART0`.
   - En transmisión, la inserción de bytes en `Uart_SendString` y la activación del bit `TIE` en `LPUART0->CTRL` se ejecutan atómicamente.
3. **Desacoplamiento de Contadores de Error (Anti-RMW):**
   - El contador de desbordamiento del ring buffer (`s_rxRingOverflowCount`) es modificado exclusivamente en la ISR.
   - El contador de longitud de línea (`s_cmdLineOverflowCount`) es modificado exclusivamente en el hilo principal (`main`), eliminando cualquier conflicto de lectura-modificación-escritura (RMW).
4. **Planificación Equitativa (Fair Scheduling):**
   - `Uart_ProcessConsole` procesa un máximo acotado de caracteres (32 bytes o una orden) por ciclo del superloop, impidiendo que el tráfico serial monopolice el procesador y garantizando la atención prioritaria del tick de 1 ms.
