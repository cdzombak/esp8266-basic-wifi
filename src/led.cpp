#include <Arduino.h>
#include "led.h"

// nb. Wemos D1 Mini onboard LED is active-low

volatile bool ledState; // the current LED state
bool ledBlinkEnabled; // whether blinking has started (ie. whether the ISR is attached)
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

void blinkLED(unsigned long desiredTimeOn, unsigned long desiredTimeOff) {
    // TIM_DIV256 = 3 //312.5Khz (1 tick = 3.2us - 26843542.4 us max)
    // milliseconds * 1000 = microseconds
    // microseconds / 3.2 = ticks
    // => milliseconds * (1000/3.2) = ticks
    // => milliseconds * 312.5 = ticks
    // ... and then we actually use 312, to avoid floating-point math entirely
    auto desiredCountsOn = desiredTimeOn * 312;
    auto desiredCountsOff = desiredTimeOff * 312;
    if (!ledBlinkEnabled || desiredCountsOff != ledCountsOff || desiredCountsOn != ledCountsOn) {
        ledCountsOn = desiredCountsOn;
        ledCountsOff = desiredCountsOff;
        ledState = true;
        digitalWrite(LED_BUILTIN, LOW);
        if (ledBlinkEnabled) {
            timer1_disable();
            timer1_write(ledCountsOn);
            timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
        } else {
            ledBlinkEnabled = true;
            timer1_attachInterrupt(ledTimerISR);
            timer1_write(ledCountsOn);
            timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
        }
    }
}

void stopLED() {
    if (ledBlinkEnabled) {
        timer1_disable();
        timer1_detachInterrupt();
        ledBlinkEnabled = false;
    }
}
