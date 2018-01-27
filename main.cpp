#include "HAL/hal.h"
#include "esp8266/esp8266.h"
#include "serialPort/uartHW.h"

uint8_t socketId;
volatile bool gotData = false;

/**
 * Function to be called when a new data is received from TCP clients on ALL
 * opened sockets at ESP. Function is called through data scheduler if enabled,
 * otherwise called directly from ISR (don't send any data from here!)
 * @param sockID socket ID at which the reply arrived
 * @param buf buffer containing incoming data
 * @param len size of incoming data in [buf] buffer
 */
static void ESPDataReceived(const uint8_t sockID, const uint8_t *buf, const uint16_t len)
{
    //  Check if data came from the socket we opened
    if (sockID == socketId)
    {
        //  Set a flag so we can reply to it from main loop
        gotData = true;

        //  Print received data to serial
        DEBUG_WRITE("Received %d bytes from %d: %s \n ", len, sockID, buf);
    }
    else
    {
        //  Do nothing
    }
}


int main(void)
{
    //  ESP8266 driver is implemented as a singleton, grab a reference to it
    ESP8266& esp = ESP8266::GetI();

    //  Initialize board and FPU
    HAL_BOARD_CLOCK_Init();

    //  Initialize serial port
    SerialPort::GetI().InitHW();
    DEBUG_WRITE("Initialized Uart... \n");

    //  Initialize hardware and software related to ESP8266
    esp.InitHW();
    DEBUG_WRITE("Initialized ESP, connecting to AP...");
    //  Add hook to be called once ESP receives new data (asynchronous)
    esp.AddHook(ESPDataReceived);
    //  Connect to AP in blocking mode
    esp.ConnectAP("sgvfyj7a", "7vxy3b5d", false);

    if (esp.MyIP() == 0)
    {
        DEBUG_WRITE("Failed to acquire IP!");
        while(1);
    }


    DEBUG_WRITE("Connected\n Acquired IP: %u \n", esp.MyIP());

    DEBUG_WRITE("Opening connection to TCP server...");
    //  Connect to a TCP server (192.168.0.12:52699), keep socket alive
    //  Function returns socket descriptor. Save it so we can reply to the socket
    socketId = esp.OpenTCPSock("192.168.0.16", 52699, true);


    //  Check if socket descriptor is valid
    if (socketId > ESP_MAX_CLI)
    {
        DEBUG_WRITE("Invalid socket descriptor %d \n");
        while(1);
    }

    DEBUG_WRITE("Connected\nGoing into sending routine \n");

    //  Every 3s send a message to TCP server - after 45s (15 messages) close
    //  the server and disconnect after
    int8_t counter = 0;
    while (counter < 15)
    {
        //  Send data to an open socket. If you ensure that string is
        //  null-terminated then there's no need to supply length parameter
        esp.GetClientBySockID(socketId)->SendTCP("Hello from TM4C1294!\n\0");

        //  Check if there was a reply from an open socket
        if (gotData)
        {
            //  Clear flag
            gotData = false;

            //  Send confirmation of received message back to the server
            esp.GetClientBySockID(socketId)->SendTCP("I've received your message!\n\0");
        }

        DEBUG_WRITE("Sent a message, %d more to go\n", (15 - counter));
        //  Do nothing for cca 3s
        HAL_DelayUS(3000000);
        counter++;
    }

    //  Close socket
    esp.GetClientBySockID(socketId)->Close();
    //  Disconnect from AP
    esp.DisconnectAP();

    while(1);
}
