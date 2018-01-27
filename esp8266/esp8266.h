/**
 *	esp8266.h
 *
 *  Created on: 1. 8. 2016.
 *      Author: Vedran Mikov
 *
 *  ESP8266 WiFi module communication library
 *  @version 1.4.5
 *  V1.1.4
 *  +Connect/disconnect from AP, get acquired IP as string/int
 *	+Start TCP server and allow multiple connections, keep track of
 *	    connected clients
 *  +Parse ESP replies and get status of commands
 *  +Parse incoming data from a TCP socket and allow for hooking user routines
 *      to manipulate received socket data
 *  +Send data over TCP socket to a client connected to the TCP server of ESP
 *  V1.2 - 25.1.2017
 *  +When in server mode can reply back to clients over TCP socket
 *  +On initialization library registers its services as a kernel module in task
 *  scheduler
 *  V1.2.1 - 26.1.2017
 *  +Implemented watchdog(WD) timer with variable timeout to handle any blockage
 *  in communication (ESP sometimes not consistent with sending msg terminator)
 *  +Establish a connection to TCP server (uses existing _espClient class)
 *  V1.3 - 31.1.2017
 *  +Integration of library with task scheduler
 *  V1.3.1 - 6.2.2017
 *  +Modified to support Task scheduler v2.3
 *  V1.3.2
 *  +Changed ESP8266 class into a singleton
 *  V1.4.1 - 25.3.2017
 *  +All snprintf functions are replaced with a series of strcat. This is because
 *  *printf function occupies over 1.3kB of stack greatly limiting maximum stack
 *  depth("number of functions called from functions") eventually crashing program
 *  +Vector holding opened sockets(clients) replaced by an array of dynamically
 *  allocated clients. When client is 'deleted' (socket closed) its pointer in
 *  the array is set to 0. Other logic remains the same
 *  +Kicking the dog is done on every received character, instead of every start
 *  of the interrupt. It was noted that ISR sometimes receives more than 1 char
 *  in a single call which causes watchdog to interrupt receiving if called once
 *  per ISR.
 *  V1.4.2 - 28.3.2017
 *  +Fixed small buffer size when passing received data to the hooked function
 *  from ISR when not using task scheduler
 *  V1.4.3 - 2.7.2017
 *  +Change include paths for better portability, new way of printing to debug
 *  +Integration with event logger
 *  V1.4.4 - 7.7.2017
 *  +Added support for rebooting module from task scheduler
 *  +AP connecting can be made as non-blocking call, member variable 'wifiStatus'
 *  holds current status of connection. However, attempting to open a socket
 *  while not connected still emits error event; check if connected before
 *  opening socket from higher level modules!
 *  +Stability improvements, different placement of watchdog resets
 *  V1.4.5 - 2.9.2017
 *  +Bugfix in parser, fixed problem with multiple sockets closing at the same time
 *
 *  TODO:Add interface to send UDP packet
 */
#include <stdint.h>
#include <stdbool.h>

//  Compile following section only if hwconfig.h says to include this module
#if !defined(ESP8266_H_)
#define ESP8266_H_

//  Define class prototype
class ESP8266;
//  Shared buffer for ESP library to assemble text requests in (declared extern
//  because it's shared with espClient library
extern char _commBuf[2048];

//  Include client library
#include "espClient.h"

/*		Communication settings	 	*/
#define ESP_DEF_BAUD			1000000

/*		ESP8266 error codes		*/
#define ESP_STATUS_LENGTH		13
#define ESP_NO_STATUS			0
#define ESP_STATUS_OK			1<<0
#define ESP_STATUS_BUSY			1<<1
#define ESP_RESPOND_SUCC		1<<2
#define ESP_NONBLOCKING_MODE	1<<3
#define ESP_STATUS_CONNECTED	1<<4
#define ESP_STATUS_DISCN        1<<5
#define ESP_STATUS_READY		1<<6
#define ESP_STATUS_SOCKOPEN     1<<7
#define ESP_STATUS_SOCKCLOSE	1<<8
#define ESP_STATUS_RECV			1<<9
#define ESP_STATUS_FAIL			1<<10
#define ESP_STATUS_SENDOK		1<<11
#define ESP_STATUS_ERROR		1<<12
#define ESP_NORESPONSE          1<<13
#define ESP_STATUS_IPD          1<<14
#define ESP_GOT_IP              1<<15

#define ESP_WIFI_NONE           0
#define ESP_WIFI_CONNECTING     1
#define ESP_WIFI_CONNECTED      2

//  Max number of clients allowed by ESP8266
#define ESP_MAX_CLI     5

/**
 * ESP8266 class definition
 * Object provides a high-level interface to the ESP chip. Allows basic AP func.,
 * setting up a TCP server and managing open sockets. Socket creation/deletion
 * handled internally by interpreting status messages received from ESP. Class
 * _espClient provides direct interface to opened TCP sockets which can be accessed
 * from ESP8266::_clients vector
 */
class ESP8266
{
    /// Functions & classes needing direct access to all members
    friend class    _espClient;
    friend void     UART7RxIntHandler(void);
    friend void     _ESP_KernelCallback(void);
	public:
        //  Functions for returning static instance
        static ESP8266& GetI();
        static ESP8266* GetP();
        //  Functions for configuring ESP8266
		uint32_t    InitHW(int32_t baud = ESP_DEF_BAUD);
        void        Enable(bool enable);
        bool        IsEnabled();
        void        AddHook(void((*funPoint)(const uint8_t, const uint8_t*,
                                             const uint16_t)));
		//  Functions used with access points
		uint32_t    ConnectAP(char* APname, char* APpass, bool nonBlocking=false);
		bool        IsConnected();
		uint32_t    DisconnectAP();
		uint32_t    MyIP();
		//  Functions related to TCP server
		uint32_t    StartTCPServer(uint16_t port);
		uint32_t    StopTCPServer();
		bool        ServerOpened();
		void        TCPListen(bool enable);
		//  Functions to interface opened TCP sockets (clients)
		_espClient* GetClientByIndex(uint8_t index);
		_espClient* GetClientBySockID(uint8_t id);
		//  Functions related to TCP clients(sockets)
		uint32_t    OpenTCPSock(char *ipAddr, uint16_t port,
		                        bool keepAlive=true, uint8_t sockID = 9);
		bool        ValidSocket(uint8_t id);
		uint32_t    Send(const char* arg, ...) { return ESP_NO_STATUS; }
		//  Miscellaneous functions
		uint32_t 	ParseResponse(char* rxBuffer, uint16_t rxLen);
uint32_t	_SendRAW(const char* txBuffer, uint32_t flags = 0,
		                     uint32_t timeout = 250);//150
		//  Status variable for error codes returned by ESP
		volatile uint32_t	flowControl;
		//  Status of connecting to AP
		volatile uint32_t    wifiStatus;

	protected:
        ESP8266();
        ~ESP8266();
        ESP8266(ESP8266 &arg) {}                //  No definition - forbid this
        void operator=(ESP8266 const &arg) {}   //  No definition - forbid this

		bool        _InStatus(const uint32_t status, const uint32_t flag);

		void        _RAWPortWrite(const char* buffer, uint16_t bufLen);
		void	    _FlushUART();
		uint32_t    _IPtoInt(char *ipAddr);
		uint8_t     _IDtoIndex(uint8_t sockID);

        //  Hook to user routine called when data from socket is received
        void    ((*custHook)(const uint8_t, const uint8_t*, const uint16_t));
		//  IP address in decimal and string format
		uint32_t    _ipAddress;
		char        _ipStr[16];
		//  TCP server port
		uint16_t    _tcpServPort;
		//  Specifies whether the TCP server is currently running
		bool        _servOpen;
		//  List of all opened sockets (clients) currently communicating with
		//  ESP. It's important that pointers itself are volatile, not _espClient
		//  object because pointers get changed within ISR. Array index is socket ID!
		_espClient volatile *_clients[ESP_MAX_CLI];
		//  Interface with task scheduler - provides memory space and function
		//  to call in order for task scheduler to request service from this module
#if defined(__USE_TASK_SCHEDULER__)
		_kernelEntry _espKer;
#endif
};


#endif /* ESP8266_H_ */
