#ifndef ESP8266_BASIC_WIFI_LED_H
#define ESP8266_BASIC_WIFI_LED_H

/**
 * Start LED blinking at the specified rate, restarting blinking immediately
 * if it's already running at a different blink rate. Blinking (re)starts in
 * the ON state. Times in milliseconds.
 */
void blinkLED(unsigned long timeOn, unsigned long timeOff);

#endif //ESP8266_BASIC_WIFI_LED_H
