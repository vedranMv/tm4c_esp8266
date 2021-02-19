ESP8266 on Tiva TM4C1294
======================

This repository contains an example project for running ESP8266 wifi module with TM4C1294 microcontroller. __Note__ that the project is for CodeComposer studio.

Code is taken from a [bigger project I worked on](https://github.com/vedranMv/roverRPi3)  and can be integrated with other modules from there (task scheduler and event logger) hence the macro switches in ``esp8266.cpp`` source :)

## Wiring

ESP8266 is connected to UART7 peripheral of Tiva evaluation kit as follows:


ESP8266       |   EK-TM4C1294XL
--------------|------------------
ESP8266 TxD   | PC4(U7Rx)
ESP8266 RxD   | PC5(U7Tx)
ESP8266 CH_PD | PC6(GPIO)
ESP8266 VCC   | 3.3V
ESP8266 GND   | GND


## Library

ESP8266 library provided in this example is implemented in C++ and based on the singleton design approach. At the beginning of the program, user grabs the reference to the instance of a singleton and uses it for the rest of the program.


Library provides complete TCP functionality, both in client and server mode. Handling of clients is automatic and happens in ISR during parsing of the data received from ESP where client instances are automatically created and destroyed as connections are opened/closed.


Data received from the open sockets is passed to a hook function which user provides during initialization. Hook function is a piece of code called whenever new data arrives from a socket. This functions gets exclusive access to handle the data immediately as it's received, otherwise data resides in ``_espClient`` object where it can be accessed whenever.


Watchdog timer is another feature implemented to ensure reliability. Timer 6 is used as a watchdog timer monitoring the time between received characters. In case communications hangs, watchdog timer will abort the communication and safely return from ongoing action. Watchdog functionality is automatically handled by the library and no user interaction/configuration is needed.


For easier porting of the code to other platforms, all board-specific functions are put in ``HAL/<board_name>/``. Main HAL include file, ``HAL/hal.h``, then uses macros to select the right board and load appropriate board drivers.


## Example code

``main.c`` contains a simple example which demonstrates connecting to AP, opening a connection to TCP server and periodically sending data to server every 3 seconds. Server can also reply and the example code prints out whatever message arrives from server. Afterwards, it also sends a message to server to acknowledge the reception.

Baudrate of ESP8266 in this example has been set to 1000000, but it can be configured with **ESP_DEF_BAUD** macro in ``esp8266.h`` file.

To run the example change your AP details and local IP of your PC in ``main.c``. Then start TCP server (in linux, tcp server can simply be run from PC by using netcat ``nc -l 52699``). After that just compile and upload the code to MCU. Result should look something like this:


![alt tag](https://my-server.dk/public/images/tm4c/test.png)
