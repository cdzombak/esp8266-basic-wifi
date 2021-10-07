#include <Arduino.h>
#include "led.h"

// nb. Wemos D1 Mini onboard LED is active-low
// TIM_DIV256 = 3 //312.5Khz (1 tick = 3.2us - 26843542.4 us max)

volatile bool ledState; // the current LED state
bool ledBlinkStarted; // whether blinking has started (ie. whether the ISR is attached)
unsigned long ledTimeOff, ledTimeOn; // desired off/on times, in milliseconds
uint32_t ledCountsOff, ledCountsOn; // desired off/on times, in timer counts of 3.2us

IRAM_ATTR void ledTimerISR() {
    if (ledState) {
        ledState = false;
        digitalWrite(LED_BUILTIN, HIGH);
        timer1_write(ledCountsOff);
        timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
    } else {
        ledState = true;
        digitalWrite(LED_BUILTIN, LOW);
        timer1_write(ledCountsOn);
        timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
    }
}

/**
 * Start LED blinking at the specified rate, restarting blinking immediately
 * if it's already running at a different blink rate. Blinking (re)starts in
 * the ON state. Times in milliseconds.
 */
void blinkLED(unsigned long timeOn, unsigned long timeOff) {
    if (!ledBlinkStarted || timeOff != ledTimeOff || timeOn != ledTimeOn) {
        ledTimeOn = timeOn;
        ledTimeOff = timeOff;
        // milliseconds * 1000 = microseconds
        // microseconds / 3.2 = ticks
        // => milliseconds * (1000/3.2) = ticks
        // => milliseconds * 312.5 = ticks
        // ... and then we actually use 312, to avoid floating-point math entirely
        ledCountsOn = timeOn * 312;
        ledCountsOff = timeOff * 312;
        ledState = true;
        digitalWrite(LED_BUILTIN, LOW);
        if (ledBlinkStarted) {
            timer1_disable();
            timer1_write(ledCountsOn);
            timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
        } else {
            timer1_attachInterrupt(ledTimerISR);
            ledBlinkStarted = true;
            timer1_write(ledCountsOn);
            timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
        }
    }
}
