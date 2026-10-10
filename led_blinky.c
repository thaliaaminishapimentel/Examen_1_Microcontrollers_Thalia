/*
 * Parcial 1: Controlador de Iluminación con Consola Serial
 * Estudiante: Thalía Aminisha Pimentel
 * Matrícula: 20250531
 * Placa: FRDM-MCXA156
 */

#include "app_challenge.h"
#include "app.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_clock.h"
#include "fsl_ctimer.h"
#include "fsl_lpuart.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_debug_console.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

/*******************************************************************************
 * Variables Globales Compartidas del Sistema
 ******************************************************************************/
app_state_t g_appCurrentState = STATE_MANUAL;
uint32_t    g_manualSavedDuty = PWM_DEFAULT_DUTY_PERCENT;
bool        g_streamEnabled   = false;

/*******************************************************************************
 * GPIO e Interrupciones: Botón SW3 (P0_6) y LED de Estado (P3_13)
 ******************************************************************************/

/* Bandera volátil señalizada por la ISR del botón en ambos flancos */
static volatile uint32_t g_buttonEdgePending = 0U;

/* Variables internas del antirrebote y medición de duración */
static uint32_t s_debounceCounter         = 0U;
static uint32_t s_pressDurationMs         = 0U;
static bool     s_lastRawPinPressed       = false;
static bool     s_buttonValidatedPressed  = false;
static bool     s_longPressReported       = false;

/* Contadores separados de pulsaciones aceptadas */
static uint32_t s_shortPressCount = 0U;
static uint32_t s_longPressCount  = 0U;

void APP_BTN_IRQHandler(void)
{
    uint32_t mask = (1UL << APP_BTN_PIN);
    if (GPIO_GpioGetInterruptFlags(APP_BTN_GPIO) & mask)
    {
        /* Limpiar causa de la interrupción en hardware */
        GPIO_GpioClearInterruptFlags(APP_BTN_GPIO, mask);
        g_buttonEdgePending = 1U;
    }
    SDK_ISR_EXIT_BARRIER;
}

/* Manejador seguro para evitar que el trap débil de SysTick en startup bloquee la MCU */
void SysTick_Handler(void)
{
}

/* Soporte adicional para pulsador SW2 (P1_7) de la tarjeta FRDM-MCXA156 */
void GPIO1_IRQHandler(void)
{
    uint32_t mask = (1UL << 7U);
    if (GPIO_GpioGetInterruptFlags(GPIO1) & mask)
    {
        GPIO_GpioClearInterruptFlags(GPIO1, mask);
        g_buttonEdgePending = 1U;
    }
    SDK_ISR_EXIT_BARRIER;
}

static uint32_t TakeButtonEdge(void)
{
    uint32_t irqState = DisableGlobalIRQ();
    uint32_t pending  = g_buttonEdgePending;
    g_buttonEdgePending = 0U;
    EnableGlobalIRQ(irqState);
    return pending;
}

void GPIO_InitApp(void)
{
    /* Habilitar relojes de periféricos GPIO y PORT */
    CLOCK_EnableClock(kCLOCK_GateGPIO0);
    CLOCK_EnableClock(kCLOCK_GatePORT0);
    CLOCK_EnableClock(kCLOCK_GateGPIO3);
    CLOCK_EnableClock(kCLOCK_GatePORT3);

    RESET_ReleasePeripheralReset(kGPIO0_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT0_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn);

    /* 1. LED de Estado Onboard: P3_13 (LED Verde, activo en nivel bajo) */
    const port_pin_config_t ledPortConfig = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt0, /* Alt0: GPIO */
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(APP_STATUS_LED_PORT, APP_STATUS_LED_PIN, &ledPortConfig);

    gpio_pin_config_t ledGpioConfig = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 1U /* Inicia apagado (nivel alto) */
    };
    GPIO_PinInit(APP_STATUS_LED_GPIO, APP_STATUS_LED_PIN, &ledGpioConfig);

    /* 2. Pin de Salida PWM: P3_12 (Conector J1 pin 15)
     * Inicia apagado (nivel alto = LED rojo onboard apagado) */
    const port_pin_config_t pwmPortConfig = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt4, /* Alt4: CT1_MAT2 */
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(APP_PWM_PORT, APP_PWM_PIN, &pwmPortConfig);

    gpio_pin_config_t pwmGpioConfig = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 1U /* Iniciar apagado (nivel alto) */
    };
    GPIO_PinInit(APP_PWM_GPIO, APP_PWM_PIN, &pwmGpioConfig);

    /* 3. Pulsador SW3 Onboard: P0_6 (activo en bajo con pull-up interno) */
    const port_pin_config_t btnPortConfig = {
        .pullSelect = kPORT_PullUp, /* Pull-up interno requerido */
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt0, /* Alt0: GPIO */
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(APP_BTN_PORT, APP_BTN_PIN, &btnPortConfig);

    gpio_pin_config_t btnGpioConfig = {
        .pinDirection = kGPIO_DigitalInput,
        .outputLogic = 0U
    };
    GPIO_PinInit(APP_BTN_GPIO, APP_BTN_PIN, &btnGpioConfig);

    /* Configurar interrupción en ambos flancos para SW3 */
    GPIO_GpioClearInterruptFlags(APP_BTN_GPIO, 1UL << APP_BTN_PIN);
    GPIO_SetPinInterruptConfig(APP_BTN_GPIO, APP_BTN_PIN, kGPIO_InterruptEitherEdge);
    NVIC_SetPriority(APP_BTN_IRQn, 3);
    EnableIRQ(APP_BTN_IRQn);

    /* 4. Pulsador SW2 Onboard: P1_7 (soporte complementario de pulsador) */
    CLOCK_EnableClock(kCLOCK_GateGPIO1);
    CLOCK_EnableClock(kCLOCK_GatePORT1);
    RESET_ReleasePeripheralReset(kGPIO1_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn);

    PORT_SetPinConfig(PORT1, 7U, &btnPortConfig);
    GPIO_PinInit(GPIO1, 7U, &btnGpioConfig);
    GPIO_GpioClearInterruptFlags(GPIO1, 1UL << 7U);
    GPIO_SetPinInterruptConfig(GPIO1, 7U, kGPIO_InterruptEitherEdge);
    NVIC_SetPriority(GPIO1_IRQn, 3);
    EnableIRQ(GPIO1_IRQn);
}

void StatusLed_Set(bool on)
{
    /* Activo en BAJO: 0 enciende, 1 apaga */
    GPIO_PinWrite(APP_STATUS_LED_GPIO, APP_STATUS_LED_PIN, on ? 0U : 1U);
}

void StatusLed_Toggle(void)
{
    GPIO_PortToggle(APP_STATUS_LED_GPIO, 1UL << APP_STATUS_LED_PIN);
}

/* Antirrebote estricto de 30 ms y clasificación de pulsaciones */
button_event_t Button_ProcessTick(void)
{
    button_event_t event = BTN_EVENT_NONE;

    /* Consumo atómico de la bandera de flanco de la ISR */
    (void)TakeButtonEdge();

    /* Nivel físico del pin: detecta tanto SW3 (P0_6) como SW2 (P1_7) */
    bool currentRawPressed = (GPIO_PinRead(APP_BTN_GPIO, APP_BTN_PIN) == 0U) ||
                             (GPIO_PinRead(GPIO1, 7U) == 0U);

    if (currentRawPressed != s_lastRawPinPressed)
    {
        /* Cambio detectado durante la ventana: reiniciar temporizador de estabilidad */
        s_lastRawPinPressed = currentRawPressed;
        s_debounceCounter = 0U;
    }
    else
    {
        if (s_debounceCounter < BTN_DEBOUNCE_TIME_MS)
        {
            s_debounceCounter++;
            if (s_debounceCounter >= BTN_DEBOUNCE_TIME_MS)
            {
                /* Nivel estable confirmado tras 30 ms continuos */
                if (currentRawPressed != s_buttonValidatedPressed)
                {
                    s_buttonValidatedPressed = currentRawPressed;
                    if (s_buttonValidatedPressed)
                    {
                        /* Inicio de pulsación validada tras 30 ms */
                        s_pressDurationMs = BTN_DEBOUNCE_TIME_MS;
                    }
                    else
                    {
                        /* Fin de pulsación (liberación validada) */
                        if (!s_longPressReported &&
                            (s_pressDurationMs >= BTN_SHORT_PRESS_MIN_MS) &&
                            (s_pressDurationMs < BTN_LONG_PRESS_MS))
                        {
                            s_shortPressCount++;
                            event = BTN_EVENT_SHORT;
                        }
                        s_pressDurationMs = 0U;
                        s_longPressReported = false;
                    }
                }
            }
        }
    }

    /* Medición sobre el estado validado */
    if (s_buttonValidatedPressed)
    {
        s_pressDurationMs++;

        /* Pulsación larga: se ejecuta exactamente una vez al alcanzar 1500 ms */
        if ((s_pressDurationMs >= BTN_LONG_PRESS_MS) && !s_longPressReported)
        {
            s_longPressReported = true;
            s_longPressCount++;
            event = BTN_EVENT_LONG;
        }
    }

    return event;
}

uint32_t Button_GetShortPressCount(void)
{
    return s_shortPressCount;
}

uint32_t Button_GetLongPressCount(void)
{
    return s_longPressCount;
}

/*******************************************************************************
 * Base de Tiempo Periférica de 1 ms (CTIMER0)
 * Requisito: SysTick no sustituye este temporizador periférico.
 ******************************************************************************/
static volatile uint32_t g_systemMs = 0U;

void APP_TICK_IRQHandler(void)
{
    uint32_t intFlags = CTIMER0->IR;
    if (intFlags & CTIMER_IR_MR0INT_MASK)
    {
        CTIMER0->IR = CTIMER_IR_MR0INT_MASK; /* Limpiar escribiendo 1 */
        g_systemMs++;
    }
    SDK_ISR_EXIT_BARRIER;
}

void Timer_TickInit(void)
{
    SysTick->CTRL = 0U;
    CLOCK_EnableClock(kCLOCK_GateCTIMER0);
    CLOCK_SetClockDiv(kCLOCK_DivCTIMER0, 1U);
    CLOCK_AttachClk(APP_TICK_CLK_ATTACH);
    RESET_ReleasePeripheralReset(kCTIMER0_RST_SHIFT_RSTn);

    CTIMER0->TCR = 0U;  /* Detener timer */
    CTIMER0->CTCR = 0U; /* Modo temporizador estándar */

    /* Calcular con el reloj funcional real del periférico */
    uint32_t clkFreq = CLOCK_GetCTimerClkFreq(0U);
    if (clkFreq == 0U)
    {
        clkFreq = 96000000U;
    }

    /* Prescaler a 1 MHz (1 us por conteo) */
    CTIMER0->PR = (clkFreq / 1000000U) - 1U;

    /* Match 0 para 1000 us = 1 ms */
    CTIMER0->MR[0] = 1000U - 1U;

    /* Reset e interrupción en Match 0 */
    CTIMER0->MCR = (1UL << 0U) | (1UL << 1U);

    CTIMER0->IR = 0xFFU;
    NVIC_SetPriority(APP_TICK_IRQn, 2);
    EnableIRQ(APP_TICK_IRQn);

    CTIMER0->TCR = 1U; /* Iniciar temporizador */
}

uint32_t Timer_GetMs(void)
{
    return g_systemMs;
}

void Timer_InjectMs(uint32_t msVal)
{
    uint32_t irq = DisableGlobalIRQ();
    g_systemMs = msVal;
    EnableGlobalIRQ(irq);
}

/*******************************************************************************
 * PWM de Hardware (CTIMER1 en P3_12 / CT1_MAT2)
 * Soporta frecuencias: 500, 1000, 2000 Hz.
 * Niveles constantes garantizados en 0% (0V) y 100% (3.3V) sin pulsos residuales.
 ******************************************************************************/
static uint32_t s_currentPwmFreqHz = PWM_DEFAULT_FREQ_HZ;
static uint32_t s_currentPwmDuty   = PWM_DEFAULT_DUTY_PERCENT;
static uint32_t s_pwmPeriodCounts  = 1000U;

static void Pwm_ConfigurePinMux(bool enablePwmAlt)
{
    const port_pin_config_t pinConfig = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        /* ALT4: CT1_MAT2 (salida PWM de hardware), ALT0: GPIO3_12 para nivel constante limpio */
        .mux = enablePwmAlt ? kPORT_MuxAlt4 : kPORT_MuxAlt0,
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(APP_PWM_PORT, APP_PWM_PIN, &pinConfig);
}

status_t Pwm_Init(uint32_t freqHz, uint32_t dutyPercent)
{
    if ((freqHz != 500U && freqHz != 1000U && freqHz != 2000U) || (dutyPercent > 100U))
    {
        return kStatus_InvalidArgument;
    }

    s_currentPwmFreqHz = freqHz;
    s_currentPwmDuty   = dutyPercent;

    CLOCK_EnableClock(kCLOCK_GateCTIMER1);
    CLOCK_SetClockDiv(kCLOCK_DivCTIMER1, 1U);
    CLOCK_AttachClk(APP_PWM_CLK_ATTACH);
    RESET_ReleasePeripheralReset(kCTIMER1_RST_SHIFT_RSTn);

    CTIMER1->TCR = 0U;
    CTIMER1->CTCR = 0U;

    /* Reloj periférico real */
    uint32_t clkFreq = CLOCK_GetCTimerClkFreq(1U);
    if (clkFreq == 0U)
    {
        clkFreq = 96000000U;
    }

    /* Prescaler a 1 MHz (1 conteo = 1 microsegundo) */
    CTIMER1->PR = (clkFreq / 1000000U) - 1U;

    /* Cuentas de período: 1,000,000 / freqHz */
    s_pwmPeriodCounts = 1000000U / freqHz;

    /* Match 3 fija el período del PWM y reinicia el contador */
    CTIMER1->MR[3] = s_pwmPeriodCounts - 1U;
    CTIMER1->MCR = (1UL << 10U); /* MR3R */

    /* Habilitar PWM en Match 2 */
    CTIMER1->PWMC |= (1UL << 2U);

    /* Aplicar duty inicial */
    Pwm_SetDuty(dutyPercent);

    /* Arrancar CTIMER1 */
    CTIMER1->TCR = 1U;

    return kStatus_Success;
}

status_t Pwm_SetFrequency(uint32_t freqHz)
{
    if (freqHz != 500U && freqHz != 1000U && freqHz != 2000U)
    {
        return kStatus_InvalidArgument;
    }

    s_currentPwmFreqHz = freqHz;
    s_pwmPeriodCounts  = 1000000U / freqHz;

    CTIMER1->MR[3] = s_pwmPeriodCounts - 1U;

    /* Recalcular registros para el duty cycle actual */
    Pwm_SetDuty(s_currentPwmDuty);

    return kStatus_Success;
}

status_t Pwm_SetDuty(uint32_t dutyPercent)
{
    if (dutyPercent > 100U)
    {
        return kStatus_InvalidArgument;
    }

    s_currentPwmDuty = dutyPercent;

    if (dutyPercent == 0U)
    {
        /* Nivel BAJO constante sin pulsos residuales */
        Pwm_ConfigurePinMux(false);
        GPIO_PinWrite(APP_PWM_GPIO, APP_PWM_PIN, 0U);
        CTIMER1->MR[2] = s_pwmPeriodCounts;
    }
    else if (dutyPercent == 100U)
    {
        /* Nivel ALTO constante sin pulsos residuales */
        Pwm_ConfigurePinMux(false);
        GPIO_PinWrite(APP_PWM_GPIO, APP_PWM_PIN, 1U);
        CTIMER1->MR[2] = 0U;
    }
    else
    {
        /* Salida PWM de hardware en Match 2 (P3_12 ALT4) */
        Pwm_ConfigurePinMux(true);
        uint32_t matchVal = (s_pwmPeriodCounts * (100U - dutyPercent)) / 100U;
        CTIMER1->MR[2] = matchVal;
    }

    return kStatus_Success;
}

uint32_t Pwm_GetFrequency(void)
{
    return s_currentPwmFreqHz;
}

uint32_t Pwm_GetDuty(void)
{
    return s_currentPwmDuty;
}

/*******************************************************************************
 * Driver LPUART0 con Búfer Circular y Parser de Comandos
 * - Búfer circular de recepción: 128 bytes.
 * - Búfer circular de transmisión no bloqueante: 256 bytes.
 * - Soporta CR, LF y CRLF produciendo una única ejecución.
 * - Manejo riguroso de desbordamiento (> 63 caracteres) y resincronización.
 ******************************************************************************/
static volatile uint8_t  s_rxRingBuffer[UART_RING_BUFFER_SIZE];
static volatile uint16_t s_rxHead = 0U;
static volatile uint16_t s_rxTail = 0U;
static volatile uint32_t s_rxOverflowErrors = 0U;

static volatile uint8_t  s_txRingBuffer[UART_TX_RING_BUFFER_SIZE];
static volatile uint16_t s_txHead = 0U;
static volatile uint16_t s_txTail = 0U;

static char     s_lineBuffer[UART_MAX_LINE_LENGTH + 1U];
static uint16_t s_lineIndex = 0U;
static bool     s_lineOverflowDiscard = false;
static bool     s_lastCharWasCr = false;

void LPUART0_IRQHandler(void)
{
    uint32_t status = LPUART0->STAT;

    /* 1. Recepción por interrupción */
    if (status & LPUART_STAT_RDRF_MASK)
    {
        uint8_t byte = (uint8_t)(LPUART0->DATA);
        uint16_t nextHead = (s_rxHead + 1U) % UART_RING_BUFFER_SIZE;
        if (nextHead != s_rxTail)
        {
            s_rxRingBuffer[s_rxHead] = byte;
            s_rxHead = nextHead;
        }
        else
        {
            s_rxOverflowErrors++;
        }
    }

    /* 2. Transmisión encolada no bloqueante */
    if ((status & LPUART_STAT_TDRE_MASK) && (LPUART0->CTRL & LPUART_CTRL_TIE_MASK))
    {
        if (s_txTail != s_txHead)
        {
            LPUART0->DATA = s_txRingBuffer[s_txTail];
            s_txTail = (s_txTail + 1U) % UART_TX_RING_BUFFER_SIZE;
        }
        else
        {
            /* Búfer TX vacío: apagar interrupción TDRE */
            LPUART0->CTRL &= ~LPUART_CTRL_TIE_MASK;
        }
    }

    /* 3. Limpieza de flags de error si ocurren */
    if (status & (LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK))
    {
        LPUART0->STAT |= (LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK);
    }

    SDK_ISR_EXIT_BARRIER;
}

void Uart_InitConsole(void)
{
    CLOCK_SetClockDiv(kCLOCK_DivLPUART0, 1U);
    CLOCK_AttachClk(kFRO12M_to_LPUART0);
    RESET_ReleasePeripheralReset(kLPUART0_RST_SHIFT_RSTn);

    CLOCK_EnableClock(kCLOCK_GatePORT0);
    const port_pin_config_t uartPinConfig = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt2,
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(PORT0, 2U, &uartPinConfig);
    PORT_SetPinConfig(PORT0, 3U, &uartPinConfig);

    lpuart_config_t config;
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = 115200U;
    config.enableTx     = true;
    config.enableRx     = true;

    LPUART_Init(LPUART0, &config, 12000000U);

    LPUART_EnableInterrupts(LPUART0, kLPUART_RxDataRegFullInterruptEnable);
    NVIC_SetPriority(LPUART0_IRQn, 3);
    EnableIRQ(LPUART0_IRQn);
}

void Uart_SendString(const char *str)
{
    if (str == NULL) return;

    while (*str != '\0')
    {
        uint8_t ch = (uint8_t)(*str++);

        while (((s_txHead + 1U) % UART_TX_RING_BUFFER_SIZE) == s_txTail)
        {
        }

        uint32_t irq = DisableGlobalIRQ();
        s_txRingBuffer[s_txHead] = ch;
        s_txHead = (s_txHead + 1U) % UART_TX_RING_BUFFER_SIZE;
        LPUART0->CTRL |= LPUART_CTRL_TIE_MASK; /* Habilitar interrupción de TX */
        EnableGlobalIRQ(irq);
    }
}

static bool Uart_GetRxByte(uint8_t *byte)
{
    if (s_rxHead == s_rxTail)
    {
        return false;
    }
    *byte = s_rxRingBuffer[s_rxTail];
    s_rxTail = (s_rxTail + 1U) % UART_RING_BUFFER_SIZE;
    return true;
}

static bool ParseStrictUnsignedInt(const char *str, uint32_t *outVal)
{
    if (str == NULL || *str == '\0') return false;

    while (*str == ' ') str++;
    if (*str == '\0') return false;

    uint64_t val = 0;
    while (*str != '\0' && *str != ' ')
    {
        if (!isdigit((unsigned char)*str))
        {
            return false;
        }
        val = val * 10U + (uint32_t)(*str - '0');
        if (val > 1000000000ULL) return false;
        str++;
    }

    while (*str == ' ') str++;
    if (*str != '\0') return false; /* Rechaza tokens extra */

    *outVal = (uint32_t)val;
    return true;
}

static void Uart_ExecuteCommand(char *cmdLine)
{
    char *p = cmdLine;
    while (*p == ' ') p++;
    if (*p == '\0') return;

    int len = strlen(p);
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\r' || p[len - 1] == '\n'))
    {
        p[--len] = '\0';
    }

    for (int k = 0; k < len; k++)
    {
        p[k] = (char)toupper((unsigned char)p[k]);
    }

    if (strcmp(p, "HELP") == 0)
    {
        Uart_SendString("Comandos disponibles:\r\n"
                        "  HELP\r\n"
                        "  STATUS\r\n"
                        "  MODE MANUAL | MODE AUTO\r\n"
                        "  DUTY <0-100>\r\n"
                        "  FREQ <500|1000|2000>\r\n"
                        "  PAUSE | RESUME\r\n"
                        "  STREAM ON | STREAM OFF\r\n");
    }
    else if (strcmp(p, "STATUS") == 0)
    {
        char resp[160];
        const char *stateStr = (g_appCurrentState == STATE_MANUAL) ? "MANUAL" :
                               (g_appCurrentState == STATE_AUTO)   ? "AUTO" : "PAUSA";
        snprintf(resp, sizeof(resp),
                 "STATUS: Modo=%s, Freq=%luHz, Duty=%lu%%, SavedDuty=%lu%%, Uptime=%lus, BtnShort=%lu, BtnLong=%lu, RxErr=%lu\r\n",
                 stateStr,
                 (unsigned long)Pwm_GetFrequency(),
                 (unsigned long)Pwm_GetDuty(),
                 (unsigned long)g_manualSavedDuty,
                 (unsigned long)(Timer_GetMs() / 1000U),
                 (unsigned long)Button_GetShortPressCount(),
                 (unsigned long)Button_GetLongPressCount(),
                 (unsigned long)s_rxOverflowErrors);
        Uart_SendString(resp);
    }
    else if (strncmp(p, "MODE", 4) == 0)
    {
        if (g_appCurrentState == STATE_PAUSA)
        {
            Uart_SendString("ERR STATE\r\n");
            return;
        }

        if (p[4] != ' ')
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }

        char *arg = p + 5;
        while (*arg == ' ') arg++;

        if (strcmp(arg, "MANUAL") == 0)
        {
            Fsm_SetModeManual();
            Uart_SendString("OK\r\n");
        }
        else if (strcmp(arg, "AUTO") == 0)
        {
            Fsm_SetModeAuto();
            Uart_SendString("OK\r\n");
        }
        else
        {
            Uart_SendString("ERR ARG\r\n");
        }
    }
    else if (strncmp(p, "DUTY", 4) == 0)
    {
        if (g_appCurrentState == STATE_PAUSA || g_appCurrentState == STATE_AUTO)
        {
            Uart_SendString("ERR STATE\r\n");
            return;
        }

        if (p[4] != ' ')
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }

        uint32_t dutyVal = 0;
        if (!ParseStrictUnsignedInt(p + 5, &dutyVal))
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }
        if (dutyVal > 100U)
        {
            Uart_SendString("ERR RANGE\r\n");
            return;
        }

        g_manualSavedDuty = dutyVal;
        Pwm_SetDuty(dutyVal);
        Uart_SendString("OK\r\n");
    }
    else if (strncmp(p, "FREQ", 4) == 0)
    {
        if (g_appCurrentState == STATE_PAUSA)
        {
            Uart_SendString("ERR STATE\r\n");
            return;
        }

        if (p[4] != ' ')
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }

        uint32_t freqVal = 0;
        if (!ParseStrictUnsignedInt(p + 5, &freqVal))
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }
        if (freqVal != 500U && freqVal != 1000U && freqVal != 2000U)
        {
            Uart_SendString("ERR RANGE\r\n");
            return;
        }

        Pwm_SetFrequency(freqVal);
        Uart_SendString("OK\r\n");
    }
    else if (strncmp(p, "PAUSE", 5) == 0)
    {
        if (p[5] != '\0')
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }

        Fsm_Pause();
        Uart_SendString("OK\r\n");
    }
    else if (strncmp(p, "RESUME", 6) == 0)
    {
        if (p[6] != '\0')
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }

        if (g_appCurrentState != STATE_PAUSA)
        {
            Uart_SendString("ERR STATE\r\n");
            return;
        }

        Fsm_Resume();
        Uart_SendString("OK\r\n");
    }
    else if (strncmp(p, "STREAM", 6) == 0)
    {
        if (p[6] != ' ')
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }

        char *arg = p + 7;
        while (*arg == ' ') arg++;

        if (strcmp(arg, "ON") == 0)
        {
            g_streamEnabled = true;
            Uart_SendString("OK\r\n");
        }
        else if (strcmp(arg, "OFF") == 0)
        {
            g_streamEnabled = false;
            Uart_SendString("OK\r\n");
        }
        else
        {
            Uart_SendString("ERR ARG\r\n");
        }
    }
    else if (strcmp(p, "TEST OVERFLOW") == 0)
    {
        /* Inyección de prueba: sitúa el reloj a 32 ms antes de desbordar UINT32_MAX */
        Timer_InjectMs(0xFFFFFFE0U);
        Uart_SendString("OK INJECTED\r\n");
    }
    else
    {
        Uart_SendString("ERR COMMAND\r\n");
    }
}

void Uart_ProcessConsole(void)
{
    uint8_t ch = 0;
    while (Uart_GetRxByte(&ch))
    {
        if (ch == '\r')
        {
            s_lastCharWasCr = true;
            if (!s_lineOverflowDiscard)
            {
                s_lineBuffer[s_lineIndex] = '\0';
                Uart_ExecuteCommand(s_lineBuffer);
            }
            else
            {
                Uart_SendString("ERR OVERFLOW\r\n");
                s_lineOverflowDiscard = false;
            }
            s_lineIndex = 0U;
            continue;
        }

        if (ch == '\n')
        {
            if (s_lastCharWasCr)
            {
                s_lastCharWasCr = false;
                continue;
            }

            s_lastCharWasCr = false;
            if (!s_lineOverflowDiscard)
            {
                s_lineBuffer[s_lineIndex] = '\0';
                Uart_ExecuteCommand(s_lineBuffer);
            }
            else
            {
                Uart_SendString("ERR OVERFLOW\r\n");
                s_lineOverflowDiscard = false;
            }
            s_lineIndex = 0U;
            continue;
        }

        s_lastCharWasCr = false;

        if (!s_lineOverflowDiscard)
        {
            if (s_lineIndex < UART_MAX_LINE_LENGTH)
            {
                s_lineBuffer[s_lineIndex++] = (char)ch;
            }
            else
            {
                /* Desbordamiento de línea (> 63 caracteres) */
                s_lineOverflowDiscard = true;
                s_rxOverflowErrors++;
            }
        }
    }
}

/*******************************************************************************
 * Máquina de Estados Finito (FSM) y Rampa Triangular
 * - MANUAL (inicial): PWM 1 kHz, duty 25%, LED encendido fijo. Pulsación corta -> AUTO.
 * - AUTO: Rampa triangular 10% a 90% (paso 1% cada 20 ms, ciclo total 3.2s). LED alterna 500 ms.
 *         Pulsación corta -> MANUAL (restaura último duty manual).
 * - PAUSA: PWM 0%, LED alterna 125 ms. Conserva modo, duty y dirección.
 *          Pulsación larga o RESUME -> reanuda estado previo exacto.
 *          Pulsación corta en PAUSA se ignora.
 ******************************************************************************/
static app_state_t s_previousState     = STATE_MANUAL;
static int32_t     s_autoDuty          = 10;
static bool        s_autoAscending     = true;
static uint32_t    s_lastRampTimeMs    = 0U;
static uint32_t    s_lastLedBlinkTimeMs = 0U;

void Fsm_Init(void)
{
    g_appCurrentState = STATE_MANUAL;
    g_manualSavedDuty = PWM_DEFAULT_DUTY_PERCENT;
    s_previousState   = STATE_MANUAL;

    s_autoDuty        = 10;
    s_autoAscending   = true;

    /* Estado inicial MANUAL: LED verde encendido fijo, duty 25% */
    StatusLed_Set(true);
    Pwm_SetFrequency(PWM_DEFAULT_FREQ_HZ);
    Pwm_SetDuty(g_manualSavedDuty);

    s_lastRampTimeMs     = Timer_GetMs();
    s_lastLedBlinkTimeMs = Timer_GetMs();
}

app_state_t Fsm_GetState(void)
{
    return g_appCurrentState;
}

void Fsm_SetModeManual(void)
{
    g_appCurrentState = STATE_MANUAL;
    Pwm_SetDuty(g_manualSavedDuty);
    StatusLed_Set(true);
}

void Fsm_SetModeAuto(void)
{
    g_appCurrentState = STATE_AUTO;
    s_autoDuty        = 10;
    s_autoAscending   = true;
    s_lastRampTimeMs  = Timer_GetMs();
    s_lastLedBlinkTimeMs = Timer_GetMs();
    Pwm_SetDuty((uint32_t)s_autoDuty);
}

void Fsm_Pause(void)
{
    if (g_appCurrentState != STATE_PAUSA)
    {
        s_previousState = g_appCurrentState;
        g_appCurrentState = STATE_PAUSA;
        Pwm_SetDuty(0U);
        s_lastLedBlinkTimeMs = Timer_GetMs();
    }
}

void Fsm_Resume(void)
{
    if (g_appCurrentState == STATE_PAUSA)
    {
        g_appCurrentState = s_previousState;
        if (g_appCurrentState == STATE_MANUAL)
        {
            Pwm_SetDuty(g_manualSavedDuty);
            StatusLed_Set(true);
        }
        else if (g_appCurrentState == STATE_AUTO)
        {
            /* Continúa rampa desde los valores y dirección guardados */
            Pwm_SetDuty((uint32_t)s_autoDuty);
            s_lastRampTimeMs = Timer_GetMs();
            s_lastLedBlinkTimeMs = Timer_GetMs();
        }
    }
}

void Fsm_Process(button_event_t btnEvent)
{
    uint32_t now = Timer_GetMs();

    /* 1. Transiciones provocadas por el pulsador */
    switch (g_appCurrentState)
    {
        case STATE_MANUAL:
            if (btnEvent == BTN_EVENT_SHORT)
            {
                Fsm_SetModeAuto();
                Uart_SendString("[BOTON] Pulsacion CORTA: Conmutando a modo AUTO (Rampa 10%-90%)\r\n");
            }
            else if (btnEvent == BTN_EVENT_LONG)
            {
                Fsm_Pause();
                Uart_SendString("[BOTON] Pulsacion LARGA: Entrando en modo PAUSA (PWM 0%)\r\n");
            }
            break;

        case STATE_AUTO:
            if (btnEvent == BTN_EVENT_SHORT)
            {
                Fsm_SetModeManual();
                char buf[96];
                snprintf(buf, sizeof(buf), "[BOTON] Pulsacion CORTA: Conmutando a modo MANUAL | Duty=%lu%%\r\n", (unsigned long)g_manualSavedDuty);
                Uart_SendString(buf);
            }
            else if (btnEvent == BTN_EVENT_LONG)
            {
                Fsm_Pause();
                Uart_SendString("[BOTON] Pulsacion LARGA: Entrando en modo PAUSA (PWM 0%)\r\n");
            }
            break;

        case STATE_PAUSA:
            if (btnEvent == BTN_EVENT_SHORT)
            {
                Uart_SendString("[BOTON] Pulsacion CORTA ignorada en modo PAUSA\r\n");
            }
            else if (btnEvent == BTN_EVENT_LONG)
            {
                Fsm_Resume();
                const char *prevName = (g_appCurrentState == STATE_AUTO) ? "AUTO" : "MANUAL";
                char buf[96];
                snprintf(buf, sizeof(buf), "[BOTON] Pulsacion LARGA: Reanudando modo previo (%s)\r\n", prevName);
                Uart_SendString(buf);
            }
            break;

        default:
            g_appCurrentState = STATE_MANUAL;
            break;
    }

    /* 2. Comportamiento continuo de cada estado */
    switch (g_appCurrentState)
    {
        case STATE_MANUAL:
            StatusLed_Set(true); /* Encendido fijo */
            break;

        case STATE_AUTO:
            /* Rampa triangular: cada 20 ms, paso de 1 punto porcentual */
            if ((uint32_t)(now - s_lastRampTimeMs) >= 20U)
            {
                s_lastRampTimeMs = now;

                if (s_autoAscending)
                {
                    s_autoDuty++;
                    if (s_autoDuty >= 90)
                    {
                        s_autoDuty = 90;
                        s_autoAscending = false;
                    }
                }
                else
                {
                    s_autoDuty--;
                    if (s_autoDuty <= 10)
                    {
                        s_autoDuty = 10;
                        s_autoAscending = true;
                    }
                }
                Pwm_SetDuty((uint32_t)s_autoDuty);
            }

            /* LED de estado en AUTO: alterna cada 500 ms */
            if ((uint32_t)(now - s_lastLedBlinkTimeMs) >= 500U)
            {
                s_lastLedBlinkTimeMs = now;
                StatusLed_Toggle();
            }
            break;

        case STATE_PAUSA:
            /* LED de estado en PAUSA: alterna cada 125 ms */
            if ((uint32_t)(now - s_lastLedBlinkTimeMs) >= 125U)
            {
                s_lastLedBlinkTimeMs = now;
                StatusLed_Toggle();
            }
            break;
    }
}

/*******************************************************************************
 * Programa Principal - Planificación No Bloqueante
 ******************************************************************************/
int main(void)
{
    /* 1. Inicialización de la placa y reloj de alta frecuencia FRO96M */
    BOARD_InitHardware();
    BOARD_BootClockFRO96M();

    /* 2. Inicializar consola UART primero para telemetría y diagnóstico inmediato */
    Uart_InitConsole();

    /* Banner de bienvenida */
    Uart_SendString("\r\n========================================\r\n"
                    "FRDM-MCXA156: Controlador de Iluminacion\r\n"
                    "Estudiante: Thalia Pimentel (20250531)\r\n"
                    "Modo inicial: MANUAL | 1000 Hz | 25%\r\n"
                    "Escriba HELP para ver la lista de comandos\r\n"
                    "========================================\r\n");

    /* 3. Inicialización de periféricos propios del proyecto */
    GPIO_InitApp();
    Timer_TickInit();
    Pwm_Init(PWM_DEFAULT_FREQ_HZ, PWM_DEFAULT_DUTY_PERCENT);
    Fsm_Init();

    /* Variables de planificación no bloqueante */
    uint32_t lastTickService = Timer_GetMs();
    uint32_t lastStreamMs    = Timer_GetMs();

    while (1)
    {
        uint32_t now = Timer_GetMs();

        /* Tarea 1: Procesar recepción de consola UART */
        Uart_ProcessConsole();

        /* Tarea 2: Procesamiento periódico de botón y FSM cada 1 ms */
        if ((uint32_t)(now - lastTickService) >= 1U)
        {
            lastTickService = now;
            button_event_t btnEvent = Button_ProcessTick();
            Fsm_Process(btnEvent);
        }

        /* Tarea 3: Telemetría cada 1000 ms si STREAM ON está activo */
        if (g_streamEnabled && ((uint32_t)(now - lastStreamMs) >= 1000U))
        {
            lastStreamMs = now;
            char streamBuf[96];
            const char *stateStr = (g_appCurrentState == STATE_MANUAL) ? "MANUAL" :
                                   (g_appCurrentState == STATE_AUTO)   ? "AUTO" : "PAUSA";
            snprintf(streamBuf, sizeof(streamBuf),
                     "STREAM: Modo=%s, Freq=%luHz, Duty=%lu%%, Uptime=%lus\r\n",
                     stateStr,
                     (unsigned long)Pwm_GetFrequency(),
                     (unsigned long)Pwm_GetDuty(),
                     (unsigned long)(now / 1000U));
            Uart_SendString(streamBuf);
        }
    }
}
