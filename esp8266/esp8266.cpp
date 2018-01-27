/**
 * esp8266.cpp
 *
 *  Created on: 1. 8. 2016.
 *      Author: Vedran Mikov
 */
#include "esp8266.h"


#include "libs/myLib.h"
#include "HAL/hal.h"

#include <stdio.h>
#include <ctype.h>

//  Enable debug information printed on serial port
//#define __DEBUG_SESSION__

//  Integration with event log, if it's present
#ifdef __HAL_USE_EVENTLOG__
    #include "init/eventLog.h"
    //  Simplify emitting events
    #define EMIT_EV(X, Y)  EventLog::EmitEvent(ESP_UID, X, Y)
#endif  /* __HAL_USE_EVENTLOG__ */

#ifdef __DEBUG_SESSION__
#include "serialPort/uartHW.h"
#endif

//  Function prototype for an interrupt handler (declared at the bottom)
void UART7RxIntHandler(void);

//  Buffer used to assemble commands (shared between all functions )
//  2048 is max allowed length for a continuous stream ESP can handle
char _commBuf[2048];


#if defined(__USE_TASK_SCHEDULER__)
/**
 * Callback routine to invoke service offered by this module from task scheduler
 * @note It is assumed that once this function is called task scheduler has
 * already copied required variables into the memory space provided for it.
 */
void _ESP_KernelCallback(void)
{
    //  Grab a pointer to singleton
    //ESP8266 *__esp = ESP8266::GetP();
    ESP8266 &__esp = ESP8266::GetI();

    //  Check for null-pointer
    if (__esp._espKer.argN == 0)
        return;
    /*
     *  Data in args[] contains bytes that constitute arguments for function
     *  calls. The exact representation(i.e. whether bytes represent ints, floats)
     *  of data is known only to individual blocks of switch() function. There
     *  is no predefined data separator between arguments inside args[].
     */
    switch (__esp._espKer.serviceID)
    {
    /*
     * Start/Stop control for TCP server
     * 1st data byte of args[] is either 0(stop) or 1(start). Following bytes
     * 2 & 3 contain uint16_t value of port at which to start server
     * args[] = enable(1B)|port(2B)
     * retVal ESP library status code
     */
    case ESP_T_TCPSERV:
        {
            if (__esp._espKer.args[0] == 1)
            {
                uint16_t port;
                memcpy((void*)&port, (void*)(__esp._espKer.args + 1), 2);
                __esp._espKer.retVal = __esp.StartTCPServer(port);
                __esp.TCPListen(true);
            }
            else
            {
                __esp._espKer.retVal = __esp.StopTCPServer();
                __esp.TCPListen(false);
            }
            __esp._espKer.retVal = ESP_STATUS_OK;
        }
        break;
    /*
     * Connect to TCP client on given IP address and port
     * args[] = KeepAlive(1B)|IPaddress(7B-15B)|port(2B)|socketID(1B)
     * retVal ESP library error code (if > 5); or socket ID (if <=5)
     */
    case ESP_T_CONNTCP:
        {
            char ipAddr[15] = {0};
            uint16_t port;
            uint8_t sockID;
            //  IP address starts on 2nd data byte and its a string of length
            //  equal to total length of data - 4bytes(port,KA,socketID)
            memcpy( (void*)ipAddr,
                    (void*)(__esp._espKer.args + 1),
                    __esp._espKer.argN - 4);
            //  Port is 3rd and 2nd byte from the back
            memcpy( (void*)&port,
                    (void*)(__esp._espKer.args + (__esp._espKer.argN-3)),
                    2);
            //  socketID is last byte
            sockID =  __esp._espKer.args[__esp._espKer.argN-1];
            //  If IP address is valid process request
            if (__esp._IPtoInt(ipAddr) == 0)
                return;
            //  1st data byte is keep alive flag
            //  Double negation to convert any integer !=0 into boolean
            bool KA = !(!__esp._espKer.args[0]);
            __esp._espKer.retVal = __esp.OpenTCPSock(ipAddr, port, KA, sockID);
        }
        break;
    /*
     * Send message to specific TCP client
     * args[] = socketID(1B)|message|
     */
    case ESP_T_SENDTCP:
        {
            //  Check if socket ID is valid
            if (!__esp.ValidSocket(__esp._espKer.args[0]))
               return;
            //  Ensure that message is null-terminated
            __esp._espKer.args[__esp._espKer.argN] = '\0';
            //  Initiate TCP send to required client
            __esp._espKer.retVal = __esp.GetClientBySockID(__esp._espKer.args[0])
                                      ->SendTCP((char*)(__esp._espKer.args+1));
        }
        break;
    /*
     * Receive data from an opened socket and pass it to user-defined routine
     * args[] = socketID(1B)
     */
    case ESP_T_RECVSOCK:
        {
            _espClient  *cli;
            //  Check if socket ID is valid
            if (!__esp.ValidSocket(__esp._espKer.args[0]))
                return;
            cli = __esp.GetClientBySockID(__esp._espKer.args[0]);
            __esp.custHook(__esp._espKer.args[0],
                            (uint8_t*)(cli->RespBody),
                            (uint16_t)((cli->RespLen)));
            __esp._espKer.retVal = ESP_STATUS_OK;
        }
        break;
    /*
     * Close socket with specified ID
     * args[] = socketID(1B)
     */
    case ESP_T_CLOSETCP:
        {
            //  Check if socket ID is valid
            if (!__esp.ValidSocket(__esp._espKer.args[0]))
                return;
            //  Initiate socket closing from client object
            __esp._espKer.retVal = __esp.GetClientBySockID(__esp._espKer.args[0])
                                              ->Close();
        }
        break;
    case ESP_T_REBOOT:
        {
            //  Set initial error status
            __esp._espKer.retVal = ESP_STATUS_ERROR;
            //  Reboot only if 0x17 was sent as argument
            if (__esp._espKer.args[0] != 0x17)
                return;
            //  Start by closing all opened sockets
            for (uint8_t i = 0; i < ESP_MAX_CLI; i++)
                if (__esp._clients[i] != 0)
                    ((_espClient*)__esp._clients[i])->Close();
            //  Power down ESP chip
            __esp.Enable(false);
#ifdef __HAL_USE_EVENTLOG__
            EMIT_EV(__esp._espKer.serviceID, EVENT_UNINITIALIZED);
#endif  /* __HAL_USE_EVENTLOG__ */
            //  Rerun initialization sequence
            __esp._espKer.retVal = __esp.InitHW();
            __esp.wifiStatus = ESP_WIFI_CONNECTING;

            //  ESP is now connecting to AP on its own, based on data stored in
            //  its flash memory. Result is picked up through ISR asynchronously
        }
        break;
    case ESP_T_PARSE:
        {

        }
        break;
    default:
        break;
    }

    //  Report outcome to event logger
#ifdef __HAL_USE_EVENTLOG__
    if ((__esp._espKer.retVal & ESP_STATUS_OK) > 0)
        EMIT_EV(__esp._espKer.serviceID, EVENT_OK);
    else
        EMIT_EV(__esp._espKer.serviceID, EVENT_ERROR);
#endif  /* __HAL_USE_EVENTLOG__ */
}
#endif  /* __USE_TASK_SCHEDULER__ */

/**
 * Routine invoked by watchdog timer on timeout
 * Sets global status for current communication to "error", clears WD interrupt
 * flag and artificially produces ESP's interrupt to process any remaining data
 * in the receiving buffer before communication got blocked
 */
void ESPWDISR()
{
    ESP8266::GetI().flowControl = ESP_STATUS_ERROR;

#ifdef __HAL_USE_EVENTLOG__
    EMIT_EV(-1, EVENT_HANG);
#endif  /* __HAL_USE_EVENTLOG__ */

    HAL_ESP_WDClearInt();
}

///-----------------------------------------------------------------------------
///         Functions for returning static instance                     [PUBLIC]
///-----------------------------------------------------------------------------

/**
 * Return reference to a singleton
 * @return reference to an internal static instance
 */
ESP8266& ESP8266::GetI()
{
    static ESP8266 singletonInstance;
    return singletonInstance;
}

/**
 * Return pointer to a singleton
 * @return pointer to a internal static instance
 */
ESP8266* ESP8266::GetP()
{
    return &(ESP8266::GetI());
}

///-----------------------------------------------------------------------------
///         Functions for configuring ESP8266                           [PUBLIC]
///-----------------------------------------------------------------------------

/**
 * Initialize hardware used to communicate with ESP module, watchdog timer used
 * to handle any blockage in communication and (if using task scheduler)
 * register kernel module so TS can make calls to this library.
 * @param baud baud-rate used in serial communication between ESP and hardware
 * @return error code, depending on the outcome
 */
uint32_t ESP8266::InitHW(int32_t baud)
{
    uint32_t retVal;

#ifdef __HAL_USE_EVENTLOG__
    EMIT_EV(-1, EVENT_STARTUP);
#endif  /* __HAL_USE_EVENTLOG__ */

    HAL_ESP_InitPort(baud);
    HAL_ESP_RegisterIntHandler(UART7RxIntHandler);
    HAL_ESP_InitWD(ESPWDISR);

    //    Turn ESP8266 chip ON
    Enable(true);

    //  Send test command(AT) then turn off echoing of commands(ATE0)
    retVal = _SendRAW("AT\0");
    retVal = _SendRAW("ATE0\0");

    //  Allow for multiple connections
    retVal |= _SendRAW("AT+CIPMUX=1\0");

    //  Reset internal parameters
    _ipAddress = 0;
    memset(_ipStr, 0, sizeof(_ipStr));
    _servOpen = false;
    _tcpServPort = 0;
    wifiStatus = ESP_WIFI_NONE;
    for (uint8_t i = 0; i < ESP_MAX_CLI; i++)
        _clients[i] = 0;

#if defined(__USE_TASK_SCHEDULER__)
    //  Register module services with task scheduler
    _espKer.callBackFunc = _ESP_KernelCallback;
    TS_RegCallback(&_espKer, ESP_UID);
#endif  /* __USE_TASK_SCHEDULER__ */

#ifdef __HAL_USE_EVENTLOG__
    EMIT_EV(-1, EVENT_INITIALIZED);
#endif  /* __HAL_USE_EVENTLOG__ */

    return retVal;
}

/**
 * Enable/disable ESP8266 chip (by controlling CH_PD pin)
 * @param enable new state of chip to set
 */
void ESP8266::Enable(bool enable)
{
    HAL_ESP_HWEnable(enable);

    //  If enabling the chip, wait 70ms it's started
    if (enable)
        HAL_DelayUS(70000);
}

/**
 * Check if ESP chip is enabled (CH_PD pin pulled high)
 * @return true if chip is enabled, false if not
 */
bool ESP8266::IsEnabled()
{
    return HAL_ESP_IsHWEnabled();
}

/**
 * Register hook to user function
 * Register hook to user-function called every time new data from TCP/UDP client
 * is received. Received data is passed as an argument to hook function together
 * with socket ID through which response came in
 * @param funPoint pointer to void function with 3 arguments
 */
void ESP8266::AddHook(void((*funPoint)(const uint8_t, const uint8_t*, const uint16_t)))
{
    custHook = funPoint;
}

///-----------------------------------------------------------------------------
///                  Functions used with access points                  [PUBLIC]
///-----------------------------------------------------------------------------

/**
 * Connected to AP using provided credentials
 * @param APname name of AP to connect to
 * @param APpass password of AP connecting to
 * @param nonBlocking Doesn't wait for connection event, but picks it up async.
 * as it comes through interrupt
 * @return error code, depending on the outcome
 */
uint32_t ESP8266::ConnectAP(char* APname, char* APpass, bool nonBlocking)
{
    int8_t retVal = ESP_NO_STATUS;

    //  Set ESP in client mode
    retVal = _SendRAW("AT+CWMODE_DEF=1\0");
    if (!_InStatus(retVal, ESP_STATUS_OK)) return retVal;

    wifiStatus = ESP_WIFI_CONNECTING;

    //  Assemble command & send it
    memset((void*)_commBuf, 0, sizeof(_commBuf));
    strcat(_commBuf, "AT+CWJAP_DEF=\"");
    strcat(_commBuf, APname);
    strcat(_commBuf, "\",\"");
    strcat(_commBuf, APpass);
    strcat(_commBuf, "\"\0");

    //  Use standard send function but increase timeout to 6s as acquiring IP
    //  address might take time
    retVal = _SendRAW(_commBuf, (nonBlocking?ESP_NONBLOCKING_MODE:0), 16000);
    if (!_InStatus(retVal, ESP_STATUS_OK)) return retVal;
    wifiStatus = ESP_WIFI_CONNECTED;

    //  Read acquired IP address and save it locally
    MyIP();

    return retVal;
}

/**
 * Check if ESP is connected to AP (if it acquired IP address)
 * @return true: if it's connected,
 *        false: otherwise
 */
bool ESP8266::IsConnected()
{
    return (_ipAddress > 0);
}

/**
 * Disconnect from WiFi Access point
 * @return
 */
uint32_t ESP8266::DisconnectAP()
{
    //  Release internal IP address
    memset(_ipStr, 0, sizeof(_ipStr));
    _ipAddress = 0;

    return _SendRAW("AT+CWQAP\0");
}

/**
 * Get IP address assigned to device when connected to AP. IP address is saved
 * to private member variable from within UART ISR
 * @return IP address of ESP in integer form
 */
uint32_t ESP8266::MyIP()
{
    if (_ipAddress == 0) _SendRAW("AT+CIPSTA\?\0");

    return _ipAddress;
}

///-----------------------------------------------------------------------------
///                      Functions related to TCP server                [PUBLIC]
///-----------------------------------------------------------------------------

/**
 * Initiate TCP server on given port number
 * @param port at which to start listening for incoming connections
 * @return error code, depending on the outcome
 */
uint32_t ESP8266::StartTCPServer(uint16_t port)
{
    int8_t retVal = ESP_STATUS_OK;
    uint8_t portStr[6] = {0};

    //  Start TCP server, in case of error return
    memset(_commBuf, 0, sizeof(_commBuf));
    strcat(_commBuf, "AT+CIPSERVER=1,");
    itoa(port, portStr);
    strcat(_commBuf, (char*)portStr);

    retVal = _SendRAW(_commBuf);
    if (!_InStatus(retVal, ESP_STATUS_OK)) return retVal;

    _tcpServPort = port;
    _servOpen = true;

    //  Set TCP connection timeout to 0, in case of error return
    retVal = _SendRAW("AT+CIPSTO=0\0");
    if (!_InStatus(retVal, ESP_STATUS_OK)) return retVal;

    return retVal;
}

/**
 * Stop TCP server running on ESP
 * @return error code depending on the outcome
 */
uint32_t ESP8266::StopTCPServer()
{
    int8_t retVal = ESP_STATUS_OK;

    //  Stop TCP server, in case of error return
    retVal = _SendRAW("AT+CIPSERVER=0");
    if (!_InStatus(retVal, ESP_STATUS_OK)) return retVal;

    _servOpen = false;
    _tcpServPort = 0;
    return retVal;
}

/**
 * Check if TCP server is running
 * @return true: server is running,
 *        false: not
 */
bool ESP8266::ServerOpened()
{
    return _servOpen;
}

/**
 * Configure listening for incoming data when running server mode or waiting for
 * response on an opened TCP socket
 * @param enable set true for initiating listening, false otherwise
 */
void ESP8266::TCPListen(bool enable)
{
    HAL_ESP_IntEnable(enable);
    _servOpen = enable;
}

///-----------------------------------------------------------------------------
///                      Functions related to TCP clients               [PUBLIC]
///-----------------------------------------------------------------------------

/**
 * Open TCP socket to a client at specific IP and port, keep alive interval 7.2s
 * @param ipAddr string containing IP address of server(null-terminated)
 * @param port TCP socket port of server
 * @param keepAlive[optional] whether to maintain the socket open or drop after
 * first transfer
 * @param sockID[optional] desired socket ID to assign to this connection, if
 * not specified smallest free ID is used
 * @return On success socket ID of TCP client in _client vector,
 *         On failure ESP_STATUS_ERROR error code
 */
uint32_t ESP8266::OpenTCPSock(char *ipAddr, uint16_t port,
                              bool keepAlive, uint8_t sockID)
{
    uint32_t retVal;
    uint8_t strNum[6] = {0};

    //  Can't continue if ESP is not connected
    if (wifiStatus != ESP_WIFI_CONNECTED)
        return ESP_STATUS_ERROR;

    //  Check if socket with this ID already exists, if not create it, if yes
    //  fined first free socket ID and use it instead
    if (GetClientBySockID(sockID) != 0 || (sockID >= ESP_MAX_CLI))
    {
        //  Find free socket number (0-(ESP_MAX_CLI-1) supported)
        for (sockID = 0; sockID <= ESP_MAX_CLI; sockID++)
            if (_clients[sockID] == 0)
                break;
        //  If loop hit ESP_MAX_CLI there are no free sockets, return error code
        if (sockID >= ESP_MAX_CLI)
            return ESP_STATUS_ERROR;
    }

    //  Assemble command: Open TCP socket to specified IP and port, set
    //  keep alive interval to 7200ms
    memset(_commBuf, 0, sizeof(_commBuf));
    strcat(_commBuf, "AT+CIPSTART=");
    itoa(sockID, strNum);
    strcat(_commBuf, (char*)strNum);
    strcat(_commBuf, ",\"TCP\",\"");
    strcat(_commBuf, ipAddr);
    strcat(_commBuf, "\",");
    memset(strNum, 0, sizeof(strNum));
    itoa(port, strNum);
    strcat(_commBuf, (char*)strNum);
    strcat(_commBuf, ",7200\0");

    //  Execute command and check outcome
    retVal = _SendRAW(_commBuf);
    if (_InStatus(retVal, ESP_STATUS_OK) && !_InStatus(retVal, ESP_STATUS_ERROR))
    {
        //  If success, start listening for potential incoming data from server
        TCPListen(true);
        GetClientBySockID(sockID)->KeepAlive = keepAlive;
        retVal = sockID;
    }

    return retVal;
}

/**
 * Check if socket with the specified [id] is open (alive)
 * @param id ID of the socket to check
 * @return alive status of particular socket
 */
bool ESP8266::ValidSocket(uint8_t id)
{
    return (GetClientBySockID(id) != 0);
}

/**
 * Get pointer to client object based on specified [index] in _client vector
 * @param index desired index of client to get
 * @return pointer to ESP client object under given index (if it exists, if not
 *          NULL pointer(0) returned)
 */
_espClient* ESP8266::GetClientByIndex(uint8_t index)
{
    if (ESP_MAX_CLI > index)
        return const_cast<_espClient*>(_clients[index]);
    else
        return 0;
}

/**
 * Get pointer to client object having specified socket [id]
 * @param id socket id as returned by ESP on opened socket
 * @return pointer to ESP client object under given id (if it exists, if not
 *          NULL pointer(0) returned)
 */
_espClient* ESP8266::GetClientBySockID(uint8_t id)
{
    return GetClientByIndex(id);
}

///-----------------------------------------------------------------------------
///                      Miscellaneous functions                        [PUBLIC]
///-----------------------------------------------------------------------------

/**
 * ESP reply message parser
 * Checks ESP reply stream for commands, actions and events and updates global
 * variables accordingly.
 * @param rxBuffer string containing reply message from ESP
 * @param rxLen length of [rxBuffer] string
 * @return bitwise OR of all statuses(ESP_STATUS_*) found in the message string
 */
uint32_t ESP8266::ParseResponse(char* rxBuffer, uint16_t rxLen)
{
    //  Return value
    uint32_t retVal = ESP_NO_STATUS;
    //  Flags to be set when the parameter has been found
    int16_t ipFlag= -1, respFlag = -1, sockOflag = -1, sockCflag = -1;
    //  Pointer to char, used to temporary store return value of strstr function
    char *retTemp;

    //  If message is empty return here
    if (rxLen < 1)
        return retVal;

    //  IP address embedded, save pointer to its first digit(as string)
    if ((retTemp = strstr(rxBuffer,"ip:\"")) != NULL)
        ipFlag = retTemp - rxBuffer + 4;

    //  Message from one of the sockets, save pointer to first character
    //  (which is always message length!)
    if ((retTemp = strstr(rxBuffer,"+IPD,")) != NULL)
    {
        respFlag = retTemp - rxBuffer + 5;
        retVal |= ESP_STATUS_IPD;
    }

    //  Look for general status messages returned by ESP
    if (strstr(rxBuffer,"WIFI CONN") != NULL)
    {
        retVal |= ESP_STATUS_CONNECTED;
        wifiStatus = ESP_WIFI_CONNECTING;
    }
    if (strstr(rxBuffer,"WIFI GOT IP") != NULL)
        wifiStatus = ESP_WIFI_CONNECTED;
    if (strstr(rxBuffer,"WIFI DISCONN") != NULL)
        retVal |= ESP_STATUS_DISCN;
    if (strstr(rxBuffer,"OK") != NULL)
        retVal |= ESP_STATUS_OK;
    if (strstr(rxBuffer,"busy...") != NULL)
        retVal |= ESP_STATUS_BUSY;
    if (strstr(rxBuffer,"FAIL") != NULL)
        retVal |= ESP_STATUS_FAIL;
    if (strstr(rxBuffer,"ERROR") != NULL)
        retVal |= ESP_STATUS_ERROR;
    if (strstr(rxBuffer,"READY") != NULL)
        retVal |= ESP_STATUS_READY;
    if (strstr(rxBuffer,"SEND OK") != NULL)
        retVal |= ESP_STATUS_SENDOK;
    if (strstr(rxBuffer,"SUCCESS") != NULL)
        retVal |= ESP_RESPOND_SUCC;
    if (strstr(rxBuffer,">") != NULL)
        retVal |= ESP_STATUS_RECV;

    //  If new socket is opened save socket id
    if ((retTemp =strstr(rxBuffer,",CONNECT")) != NULL)
    {
        sockOflag = retTemp - rxBuffer - 1; //Sock ID position
        //  Socket got opened, create new client for it
        if (sockOflag >= 0)
            _clients[rxBuffer[sockOflag] - 48] =
                    new _espClient(rxBuffer[sockOflag] - 48, this);

        retVal |= ESP_STATUS_SOCKOPEN;
    }
    //  If socket is closed save socket id
    if ((retTemp =strstr(rxBuffer,",CLOSED")) != NULL)
    {
        //  Handles a case when multiple sockets are closed at the same time
        //  e.g. when few sockets are opened to the same server, and server
        //  goes down
        while (retTemp != NULL)
        {
            sockCflag = retTemp - rxBuffer - 1; //Sock ID position
            //  Socket got closed, find client with this ID and delete it
            if (sockCflag >= 0)
            {
                delete _clients[rxBuffer[sockCflag] - 48];
                _clients[rxBuffer[sockCflag] - 48] = 0;
            }

            retTemp =strstr(retTemp+1,",CLOSED");
        }
        retVal |= ESP_STATUS_SOCKCLOSE;
    }

    //  IP address embedded, extract it
    if (ipFlag >= 0)
    {
        int i = ipFlag;
        memset(_ipStr, 0, 16);
        while(((rxBuffer[i] == '.') || isdigit(rxBuffer[i])) && (i < rxLen))
            { _ipStr[i-ipFlag] = rxBuffer[i]; i++; }

        _ipAddress = _IPtoInt(_ipStr);
        retVal |= ESP_GOT_IP;
    }
    //  TCP incoming data embedded, extract it
    //  Data format:> +IPD,socketID,length:message
    if (respFlag >= 0)
    {
        int i;
        //  Get client who sent the incoming data (based on socket ID)
        _espClient *cli = const_cast<_espClient*>(_clients[_IDtoIndex(rxBuffer[respFlag] - 48)]);

        //  respFlag points to socket ID (single digit < 5)
        i = respFlag+2; //Skip comma and go to first digit of length
        //  Colon marks beginning of the message, everything before it and after
        //  the current position(i) are digits of message length
        uint8_t cmsgLen[3] = {0};
        //  Extract message length
        while(rxBuffer[i++] != ':') //  ++ here so colon is skipped when done
            cmsgLen[i-1-respFlag-2] = rxBuffer[i-1];
        //  Convert message length string to int and save it
        cli->RespLen = (uint16_t)lroundf(stof(cmsgLen, i-1-respFlag));
        // i now points to the first char of the actual received message
        //  Use raspFlag to mark starting point
        respFlag = i;
        //  Loop until the end of received message
        while((i-respFlag) < cli->RespLen)
        {
            cli->RespBody[i - respFlag] = rxBuffer[i];
            i++;
        }
        //  Set flag that new response has been received
        cli->_respRdy = true;
    }

    return retVal;
}

///-----------------------------------------------------------------------------
///                      Class constructor & destructor              [PROTECTED]
///-----------------------------------------------------------------------------

ESP8266::ESP8266() : custHook(0), flowControl(ESP_NO_STATUS), _tcpServPort(0),
                     _ipAddress(0), _servOpen(false), wifiStatus(0)
{
#ifdef __HAL_USE_EVENTLOG__
    EMIT_EV(-1, EVENT_UNINITIALIZED);
#endif  /* __HAL_USE_EVENTLOG__ */
}

ESP8266::~ESP8266()
{
    Enable(false);
}

///-----------------------------------------------------------------------------
///                      Miscellaneous functions                     [PROTECTED]
///-----------------------------------------------------------------------------

/**
 * Check whether the [flag]s are set in the [status] message
 * @param status to check for flags
 * @param flag bitwise OR of ESP_STATUS_* values to look for in status
 * @return true: if ALL flags are set in status
 *        false: if at least on flag is not set in status
 */
bool ESP8266::_InStatus(const uint32_t status, const uint32_t flag)
{
    return ((status & flag) > 0);
}

/**
 * Send command to ESP8266 module
 * Sends command passed in the null-terminated [txBuffer]. This is a blocking
 * function, awaiting reply from ESP. Function returns when status OK or ERROR
 * or any other status passed in [flags] have been received from ESP. Timeout
 * is value at which watchdog timer interrupts the process and returns ERROR flag.
 * @param txBuffer null-terminated string with command to execute
 * @param flags bitwise OR of ESP_STATUS_* values
 * @param timeout time in ms before the sending process is interrupted by WD timer
 * @return bitwise OR of ESP_STATUS_* returned by the ESP module
 */
uint32_t ESP8266::_SendRAW(const char* txBuffer, uint32_t flags, uint32_t timeout)
{
    uint16_t txLen = 0;

    HAL_ESP_WDControl(false, timeout);

    //  Reset global status
    flowControl = ESP_NO_STATUS;
    //  Wait for any ongoing transmission then flush UART port
    while(HAL_ESP_UARTBusy());
    _FlushUART();
    while(HAL_ESP_UARTBusy());
#ifdef __DEBUG_SESSION__
    DEBUG_WRITE("Sending: %s \n", txBuffer);
#endif
    //  Send char-by-char until reaching end of command
    while (*(txBuffer + txLen) != '\0')
    {
        HAL_ESP_SendChar(*(txBuffer + txLen));
        txLen++;
    }
    //  ESP messages terminated by \r\n
    HAL_ESP_SendChar('\r');
    HAL_ESP_SendChar('\n');

    //  Start listening for reply
    HAL_ESP_IntEnable(true);

    //  If non-blocking mode is not enabled wait for status
    if (!(flags & ESP_NONBLOCKING_MODE))
    {
        //  Start watchdog timer
        HAL_ESP_WDControl(true, timeout);

        while( !(flowControl & ESP_STATUS_OK) &&
                !(flowControl & ESP_STATUS_ERROR) &&
                !(flowControl & flags));

        HAL_DelayUS(1000);
        //  Stop watchdog timer
        HAL_ESP_WDControl(false, timeout);
        return flowControl;
    }
    else return ESP_NONBLOCKING_MODE;
}

/**
 * Write bytes directly to port (used when sending data of TCP/UDP socket)
 * @param buffer data to send to serial port
 * @param bufLen length of data in [buffer]
 */
void ESP8266::_RAWPortWrite(const char* buffer, uint16_t bufLen)
{
#ifdef __DEBUG_SESSION__
    DEBUG_WRITE("SendingRAWport: %s \n", buffer);
#endif

    for (uint16_t i = 0; i < bufLen; i++)
    {
        while(HAL_ESP_UARTBusy());
        HAL_ESP_SendChar(buffer[i]);
    }
}

/**
 * Empty UART's Rx buffer
 */
void ESP8266::_FlushUART()
{
    char temp = 0;
    UNUSED(temp);   //    Suppress unused variable warning
    while (HAL_ESP_CharAvail())
        temp = HAL_ESP_GetChar();
}

/**
 * Convert IP address from string to integer
 * @param ipAddr string containing IP address X.X.X.X where X=0...255
 * @return uint32_t value of IP passed as string
 */
uint32_t ESP8266::_IPtoInt(char *ipAddr)
{
    uint32_t retVal = 0;
    uint8_t it = 0;
    uint8_t oct = 3;    //IPV4 has 4 octets, 3 down to 0

    //  Starts at first digit of first octet
    while (isdigit(ipAddr[it]))
    {
        char temp[4];
        uint8_t tempIt = 0;

        //  Extract single octet
        while(isdigit(ipAddr[it]))
            temp[ tempIt++ ] = ipAddr[ it++ ];

        it++;   //  Move iterator from dot char
        //  Convert octet to int and push it to return variable
        retVal |= ((uint32_t)stoi((uint8_t*)temp, tempIt)) << (8 * oct--);
    }

    return retVal;
}

/**
 * Get client index in _clients vector based on its socket ID
 * @param sockID socket ID
 * @return index in _client vector if socket exists
 *         222 if no client matches socket ID
 */
uint8_t ESP8266::_IDtoIndex(uint8_t sockID)
{
    if (sockID < ESP_MAX_CLI)
        return sockID;
    else
        return 222;
}

///-----------------------------------------------------------------------------
/// Interrupt service routine for handling incoming data on UART (Tx)  [PRIVATE]
///-----------------------------------------------------------------------------

void UART7RxIntHandler(void)
{
    //  Grab a pointer to singleton
    ESP8266 &__esp = ESP8266::GetI();

    static char rxBuffer[1024] ;
    static uint16_t rxLen = 0;

    HAL_ESP_ClearInt();             //  Clear interrupt

    //  Loop while there are characters in receiving buffer
    while (HAL_ESP_CharAvail())
    {
        char temp = HAL_ESP_GetChar();
        //   Reset watchdog timer on every char - bus is active
        HAL_ESP_WDControl(true, 0);

        rxBuffer[rxLen++] = temp;
        //  Keep in mind buffer size
        rxLen %= sizeof(rxBuffer);
    }

    /*
     * If watchdog timer times out, artificially produce terminating sequence at
     * the end of the buffer in order to trigger next if to read the content
     */
    if (( __esp.flowControl == ESP_STATUS_ERROR))
    {
        rxBuffer[rxLen++] = '\r';
        rxBuffer[rxLen++] = '\n';
#ifdef __DEBUG_SESSION__
        DEBUG_WRITE("WATCHDOG!!\n");
#endif
    }

    //  Prevent parsing when buffer only contains \r\n and nothing else
    if (rxLen == 2)
    {
        if ((rxBuffer[rxLen-2] == '\r') && (rxBuffer[rxLen-1] == '\n'))
        {
            HAL_ESP_WDControl(false, 0);    //   Stop watchdog timer
            //  Reset receiving buffer and its size
            memset(rxBuffer, '\0', sizeof(rxBuffer));
            rxLen = 0;
            return;
        }
    }
    //  No point in starting parser for messages with such small size, return
    else if (rxLen < 2)
    {
        HAL_ESP_WDControl(false, 0);    //   Stop watchdog timer
        return;
    }

    /*
     *  There are 3 occasions when we want to process data in input buffer:
     *  1) We've reached terminator sequence of the message (\r\n)
     *  2) ESP returned '> ' (without terminator) and awaits data
     *  3) Watchdog timer has timed out changing 'flowControl' to "error"
     *      (on timeout WD timer also recalls this interrupt)
     */
    if (((rxBuffer[rxLen-2] == '\r') && (rxBuffer[rxLen-1] == '\n'))
      || ((rxBuffer[rxLen-2] == '>') && (rxBuffer[rxLen-1] == ' ' ))
      || ( __esp.flowControl == ESP_STATUS_ERROR) )
    {
        HAL_ESP_WDControl(false, 0);    //   Stop watchdog timer


#ifdef __DEBUG_SESSION__
    {
        uint16_t i;
        for (i = 0; i < rxLen; i++)
            DEBUG_WRITE("0x%x ", rxBuffer[i]);
        DEBUG_WRITE("\nParsing(%d): %s\n", rxLen, rxBuffer);
    }
#endif
        //  Parse data in receiving buffer - if there was an error from WD timer
        //  leave it in so that we know there was a problem
        if (__esp.flowControl == ESP_STATUS_ERROR)
            __esp.flowControl |= __esp.ParseResponse(rxBuffer, rxLen);
        else
            __esp.flowControl = __esp.ParseResponse(rxBuffer, rxLen);

        //  If some data came from one of opened TCP sockets receive it and
        //  pass it to a user-defined function for further processing
        if ((__esp.custHook != 0) && (__esp.flowControl & ESP_STATUS_IPD))
        {
            for (uint8_t i = 0; i < ESP_MAX_CLI; i++)
                //  Skip null pointers
                if (__esp.GetClientByIndex(i) != 0)
                //  Check which socket received data
                if (__esp.GetClientByIndex(i)->Ready())
                {
#if defined(__USE_TASK_SCHEDULER__)
                //  If using task scheduler, schedule receiving outside this ISR
                    volatile TaskEntry tE(ESP_UID, ESP_T_RECVSOCK, 0);
                    tE.AddArg(&__esp.GetClientByIndex(i)->_id, 1);
                    TaskScheduler::GetP()->SyncTask(tE);
#else
                    //  If no task scheduler do everything in here
                    _espClient* cli = __esp.GetClientByIndex(i);
                    __esp.custHook(i, (const uint8_t*)cli->RespBody, (cli->RespLen));
#endif  /* __USE_TASK_SCHEDULER__ */
                }
            }



        //  Reset receiving buffer and its size
        memset(rxBuffer, '\0', sizeof(rxBuffer));
        rxLen = 0;
    }
}
