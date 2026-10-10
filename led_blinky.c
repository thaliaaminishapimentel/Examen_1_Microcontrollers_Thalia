#include "app_challenge.h"
#include "app.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_clock.h"
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
        .mux = kPORT_MuxAlt0,
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(APP_PWM_PORT, APP_PWM_PIN, &pwmPortConfig);

    gpio_pin_config_t pwmGpioConfig = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 1U
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

int main(void)
{
    BOARD_InitHardware();
    GPIO_InitApp();

    PRINTF("\r\n========================================================\r\n");
    PRINTF("  FRDM-MCXA156 - CONTROLADOR DE ILUMINACION\r\n");
    PRINTF("  Estudiante: Thalia Aminisha Pimentel (Matricula: 20250531)\r\n");
    PRINTF("  Hito 2: Interrupciones GPIO y antirrebote (30 ms).\r\n");
    PRINTF("========================================================\r\n");
    PRINTF("[INIT] Reloj FRO96M configurado.\r\n");
    PRINTF("[INIT] Interrupciones habilitadas para SW3 (P0_6) y SW2 (P1_7).\r\n");
    PRINTF("[INIT] Antirrebote activo (30 ms). Esperando pulsaciones...\r\n\r\n");

    StatusLed_Set(true);
    SDK_DelayAtLeastUs(200000U, SystemCoreClock);
    StatusLed_Set(false);

    uint32_t heartbeatCounter = 0U;

    while (1)
    {
        SDK_DelayAtLeastUs(1000U, SystemCoreClock);

        button_event_t event = Button_ProcessTick();

        if (event == BTN_EVENT_SHORT)
        {
            PRINTF("[EVENTO] Pulsacion CORTA detectada (< 1.5s) | Total: %lu\r\n", s_shortPressCount);
            StatusLed_Toggle();
        }
        else if (event == BTN_EVENT_LONG)
        {
            PRINTF("[EVENTO] Pulsacion LARGA detectada (>= 1.5s) | Total: %lu\r\n", s_longPressCount);
            GPIO_PortToggle(APP_PWM_GPIO, 1UL << APP_PWM_PIN);
        }

        heartbeatCounter++;
        if (heartbeatCounter >= 1000U)
        {
            heartbeatCounter = 0U;
            StatusLed_Toggle();
        }
    }
}
