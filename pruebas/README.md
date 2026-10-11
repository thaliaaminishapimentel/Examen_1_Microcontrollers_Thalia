# Plan de Validación y Casos de Prueba (Hito 6)

- **Asignatura:** Microcontroladores (TMC-207)
- **Estudiante:** Thalía Aminisha Pimentel (Matrícula: 20250531)
- **Placa:** NXP FRDM-MCXA156 (ARM Cortex-M33 @ 96 MHz)
- **Proyecto:** Controlador de Iluminación con Consola Serial, PWM y FSM

Este documento detalla los 13 casos de prueba oficiales exigidos en la rúbrica del examen parcial, las entradas requeridas, los resultados esperados y el procedimiento de verificación sobre el hardware.

---

## 1. Matriz de Casos de Prueba de Aceptación

| ID | Caso de Prueba | Entrada / Acción | Resultado Esperado | Criterio de Éxito | Estado |
| :---: | :--- | :--- | :--- | :--- | :---: |
| **TC-01** | **Encendido inicial** | Reset físico de la placa (botón RESET) | Banner impreso en UART (115200 bps). Modo MANUAL, 1000 Hz, Duty 25%, STREAM OFF. LED verde fijo ON. | Estado inicial exacto según especificación | **PASÓ** |
| **TC-02** | **Niveles PWM Extremos y 50%** | Comandos `DUTY 0`, `DUTY 50`, `DUTY 100` en MANUAL | `DUTY 0`: nivel bajo continuo (0.0 V DC, sin pulsos residuales).<br>`DUTY 50`: onda cuadrada 50% duty.<br>`DUTY 100`: nivel alto continuo (3.3 V DC, sin caídas). | Tolerancia de duty $\pm 2\%$. Sin pulsos espurios en 0% y 100%. | **PASÓ** |
| **TC-03** | **Frecuencias PWM y desacoplamiento de tick** | Comandos `FREQ 500`, `FREQ 1000`, `FREQ 2000` | CTIMER1 conmuta a 500 Hz, 1000 Hz y 2000 Hz. Error de frecuencia $\le 2\%$. El tick de 1 ms (CTIMER0) no sufre alteración. | Frecuencias exactas. Tick inmutable de 1 ms. | **PASÓ** |
| **TC-04** | **Rampa triangular en modo AUTO** | Comando `MODE AUTO` o pulsación corta de `SW3` | Rampa lineal de brillo de 10% a 90% (subida 1.6 s, bajada 1.6 s, período total 3.2 s). LED verde parpadea a 1 Hz (500 ms ON / 500 ms OFF). | Ciclo completo de 3.2 s exactos (pasos de 1% cada 20 ms). | **PASÓ** |
| **TC-05** | **Discriminación de 20 pulsaciones cortas** | 20 pulsaciones de `SW3`, cada una entre 30 ms y 1400 ms | Se alternan modos MANUAL $\leftrightarrow$ AUTO exactamente 20 veces. Contador `BtnShort` incrementa a 20. Cero duplicados por rebote. | Antirrebote de 30 ms filtra 100% de transitorios mecánicos. | **PASÓ** |
| **TC-06** | **Pulsación larga sostenida (3 s)** | Mantener presionado `SW3` por 3.0 s continuos | Se dispara evento largo exactamente a los 1500 ms pasando a PAUSA. Al soltar tras 3 s, NO se genera pulsación corta espuria. | Una sola ejecución de evento largo. Cero repeticiones ni eventos fantasma. | **PASÓ** |
| **TC-07** | **Entrada y salida de PAUSA (memoria de estado)** | Pulsación larga en AUTO, esperar 5 s, luego pulsación larga (o `RESUME`) | En PAUSA: PWM a 0 V DC, LED verde parpadea a 4 Hz (125 ms). Al reanudar: continúa rampa desde el porcentaje y sentido exactos sin recuperar pasos perdidos. | Memoria de estado no volátil lógica. Rampa reanuda sin salto temporal. | **PASÓ** |
| **TC-08** | **Validación numérica estricta del parser** | Comandos `DUTY 20abc`, `DUTY -1`, `DUTY 101`, `DUTY` | Comandos rechazados con `ERR ARG` o `ERR RANGE`. El duty actual no sufre alteración. | Rechazo robusto sin uso de `atoi` ingenuo. | **PASÓ** |
| **TC-09** | **Desbordamiento de línea (> 63 caracteres)** | Enviar cadena de 100 caracteres seguida de `\r\n`, luego `STATUS` | Responde `ERR OVERFLOW`. El buffer se vacía y el siguiente comando `STATUS` se procesa normalmente. | Resincronización inmediata sin bloqueo ni corrupción de memoria. | **PASÓ** |
| **TC-10** | **Terminadores de línea CR, LF y CRLF** | Enviar `STATUS\r`, `STATUS\n`, `STATUS\r\n` | Cada variante ejecuta la orden exactamente una sola vez. No se generan respuestas dobles con CRLF. | Compatibilidad universal con minicom, PuTTY, RealTerm, VS Code Serial. | **PASÓ** |
| **TC-11** | **Concurrencia bajo carga (STREAM + 10 cmds/s)** | Activar `STREAM ON` e inyectar ráfaga continua de 10 órdenes/s por 30 s | El sistema mantiene telemetría periódica, responde a todas las órdenes y responde a botones sin retrasos ni reinicios. | Superloop no bloqueante y búfer circular TX de 512 bytes sin congelamiento. | **PASÓ** |
| **TC-12** | **Desbordamiento de búfer RX provocado** | Inyección masiva de bytes (> 128 bytes sin leer) | Se incrementa contador de errores de desbordamiento (`RxErr`). Los bytes excedentes se descartan sin corromper la cola. | Recuperación limpia en el siguiente fin de línea. | **PASÓ** |
| **TC-13** | **Invarianza ante desbordamiento de 32 bits** | Comando `TEST OVERFLOW` (inyecta $2^{32} - 32$) | Temporizador pasa de `0xFFFFFFFF` a `0x00000000`. `Uptime` continúa, la rampa y los parpadeos no se detienen. | Aritmética modular sin signo inmune a rollovers temporales. | **PASÓ** |

---

## 2. Guía de Ejecución de Comandos de Prueba

### TC-01: Encendido Inicial
```text
Comando: [Presionar botón RESET de la tarjeta]
Salida esperada:
========================================
FRDM-MCXA156: Controlador de Iluminacion
Estudiante: Thalia Pimentel (20250531)
Hito 5: Concurrencia y Secciones Criticas
Modo inicial: MANUAL | 1000 Hz | 25%
Escriba HELP para ver la lista de comandos
========================================
```

### TC-02: Niveles PWM Extremos
```text
Entrada: DUTY 0
Salida:  OK
Medición: 0.00 V continuo en pin P3_12 (J1 pin 15), LED apagado.

Entrada: DUTY 50
Salida:  OK
Medición: Onda periódica con Ton = 500 us, Toff = 500 us (1 kHz).

Entrada: DUTY 100
Salida:  OK
Medición: 3.30 V continuo en pin P3_12 (J1 pin 15), LED a brillo máximo.
```

### TC-03: Frecuencias PWM y Desacoplamiento de Tick
```text
Entrada: FREQ 500
Salida:  OK
Medición: Período PWM = 2.00 ms (500 Hz).

Entrada: FREQ 2000
Salida:  OK
Medición: Período PWM = 500 us (2.00 kHz).

Entrada: STATUS
Salida:  STATUS: Modo=MANUAL, Freq=2000Hz, Duty=100%, SavedDuty=100%, Uptime=..., ...
Verificación: Uptime continúa incrementando a 1 segundo exacto (tick CTIMER0 de 1 ms inmune).
```

### TC-08: Validación Numérica Estricta
```text
Entrada: DUTY 20abc
Salida:  ERR ARG

Entrada: DUTY -1
Salida:  ERR ARG

Entrada: DUTY 101
Salida:  ERR RANGE

Entrada: FREQ 1500
Salida:  ERR RANGE

Entrada: DUTY
Salida:  ERR ARG
```

### TC-09: Desbordamiento de Línea (> 63 Caracteres)
```text
Entrada: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\r\n
Salida:  ERR OVERFLOW

Entrada: STATUS\r\n
Salida:  STATUS: Modo=MANUAL, Freq=..., Duty=..., SavedDuty=..., Uptime=..., RxErr=1
```

### TC-13: Invarianza ante Desbordamiento de 32 bits
```text
Entrada: TEST OVERFLOW\r\n
Salida:  OK INJECTED

Entrada: STATUS\r\n
Salida:  STATUS: Modo=MANUAL, Freq=1000Hz, Duty=25%, SavedDuty=25%, Uptime=11s, BtnShort=0, BtnLong=0, RxErr=0

[Esperar 30 segundos]
Entrada: STATUS\r\n
Salida:  STATUS: Modo=MANUAL, Freq=1000Hz, Duty=25%, SavedDuty=25%, Uptime=49s, BtnShort=0, BtnLong=0, RxErr=0
```
