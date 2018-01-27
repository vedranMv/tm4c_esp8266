/**
 * espClient.cpp
 *
 *  Created on: Mar 4, 2017
 *      Author: Vedran
 */
#include "espClient.h"
#include "HAL/hal.h"
#include "libs/myLib.h"

#include <stdio.h>

//  Enable debug information printed on serial port
#define __DEBUG_SESSION__

#ifdef __DEBUG_SESSION__
#include "serialPort/uartHW.h"
#endif

///-----------------------------------------------------------------------------
///                      Class constructor & destructor                [PUBLIC]
///-----------------------------------------------------------------------------
_espClient::_espClient() : KeepAlive(true), _parent(0), _id(0) ,_alive(false)
{
    _Clear();
}

_espClient::_espClient(uint8_t id, ESP8266 *par)
    : KeepAlive(true), _parent(par), _id(id), _alive(true)
{
    _Clear();
}
_espClient::_espClient(const _espClient &arg)
    : KeepAlive(arg.KeepAlive), _parent(arg._parent), _id(arg._id), _alive(arg._alive)
{
    _Clear();
}

void _espClient::operator= (const _espClient &arg)
{
    _parent = arg._parent;
    _id = arg._id;
    _alive = arg._alive;
    _respRdy = arg._respRdy;
    KeepAlive = arg.KeepAlive;
    memcpy((void*)RespBody, (void*)(arg.RespBody), sizeof(RespBody));
}

///-----------------------------------------------------------------------------
///                      Client socket-manipulation                     [PUBLIC]
///-----------------------------------------------------------------------------

/**
 * Send data to a client over open TCP socket
 * @param buffer NULL-TERMINATED(!) data to send
 * @param bufferLen[optional] len of the buffer, if not provided function looks
 * for first occurrence of \0 in buffer and takes that as length
 * @return status of send process (binary or of ESP_* flags received while sending)
 */
uint32_t _espClient::SendTCP(char *buffer, uint16_t bufferLen)
{
    uint16_t bufLen = bufferLen;
    uint8_t numStr[6] = {0};

    //  If buffer length is not provided find it by looking for \0 char in string
    if (bufferLen == 0)
    {
        //  Dangerous, might end up in memory access violation
        while (buffer[bufLen++] != '\0');   //  Find \0
        bufLen--;   //Exclude \0 char from size of buffer
    }

    memset(_commBuf, 0, sizeof(_commBuf));
    strcat(_commBuf, "AT+CIPSEND=");
    itoa(_id, numStr);
    strcat(_commBuf, (char*)numStr);
    strcat(_commBuf, ",");
    memset(numStr, 0, sizeof(numStr));
    itoa(bufLen, numStr);
    strcat(_commBuf, (char*)numStr);

    if (_parent->_SendRAW(_commBuf, ESP_STATUS_RECV, 600))
    {
        _parent->flowControl = ESP_NO_STATUS;
        HAL_ESP_WDControl(true, 600);

        //  If ESP is not in server mode we need to manually start listening
        //  for incoming data from ESP
        if (!_parent->_servOpen)
            HAL_ESP_IntEnable(true);

        //  Write data we want to send
        _parent->_RAWPortWrite(buffer, bufLen);

        //  Listen for potential response
        while (_parent->flowControl == ESP_NO_STATUS);
    }
    //   Stop watchdog timer (started in ISR)
    //HAL_ESP_WDControl(false, 0);
    return _parent->flowControl;
}
/**
 * Read response from TCP socket(client) saved in internal buffer
 * Internal buffer with response is filled as soon as response is received in
 * an interrupt. This function only copies response from internal buffer to a
 * user provided one and then clears internal (and data-ready flag).
 * @param buffer pointer to user-provided buffer for incoming data
 * @param bufferLen used to return [buffer] size to user
 * @return true: if response was present and is copied into the provided buffer
 *        false: if no response is available
 */
bool _espClient::Receive(char *buffer, uint16_t *bufferLen)
{
    (*bufferLen) = 0;
    //  Check if there's new data received
    if (_respRdy)
    {
        //  Fill argument buffer
        while(RespBody[(*bufferLen)] != 0)
        {
            buffer[(*bufferLen)] = RespBody[(*bufferLen)];
            (*bufferLen)++;
        }

        //  Clear response body & flag
        _Clear();

        //  Check if it's supposed to stay open, if not force closing or schedule
        //  closing(preferred) of socket
        if (!KeepAlive)
        {
#if defined(__USE_TASK_SCHEDULER__)
            TaskScheduler::GetP()->SyncTask(ESP_UID, ESP_T_CLOSETCP, 0);
            TaskScheduler::GetP()->AddArgs(&_id, 1);
#else
            Close();
#endif
        }

        return true;
    }
    else return false;
}

/**
 * Check is socket has any new data ready for user
 * @note Used when manually reading response body from member variable to check
 * whether new data is available. If used, Done() MUST be called when done
 * processing data in response body. Alternative: use Receive() function instead
 * @return true: if there's new data from that socket
 *        false: otherwise
 */
bool _espClient::Ready()
{
    return _respRdy;
}

/**
 * Clear response body, flags and maintain socket alive if specified
 * @note Has to be called if user manually reads response body by reading member
 * variable directly, and not through Receive() function call
 */
void _espClient::Done()
{
    //  Clear response body & flag
    _Clear();
    //  Check if it's supposed to stay open, if not force closing or schedule
    //  closing(preferred) of socket
    if (!KeepAlive)
    {
#if defined(__USE_TASK_SCHEDULER__)
        TaskScheduler::GetP()->SyncTask(ESP_UID, ESP_T_CLOSETCP, 0);
        TaskScheduler::GetP()->AddArgs(&_id, 1);
#else
        Close();
#endif
    }
}

/**
 * Force closing TCP socket with the client
 * @note Object is deleted in ParseResponse function, once ESP confirms closing
 * @return status of close process (binary or of ESP_* flags received while closing)
 */
uint32_t _espClient::Close()
{
    uint8_t strNum[6] = {0};

    memset(_commBuf, 0, sizeof(_commBuf));
    strcat(_commBuf, "AT+CIPCLOSE=");
    itoa(_id, strNum);
    strcat(_commBuf, (char*)strNum);

    _alive = false;
    return _parent->_SendRAW(_commBuf);
}

/**
 * Clear response body and flag for response ready
 */
void _espClient::_Clear()
{
    memset((void*)RespBody, 0, sizeof(RespBody));
    RespLen = 0;
    _respRdy = false;
}
