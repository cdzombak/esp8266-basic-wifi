#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <AsyncPing.h>

#define _TASK_SLEEP_ON_IDLE_RUN
#include <TaskScheduler.h>

#include "config.h"  // git-ignored file for configuration including secrets

// Goal here is to:
// - connect to WiFi
//    - esp8266wifi should handle reconnecting after a failure; I just need to monitor and blink the LED
// - broadcast mDNS name
// - blink LED fast while (re)connecting; display slow LED feedback when OK
// - allow doing useful work once it's connected (simulated here by pinging Google DNS)

#define SERIAL_BAUD 115200

Scheduler ts;

// Callback method prototypes
void connectInit();
void ledCallback();
bool onLedEnable();
void onLedDisable();
void ledOn();
void ledOff();
bool onMDNSEnable();
void mDNSCallback();
void connMonitorCallback();
void pingCallback();

// Ping (our "useful work")
AsyncPing ping;
#define PING_COUNT         (2)
#define PING_TIMEOUT       (TASK_SECOND/3)
#define PING_TASK_INTERVAL (PING_TIMEOUT*PING_COUNT + TASK_MILLISECOND*100)
//#define PING_PRINT_SUMMARY

// Tasks
Task  tConnect     (TASK_SECOND, TASK_FOREVER, &connectInit, &ts, true);  // handles waiting on initial WiFi connection
Task  tLED         (TASK_IMMEDIATE, TASK_FOREVER, &ledCallback, &ts, false, &onLedEnable, &onLedDisable);
Task  tMDNS        (TASK_MILLISECOND*50, TASK_FOREVER, &mDNSCallback, &ts, false, &onMDNSEnable, nullptr);
Task  tConnMonitor (TASK_SECOND, TASK_FOREVER, &connMonitorCallback, &ts, false);
Task  tPing        (PING_TASK_INTERVAL, TASK_FOREVER, &pingCallback, &ts, false);  // simulates starting our "useful work"

// WiFi
const char *ssid     = CFG_WIFI_ESSID;
const char *pwd      = CFG_WIFI_PASSWORD;
const char *hostname = CFG_HOSTNAME;

// LED
bool ledState;
long ledTimeOff, ledTimeOn;
#define CONNECTED_LED_TIME_ON   (TASK_SECOND/3)
#define CONNECTED_LED_TIME_OFF  (TASK_SECOND)
#define CONNECTING_LED_TIME_ON  (TASK_SECOND/4)
#define CONNECTING_LED_TIME_OFF (TASK_SECOND/4)

void setup() {
    Serial.begin(SERIAL_BAUD);
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    ts.execute();
}

/**
   Wait for initial WiFi connection
*/
void connectWait() {
    Serial.print(millis());
    Serial.println(F(": Waiting for initial connection"));

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(millis());
        Serial.print(F(": Connected. My IP: "));
        Serial.println(WiFi.localIP());
        ledTimeOff = CONNECTED_LED_TIME_OFF;
        ledTimeOn = CONNECTED_LED_TIME_ON;
        tLED.enableIfNot();
        tConnect.disable();  // triggers internal status signal to resolve to 0

        tMDNS.enable();
        tConnMonitor.enable();
    }
}

/**
   Initiate connection to the WiFi network
*/
void connectInit() {
    Serial.print(F("Connecting to WiFi ("));
    Serial.print(ssid);
    Serial.println(F(")"));

    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostname);
    WiFi.begin(ssid, pwd);
    yield();  // esp8266 yield allows WiFi stack to run

    ledTimeOff = CONNECTING_LED_TIME_OFF;
    ledTimeOn = CONNECTING_LED_TIME_ON;
    tLED.enable();

    tConnect.yield(&connectWait);  // pass control to Scheduler; wait for initial WiFi connection
}

/**
 * Start the mDNS stack.
 */
bool onMDNSEnable() {
    return MDNS.begin(hostname);
}

/**
 * Keep the mDNS stack running.
 */
void mDNSCallback() {
    MDNS.update();
}

/**
 * Check WiFi status every second and configure accordingly:
 * - LED blink rate
 * - Whether pinging (our "work") is enabled
 */
void connMonitorCallback() {
    auto status = WiFi.status();
    if (status == WL_CONNECTED) {
        ledTimeOff = CONNECTED_LED_TIME_OFF;
        ledTimeOn = CONNECTED_LED_TIME_ON;
        tLED.enableIfNot();
        tPing.enableIfNot();
    } else {
        Serial.print(millis()); Serial.print(F(": WiFi connection status: ")); Serial.println(status);
        ledTimeOff = CONNECTING_LED_TIME_OFF;
        ledTimeOn = CONNECTING_LED_TIME_ON;
        tLED.enableIfNot();
        tPing.disable();
    }
}

/**
 * Ping Google DNS (8.8.4.4), per configuration defined above.
 * This simulates the "useful work" this project performs.
 */
void pingCallback() {
    // adapted from sample code in https://github.com/akaJes/AsyncPing README.

    ping.on(true,[](const AsyncPingResponse& response){
        IPAddress addr(response.addr); // to prevent with no const toString() in 2.3.0
        if (response.answer)
            Serial.printf_P(PSTR("%lu: %d bytes from %s: icmp_seq=%d ttl=%d time=%lu ms\n"), millis(), response.size, addr.toString().c_str(), response.icmp_seq, response.ttl, response.time);
        else
            Serial.printf_P(PSTR("%lu: no answer yet for %s icmp_seq=%d\n"), millis(), addr.toString().c_str(), response.icmp_seq);
        return false; // do not stop
    });
    ping.on(false,[](const AsyncPingResponse& response){
        IPAddress addr(response.addr); // to prevent with no const toString() in 2.3.0
        Serial.printf_P(PSTR("%lu: total answer from %s sent %d recevied %d time %lu ms\n"), millis(), addr.toString().c_str(),response.total_sent,response.total_recv,response.total_time);
        return true;
    });

    ping.begin(IPAddress(8, 8, 4, 4), PING_COUNT, PING_TIMEOUT);
}

/**
 * Flip the LED state based on the current state.
 */
void ledCallback() {
    if (ledState) ledOff();
    else ledOn();
}

/**
 * Make sure the LED starts lit.
 */
bool onLedEnable() {
    ledOn();
    return true;
}

/**
 * Make sure LED ends dimmed.
 */
void onLedDisable() {
    ledOff();
}

/**
 * Turn LED on; set delay to ledTimeOn.
 * nb. Wemos D1 Mini onboard LED is active-low
 */
void ledOn() {
    ledState = true;
    digitalWrite(LED_BUILTIN, LOW);
    tLED.delay(ledTimeOn);
}

/**
 * Turn LED off; set delay to ledTimeOff.
 * nb. Wemos D1 Mini onboard LED is active-low
 */
void ledOff() {
    ledState = false;
    digitalWrite(LED_BUILTIN, HIGH);
    tLED.delay(ledTimeOff);
}
