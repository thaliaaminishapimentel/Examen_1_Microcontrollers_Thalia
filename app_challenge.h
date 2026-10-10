#ifndef _APP_CHALLENGE_H_
#define _APP_CHALLENGE_H_

#include <stdint.h>
#include <stdbool.h>
#include "fsl_common.h"
#include "fsl_gpio.h"
#include "fsl_port.h"

#define APP_STATUS_LED_GPIO        GPIO3
#define APP_STATUS_LED_PIN         13U
#define APP_STATUS_LED_PORT        PORT3

#define APP_BTN_GPIO               GPIO0
#define APP_BTN_PIN                6U
#define APP_BTN_PORT               PORT0
#define APP_BTN_IRQn               GPIO0_IRQn
#define APP_BTN_IRQHandler         GPIO0_IRQHandler

#define APP_PWM_GPIO               GPIO3
#define APP_PWM_PIN                12U
#define APP_PWM_PORT               PORT3

#define BTN_DEBOUNCE_TIME_MS       30U
#define BTN_SHORT_PRESS_MIN_MS     30U
#define BTN_LONG_PRESS_MS          1500U

typedef enum {
    BTN_EVENT_NONE = 0,
    BTN_EVENT_SHORT,
    BTN_EVENT_LONG
} button_event_t;

typedef enum {
    STATE_MANUAL = 0,
    STATE_AUTO,
    STATE_PAUSA
} app_state_t;

#define APP_TICK_TIMER             CTIMER0
#define APP_TICK_CLK_ATTACH        kFRO_HF_to_CTIMER0
#define APP_TICK_IRQn              CTIMER0_IRQn
#define APP_TICK_IRQHandler        CTIMER0_IRQHandler

#define APP_PWM_TIMER              CTIMER1
#define APP_PWM_CLK_ATTACH         kFRO_HF_to_CTIMER1
#define APP_PWM_PERIOD_CH          kCTIMER_Match_3
#define APP_PWM_OUTPUT_CH          kCTIMER_Match_2

#define PWM_DEFAULT_FREQ_HZ        1000U
#define PWM_DEFAULT_DUTY_PERCENT   25U

#define UART_RING_BUFFER_SIZE      128U
#define UART_TX_RING_BUFFER_SIZE   256U
#define UART_MAX_LINE_LENGTH       63U

void GPIO_InitApp(void);
void StatusLed_Set(bool on);
void StatusLed_Toggle(void);

button_event_t Button_ProcessTick(void);
uint32_t Button_GetShortPressCount(void);
uint32_t Button_GetLongPressCount(void);

void Timer_TickInit(void);
uint32_t Timer_GetMs(void);
void Timer_InjectMs(uint32_t msVal);

status_t Pwm_Init(uint32_t freqHz, uint32_t dutyPercent);
status_t Pwm_SetFrequency(uint32_t freqHz);
status_t Pwm_SetDuty(uint32_t dutyPercent);
uint32_t Pwm_GetFrequency(void);
uint32_t Pwm_GetDuty(void);

void Uart_InitConsole(void);
void Uart_SendString(const char *str);

#endif /* _APP_CHALLENGE_H_ */
