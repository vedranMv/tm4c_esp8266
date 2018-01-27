/**
 * hal_esp_tm4c.h
 *
 *  Created on: Mar 4, 2017
 *      Author: Vedran Mikov
 *
 ****Hardware dependencies:
 *      UART7, pins PC4(Rx), PC5(Tx)
 *      GPIO PC6(CH_PD), PC7(Reset-not implemented!)
 *      Timer 6 - watchdog timer in case UART port hangs(likes to do so)
 */
#include <stdint.h>
#include <stdbool.h>


#if !defined(ROVERKERNEL_HAL_TM4C1294_HAL_ESP_TM4C_H_)
#define ROVERKERNEL_HAL_TM4C1294_HAL_ESP_TM4C_H_

//  This include is needed to provide definitions for macros below
#include "driverlib/uart.h"
#include "driverlib/rom_map.h"
#include "driverlib/rom.h"

/**     ESP8266 - related macros        */
#define ESP8266_UART_BASE       0x40013000

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * To prevent unnecessary stack layers (calling function from function) some
 * simpler functions are implemented using just macro definitions
 */
#define HAL_ESP_UARTBusy()      MAP_UARTBusy(ESP8266_UART_BASE)
#define HAL_ESP_SendChar(x)     MAP_UARTCharPut(ESP8266_UART_BASE, x)
#define HAL_ESP_CharAvail()     MAP_UARTCharsAvail(ESP8266_UART_BASE)
#define HAL_ESP_GetChar()       MAP_UARTCharGetNonBlocking(ESP8266_UART_BASE)


extern uint32_t    HAL_ESP_InitPort(uint32_t baud);
extern void        HAL_ESP_RegisterIntHandler(void((*intHandler)(void)));
extern void        HAL_ESP_HWEnable(bool enable);
extern bool        HAL_ESP_IsHWEnabled();
extern void        HAL_ESP_IntEnable(bool enable);
extern int32_t     HAL_ESP_ClearInt();
//extern bool        HAL_ESP_CharAvail();
//extern char        HAL_ESP_GetChar();
extern void        HAL_ESP_InitWD(void((*intHandler)(void)));
extern void        HAL_ESP_WDControl(bool enable, uint32_t timeout);
extern void        HAL_ESP_WDClearInt();

#ifdef __cplusplus
}
#endif


#endif /* ROVERKERNEL_HAL_TM4C1294_HAL_ESP_TM4C_H_ */
