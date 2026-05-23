// webserver.h — tiny phone control page (just an on/off toggle)
#pragma once
#include <Arduino.h>

void webserver_begin();
bool webserver_is_on();    // returns current toggle state (true = listening enabled)
