#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED
#include <signal.h>

extern int sock_2ble_client;

//uncomment if you need debug this app without connection down to BLE mesh. Only up stream to web will be exist
//#define DEBUG_ALONE

#define DEBUG(str) str
//#define DEBUG(str)

#endif // MAIN_H_INCLUDED
