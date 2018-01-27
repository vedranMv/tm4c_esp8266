/**
 * espClient.h
 *
 *  Created on: Mar 4, 2017
 *      Author: Vedran Mikov
 */

#ifndef ROVERKERNEL_ESP8266_ESPCLIENT_H_
#define ROVERKERNEL_ESP8266_ESPCLIENT_H_

//  Define class prototype
class _espClient;


#include "esp8266.h"


/**
 * _espClient class - wrapper for TCP client connected to ESP server
 */
class _espClient
{
    friend class    ESP8266;
    friend void     UART7RxIntHandler(void);
    public:
        _espClient();
        _espClient(uint8_t id, ESP8266 *par);
        _espClient(const _espClient &arg);

        void        operator= (const _espClient &arg);

        uint32_t    SendTCP(char *buffer, uint16_t bufferLen = 0);
        bool        Receive(char *buffer, uint16_t *bufferLen);
        bool        Ready();
        void        Done();
        uint32_t    Close();

        //  Keep socket alive (don't terminate it after first round of communication)
        volatile bool       KeepAlive;
        //  Buffer for data received on this socket
        volatile char       RespBody[1024];
        volatile uint16_t   RespLen;

    private:
        void        _Clear();

        //  Pointer to a parent device of of this client
        ESP8266         *_parent;
        //  Socket ID of this client, as returned by ESP
        uint8_t         _id;
        //  Specifies whether the socket is alive
        volatile bool   _alive;
        //  Specifies whether there's a response from this client ready to read
        volatile bool   _respRdy;
};

#endif /* ROVERKERNEL_ESP8266_ESPCLIENT_H_ */
