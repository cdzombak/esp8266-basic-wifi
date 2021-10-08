#ifndef ESP8266_BASIC_WIFI_LED_H
#define ESP8266_BASIC_WIFI_LED_H

/**
 * Start LED blinking at the specified rate, restarting blinking immediately
 * if it's already running at a different blink rate. Blinking (re)starts in
 * the ON state. Times in milliseconds.
 */
void blinkLED(unsigned long desiredTimeOn, unsigned long desiredTimeOff);


/**
 * Stop LED blinking, freeing timer1 for other uses.
 */
void stopLED();

#endif //ESP8266_BASIC_WIFI_LED_H
