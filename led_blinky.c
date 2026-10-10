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
#include "peripherals.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

static volatile uint32_t g_buttonEdgePending = 0U;

static uint32_t s_debounceCounter        = 0U;
static uint32_t s_pressDurationMs        = 0U;
static bool     s_lastRawPinPressed      = false;
static bool     s_buttonValidatedPressed = false;
static bool     s_longPressReported      = false;

static uint32_t s_shortPressCount = 0U;
static uint32_t s_longPressCount  = 0U;

static volatile uint8_t  s_rxRingBuffer[UART_RING_BUFFER_SIZE];
static volatile uint16_t s_rxHead = 0U;
static volatile uint16_t s_rxTail = 0U;
static volatile uint32_t s_rxOverflowErrors = 0U;

static volatile uint8_t  s_txRingBuffer[UART_TX_RING_BUFFER_SIZE];
static volatile uint16_t s_txHead = 0U;
static volatile uint16_t s_txTail = 0U;

void SysTick_Handler(void)
{
}

void APP_BTN_IRQHandler(void)
{
    uint32_t mask = (1UL << APP_BTN_PIN);
    if (GPIO_GpioGetInterruptFlags(APP_BTN_GPIO) & mask)
    {
        GPIO_GpioClearInterruptFlags(APP_BTN_GPIO, mask);
        g_buttonEdgePending = 1U;
    }
    SDK_ISR_EXIT_BARRIER;
}

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

void LPUART0_IRQHandler(void)
{
    uint32_t status = LPUART0->STAT;

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

    if ((status & LPUART_STAT_TDRE_MASK) && (LPUART0->CTRL & LPUART_CTRL_TIE_MASK))
    {
        if (s_txTail != s_txHead)
        {
            LPUART0->DATA = s_txRingBuffer[s_txTail];
            s_txTail = (s_txTail + 1U) % UART_TX_RING_BUFFER_SIZE;
        }
        else
        {
            LPUART0->CTRL &= ~LPUART_CTRL_TIE_MASK;
        }
    }

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
        LPUART0->CTRL |= LPUART_CTRL_TIE_MASK;
        EnableGlobalIRQ(irq);
    }
}

void GPIO_InitApp(void)
{
    CLOCK_EnableClock(kCLOCK_GateGPIO0);
    CLOCK_EnableClock(kCLOCK_GatePORT0);
    CLOCK_EnableClock(kCLOCK_GateGPIO1);
    CLOCK_EnableClock(kCLOCK_GatePORT1);
    CLOCK_EnableClock(kCLOCK_GateGPIO3);
    CLOCK_EnableClock(kCLOCK_GatePORT3);

    RESET_ReleasePeripheralReset(kGPIO0_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT0_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kGPIO1_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn);

    const port_pin_config_t ledPortConfig = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt0,
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(APP_STATUS_LED_PORT, APP_STATUS_LED_PIN, &ledPortConfig);

    gpio_pin_config_t ledGpioConfig = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 1U
    };
    GPIO_PinInit(APP_STATUS_LED_GPIO, APP_STATUS_LED_PIN, &ledGpioConfig);

    const port_pin_config_t pwmPortConfig = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt4,
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(APP_PWM_PORT, APP_PWM_PIN, &pwmPortConfig);

    gpio_pin_config_t pwmGpioConfig = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 0U
    };
    GPIO_PinInit(APP_PWM_GPIO, APP_PWM_PIN, &pwmGpioConfig);

    const port_pin_config_t btnPortConfig = {
        .pullSelect = kPORT_PullUp,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt0,
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

    GPIO_GpioClearInterruptFlags(APP_BTN_GPIO, 1UL << APP_BTN_PIN);
    GPIO_SetPinInterruptConfig(APP_BTN_GPIO, APP_BTN_PIN, kGPIO_InterruptEitherEdge);
    NVIC_SetPriority(APP_BTN_IRQn, 3);
    EnableIRQ(APP_BTN_IRQn);

    PORT_SetPinConfig(PORT1, 7U, &btnPortConfig);
    GPIO_PinInit(GPIO1, 7U, &btnGpioConfig);

    GPIO_GpioClearInterruptFlags(GPIO1, 1UL << 7U);
    GPIO_SetPinInterruptConfig(GPIO1, 7U, kGPIO_InterruptEitherEdge);
    NVIC_SetPriority(GPIO1_IRQn, 3);
    EnableIRQ(GPIO1_IRQn);
}

void StatusLed_Set(bool on)
{
    GPIO_PinWrite(APP_STATUS_LED_GPIO, APP_STATUS_LED_PIN, on ? 0U : 1U);
}

void StatusLed_Toggle(void)
{
    GPIO_PortToggle(APP_STATUS_LED_GPIO, 1UL << APP_STATUS_LED_PIN);
}

button_event_t Button_ProcessTick(void)
{
    button_event_t event = BTN_EVENT_NONE;

    (void)TakeButtonEdge();

    bool currentRawPressed = (GPIO_PinRead(APP_BTN_GPIO, APP_BTN_PIN) == 0U) ||
                             (GPIO_PinRead(GPIO1, 7U) == 0U);

    if (currentRawPressed != s_lastRawPinPressed)
    {
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
                if (currentRawPressed != s_buttonValidatedPressed)
                {
                    s_buttonValidatedPressed = currentRawPressed;
                    if (s_buttonValidatedPressed)
                    {
                        s_pressDurationMs = BTN_DEBOUNCE_TIME_MS;
                    }
                    else
                    {
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

    if (s_buttonValidatedPressed)
    {
        s_pressDurationMs++;
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

static volatile uint32_t g_systemMs = 0U;

void APP_TICK_IRQHandler(void)
{
    uint32_t intFlags = CTIMER0->IR;
    if (intFlags & CTIMER_IR_MR0INT_MASK)
    {
        CTIMER0->IR = CTIMER_IR_MR0INT_MASK;
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

    CTIMER0->TCR = 0U;
    CTIMER0->CTCR = 0U;

    uint32_t clkFreq = CLOCK_GetCTimerClkFreq(0U);
    if (clkFreq == 0U)
    {
        clkFreq = 96000000U;
    }

    CTIMER0->PR = (clkFreq / 1000000U) - 1U;
    CTIMER0->MR[0] = 1000U - 1U;
    CTIMER0->MCR = (1UL << 0U) | (1UL << 1U);
    CTIMER0->IR = 0xFFU;

    NVIC_SetPriority(APP_TICK_IRQn, 2);
    EnableIRQ(APP_TICK_IRQn);

    CTIMER0->TCR = 1U;
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

    uint32_t clkFreq = CLOCK_GetCTimerClkFreq(1U);
    if (clkFreq == 0U)
    {
        clkFreq = 96000000U;
    }

    CTIMER1->PR = (clkFreq / 1000000U) - 1U;
    s_pwmPeriodCounts = 1000000U / freqHz;

    CTIMER1->MR[3] = s_pwmPeriodCounts - 1U;
    CTIMER1->MCR = (1UL << 10U);
    CTIMER1->PWMC |= (1UL << 2U);

    Pwm_SetDuty(dutyPercent);

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
        Pwm_ConfigurePinMux(false);
        GPIO_PinWrite(APP_PWM_GPIO, APP_PWM_PIN, 0U);
        CTIMER1->MR[2] = s_pwmPeriodCounts;
    }
    else if (dutyPercent == 100U)
    {
        Pwm_ConfigurePinMux(false);
        GPIO_PinWrite(APP_PWM_GPIO, APP_PWM_PIN, 1U);
        CTIMER1->MR[2] = 0U;
    }
    else
    {
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

int main(void)
{
    BOARD_InitHardware();
    BOARD_BootClockFRO96M();
    Uart_InitConsole();

    Uart_SendString("\r\n========================================================\r\n");
    Uart_SendString("  FRDM-MCXA156 - CONTROLADOR DE ILUMINACION\r\n");
    Uart_SendString("  Estudiante: Thalia Aminisha Pimentel (Matricula: 20250531)\r\n");
    Uart_SendString("  Hito 3: Base de tiempo CTIMER0 (1 ms) y PWM CTIMER1 (1 kHz)\r\n");
    Uart_SendString("========================================================\r\n");
    Uart_SendString("[INIT] Reloj FRO96M configurado.\r\n");
    Uart_SendString("[INIT] CTIMER0 activo: tick de 1 ms e interrupcion habilitada.\r\n");
    Uart_SendString("[INIT] CTIMER1 activo: PWM en P3_12 (CT1_MAT2) @ 1000 Hz, Duty 25%.\r\n");
    Uart_SendString("[INIT] UART0 listo: RX=P0_2, TX=P0_3 a 115200 bps.\r\n");
    Uart_SendString("[INIT] Interrupciones habilitadas para SW3 (P0_6) y SW2 (P1_7).\r\n");
    Uart_SendString("[CONTROL] Pulsacion corta: cicla Duty (25% -> 50% -> 75% -> 100% -> 0%).\r\n");
    Uart_SendString("[CONTROL] Pulsacion larga: cicla Frecuencia (500 Hz -> 1000 Hz -> 2000 Hz).\r\n\r\n");

    GPIO_InitApp();
    Timer_TickInit();
    Pwm_Init(PWM_DEFAULT_FREQ_HZ, PWM_DEFAULT_DUTY_PERCENT);

    uint32_t lastTickService = Timer_GetMs();
    uint32_t lastHeartbeat   = Timer_GetMs();
    char msgBuf[128];

    while (1)
    {
        uint32_t now = Timer_GetMs();

        if ((uint32_t)(now - lastTickService) >= 1U)
        {
            lastTickService = now;
            button_event_t event = Button_ProcessTick();

            if (event == BTN_EVENT_SHORT)
            {
                uint32_t nextDuty = (Pwm_GetDuty() + 25U);
                if (nextDuty > 100U)
                {
                    nextDuty = 0U;
                }
                Pwm_SetDuty(nextDuty);
                snprintf(msgBuf, sizeof(msgBuf),
                         "[EVENTO] Pulsacion CORTA detectada (< 1.5s) | Duty fijado en: %lu%% | Total cortas: %lu\r\n",
                         (unsigned long)Pwm_GetDuty(), (unsigned long)s_shortPressCount);
                Uart_SendString(msgBuf);
                StatusLed_Toggle();
            }
            else if (event == BTN_EVENT_LONG)
            {
                uint32_t curFreq = Pwm_GetFrequency();
                uint32_t nextFreq = 1000U;
                if (curFreq == 1000U)
                {
                    nextFreq = 2000U;
                }
                else if (curFreq == 2000U)
                {
                    nextFreq = 500U;
                }
                else
                {
                    nextFreq = 1000U;
                }
                Pwm_SetFrequency(nextFreq);
                snprintf(msgBuf, sizeof(msgBuf),
                         "[EVENTO] Pulsacion LARGA detectada (>= 1.5s) | Frecuencia fijada en: %lu Hz | Total largas: %lu\r\n",
                         (unsigned long)Pwm_GetFrequency(), (unsigned long)s_longPressCount);
                Uart_SendString(msgBuf);
            }
        }

        if ((uint32_t)(now - lastHeartbeat) >= 500U)
        {
            lastHeartbeat = now;
            StatusLed_Toggle();
        }
    }
}
