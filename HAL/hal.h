/**
 *  hal.h
 *
 *  Created on: 02. 03. 2017.
 *      Author: Vedran Mikov
 *
 *  Hardware abstraction layer providing uniform interface between board support
 *  layer and hardware in there and any higher-level libraries. Acts as a
 *  switcher between HALs for different boards.
 */

#ifndef __HAL_H__
#define __HAL_H__


    #include "tm4c1294/hal_common_tm4c.h"
    #include "tm4c1294/hal_esp_tm4c.h"

#endif  /* __HAL_H__ */
