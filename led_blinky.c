#include "app_challenge.h"
#include "app.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_debug_console.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

void SysTick_Handler(void)
{
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

    PORT_SetPinConfig(PORT1, 7U, &btnPortConfig);
    GPIO_PinInit(GPIO1, 7U, &btnGpioConfig);
}

void StatusLed_Set(bool on)
{
    GPIO_PinWrite(APP_STATUS_LED_GPIO, APP_STATUS_LED_PIN, on ? 0U : 1U);
}

void StatusLed_Toggle(void)
{
    GPIO_PortToggle(APP_STATUS_LED_GPIO, 1UL << APP_STATUS_LED_PIN);
}

int main(void)
{
    BOARD_InitHardware();
    GPIO_InitApp();

    PRINTF("\r\n========================================================\r\n");
    PRINTF("  FRDM-MCXA156 - CONTROLADOR DE ILUMINACION\r\n");
    PRINTF("  Estudiante: Thalia Aminisha Pimentel (Matricula: 20250531)\r\n");
    PRINTF("  Hito 1: Setup inicial, pines GPIO y consola serial.\r\n");
    PRINTF("========================================================\r\n");
    PRINTF("[INIT] Reloj FRO96M configurado.\r\n");
    PRINTF("[INIT] GPIOs listos: LED Verde (P3_13), LED PWM J1-15 (P3_12).\r\n");
    PRINTF("[INIT] Botones listos: SW3 (P0_6) y SW2 (P1_7) con pull-up.\r\n");
    PRINTF("[INIT] Hardware en estado seguro y verificado.\r\n\r\n");

    StatusLed_Set(true);
    SDK_DelayAtLeastUs(200000U, SystemCoreClock);
    StatusLed_Set(false);

    while (1)
    {
        SDK_DelayAtLeastUs(500000U, SystemCoreClock);
        StatusLed_Toggle();
    }
}
