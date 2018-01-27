/**
 * hal_esp_tm4c.c
 *
 *  Created on: Mar 4, 2017
 *      Author: Vedran
 */
#include "hal_esp_tm4c.h"



#include "libs/myLib.h"
#include "HAL/tm4c1294/hal_common_tm4c.h"

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_timer.h"
#include "inc/hw_gpio.h"

#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "utils/uartstdio.h"
#include "driverlib/systick.h"
#include "driverlib/timer.h"

/**
 * Initialize UART port communicating with ESP8266 chip - 8 data bits, no parity,
 * 1 stop bit, no flow control
 * @param baud designated speed of communication
 * @return HAL library error code
 */
uint32_t HAL_ESP_InitPort(uint32_t baud)
{
    static bool pinInit = false;

    //  Prevents reinitializing the pins every time a baud rate is updated
    if (!pinInit)
    {
        //  Configure HW pins for UART and on/off signal
        MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
        MAP_GPIOPinConfigure(GPIO_PC4_U7RX);
        MAP_GPIOPinConfigure(GPIO_PC5_U7TX);
        MAP_GPIOPinTypeUART(GPIO_PORTC_BASE, GPIO_PIN_4 | GPIO_PIN_5);

        MAP_GPIOPinTypeGPIOOutput(GPIO_PORTC_BASE, GPIO_PIN_6);
        MAP_GPIOPinWrite(GPIO_PORTC_BASE, GPIO_PIN_6, 0x00);

        MAP_GPIOPinTypeGPIOOutput(GPIO_PORTC_BASE, GPIO_PIN_7);
        MAP_GPIOPinWrite(GPIO_PORTC_BASE, GPIO_PIN_7, 0xFF);

        pinInit = true;
    }

    //    Configure UART 7 peripheral used for ESP communication
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART7);
    MAP_SysCtlPeripheralReset(SYSCTL_PERIPH_UART7);
    MAP_UARTClockSourceSet(ESP8266_UART_BASE, UART_CLOCK_SYSTEM);
    MAP_UARTConfigSetExpClk(ESP8266_UART_BASE, g_ui32SysClock, baud,
                        (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                         UART_CONFIG_PAR_NONE));
    MAP_UARTEnable(ESP8266_UART_BASE);
    HAL_DelayUS(50000);    //  50ms delay after configuring

    return HAL_OK;
}

/**
 * Attach specific interrupt handler to ESP's UART and configure interrupt to
 * occur on every received character
 */
void HAL_ESP_RegisterIntHandler(void((*intHandler)(void)))
{
    MAP_UARTDisable(ESP8266_UART_BASE);
    //  Enable Interrupt on received data
    MAP_UARTFIFOLevelSet(ESP8266_UART_BASE,UART_FIFO_TX1_8, UART_FIFO_RX1_8 );
    UARTIntRegister(ESP8266_UART_BASE, intHandler);
    MAP_UARTIntEnable(ESP8266_UART_BASE, UART_INT_RX | UART_INT_RT);
    MAP_IntDisable(INT_UART7);
    MAP_UARTEnable(ESP8266_UART_BASE);
}

/**
 * Enable or disable ESP chip by toggling a pin connected to CH_PD
 * @param enable is state of device
 */
void HAL_ESP_HWEnable(bool enable)
{
    ///    After both actions add a delay to allow chip to settle
    if (!enable)
    {
        MAP_GPIOPinWrite(GPIO_PORTC_BASE, GPIO_PIN_6, 0);
        HAL_DelayUS(1000000);
    }
    else
    {
        MAP_GPIOPinWrite(GPIO_PORTC_BASE, GPIO_PIN_6, 0xFF);
        HAL_DelayUS(2000000);
    }
}

/**
 * Check whether the chip is enabled or disabled
 */
bool HAL_ESP_IsHWEnabled()
{
    return ((MAP_GPIOPinRead(GPIO_PORTC_BASE, GPIO_PIN_6) & GPIO_PIN_6) > 0);
}

/**
 * Enable/disable UART interrupt - interrupt occurs on every char received
 * @param enable
 */
void HAL_ESP_IntEnable(bool enable)
{
    if (enable) MAP_IntEnable(INT_UART7);
    else MAP_IntDisable(INT_UART7);
}

/**
 * Clear all interrupt flags when an interrupt occurs
 */
int32_t HAL_ESP_ClearInt()
{
    uint32_t retVal = MAP_UARTIntStatus(ESP8266_UART_BASE, true);
    //  Clear all raised interrupt flags
    MAP_UARTIntClear(ESP8266_UART_BASE, retVal);
    return retVal;
}

/**
 * Watchdog timer for ESP module - used to reset protocol if communication hangs
 * for too long.
 */
void((*g_intHandler)(void));
void HAL_ESP_InitWD(void((*intHandler)(void)))
{
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER6);
    MAP_SysCtlPeripheralReset(SYSCTL_PERIPH_TIMER6);
    MAP_TimerConfigure(TIMER6_BASE, TIMER_CFG_ONE_SHOT_UP);
    TimerIntRegister(TIMER6_BASE, TIMER_A, intHandler);
    MAP_TimerIntEnable(TIMER6_BASE, TIMER_TIMA_TIMEOUT);
    MAP_IntEnable(INT_TIMER6A);
    g_intHandler = intHandler;
}

/**
 * On/Off control for WD timer
 * @param enable desired state of timer (true-run/false-stop)
 * @param ms time in millisec. after which the communication is interrupted
 */
void HAL_ESP_WDControl(bool enable, uint32_t ms)
{
    //  Record last value for timeout, use it when timeout argument is 0
    static uint32_t LTM;
    HAL_DelayUS(2);
    MAP_TimerDisable(TIMER6_BASE, TIMER_A);

    if (enable)
    {
        if (ms != 0)
        {
            HAL_ESP_InitWD(g_intHandler);
            MAP_TimerLoadSet(TIMER6_BASE, TIMER_A, _TM4CMsToCycles(ms));
            LTM = ms;

        }
        else
            MAP_TimerLoadSet(TIMER6_BASE, TIMER_A, _TM4CMsToCycles(LTM));
        MAP_TimerEnable(TIMER6_BASE, TIMER_A);
    }
    else if (ms != 0)
    {
        LTM = ms;
    }
}

/**
 * Clear interrupt flag of WD timer and manually set trigger the UART interrupt
 * used to capture Rx data from ESP.
 * Clarification: This function is called within ISR provided as an argument in
 * initialization of WD timer. That ISR will set a signal for UART Rx ISR, to
 * notify it that time is up and communication is terminated. In order to prevent
 * calling UART ISR manually here is used a property of NVIC that allows to
 * manually trigger an interrupt that will be executed as soon as processor
 * leaves this (watchdog) interrupt. (Because there can be no interrupt within
 * an interrupt)
 */
void HAL_ESP_WDClearInt()
{
    MAP_TimerIntClear(TIMER6_BASE, MAP_TimerIntStatus(TIMER6_BASE, true));
    MAP_TimerDisable(TIMER6_BASE, TIMER_A);

    MAP_IntPendSet(INT_UART7);
}

