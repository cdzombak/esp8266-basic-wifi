#include <Arduino.h>
#include <ESP8266WiFi.h>    // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html
#include <ESP8266mDNS.h>    // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS
#include <ESP8266Ping.h>    // https://github.com/dancol90/ESP8266Ping
#include <InfluxDbClient.h> // https://github.com/tobiasschuerg/InfluxDB-Client-for-Arduino

#define _TASK_SLEEP_ON_IDLE_RUN
#include <TaskScheduler.h> // https://github.com/arkhipenko/TaskScheduler

#define TIMER_INTERRUPT_DEBUG         0
#define _TIMERINTERRUPT_LOGLEVEL_     0
#include "ESP8266TimerInterrupt.h" // https://github.com/khoih-prog/ESP8266TimerInterrupt v1.4.0 (PlatformIO listing is outdated)

#include "config.h"  // git-ignored file for configuration including secrets

// Goal here is to:
// - connect to WiFi
//    - esp8266wifi should handle reconnecting after a failure; I just need to monitor and blink the LED
// - broadcast mDNS name
// - blink LED fast while (re)connecting; display slow LED feedback when OK
// - allow doing useful work once it's connected (here, pinging Google DNS and reporting ping results and WiFi stats to InfluxDB)

#define SERIAL_BAUD 115200

// Hardware interrupt-based timer, for LED management
ESP8266Timer ITimer;

// Scheduler & callback method prototypes
Scheduler ts;
void connectInit();
bool onMDNSEnable();
void mDNSCallback();
void connMonitorCallback();
void loggerCallback();
void ledTimerISR();
void blinkLED(unsigned long timeOn, unsigned long timeOff);

// Ping task + Influx logging
#define PING_COUNT   (2)
#define PING_TARGET  IPAddress(8, 8, 4, 4)
#define LOGGER_TASK_INTERVAL (TASK_SECOND*5)

#ifndef CFG_DEVICE_NAME_TAG
#define CFG_DEVICE_NAME_TAG CFG_HOSTNAME
#endif

#ifdef CFG_INFLUXDB_1_DB_NAME
InfluxDBClient influxClient(CFG_INFLUXDB_URL, CFG_INFLUXDB_1_DB_NAME);
#else
InfluxDBClient influxClient(CFG_INFLUXDB_URL, CFG_INFLUXDB_ORG, CFG_INFLUXDB_BUCKET, CFG_INFLUXDB_TOKEN);
#endif

// LED (all times in milliseconds)
#define CONNECTED_LED_TIME_ON   (TASK_SECOND/5)
#define CONNECTED_LED_TIME_OFF  (TASK_SECOND*1.5)
#define CONNECTING_LED_TIME_ON  (TASK_SECOND/5)
#define CONNECTING_LED_TIME_OFF (TASK_SECOND/5)

// Tasks
Task  tConnect     (TASK_SECOND, TASK_FOREVER, &connectInit, &ts, true);  // handles waiting on initial WiFi connection
Task  tMDNS        (TASK_MILLISECOND*50, TASK_FOREVER, &mDNSCallback, &ts, false, &onMDNSEnable, nullptr);
Task  tConnMonitor (TASK_SECOND, TASK_FOREVER, &connMonitorCallback, &ts, false);
Task  tDataLogger  (LOGGER_TASK_INTERVAL, TASK_FOREVER, &loggerCallback, &ts, false);

void setup() {
    Serial.begin(SERIAL_BAUD);
    pinMode(LED_BUILTIN, OUTPUT);
    influxClient.setWriteOptions(WriteOptions().bufferSize(12)); // 12 points == 1 minute of readings @ 5 second intervals
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
        blinkLED(CONNECTED_LED_TIME_ON, CONNECTED_LED_TIME_OFF);
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
    Serial.print(CFG_WIFI_ESSID);
    Serial.println(F(")"));

    WiFi.mode(WIFI_STA);
    WiFi.hostname(CFG_HOSTNAME);
    WiFi.begin(CFG_WIFI_ESSID, CFG_WIFI_PASSWORD);
    yield();  // esp8266 yield allows WiFi stack to run

    blinkLED(CONNECTING_LED_TIME_ON, CONNECTING_LED_TIME_OFF);

    tConnect.yield(&connectWait);  // pass control to Scheduler; wait for initial WiFi connection
}

/**
 * Start the mDNS stack.
 */
bool onMDNSEnable() {
    return MDNS.begin(CFG_HOSTNAME);
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
        blinkLED(CONNECTED_LED_TIME_ON, CONNECTED_LED_TIME_OFF);
        tDataLogger.enableIfNot();
    } else {
        Serial.print(millis()); Serial.print(F(": WiFi connection status: ")); Serial.println(status);
        blinkLED(CONNECTING_LED_TIME_ON, CONNECTING_LED_TIME_OFF);
        tDataLogger.disable();
    }
}

/**
 * Ping Google DNS (8.8.4.4), per configuration defined above.
 * Log the results, plus uptime and WiFi status, to InfluxDB.
 */
void loggerCallback() {
    bool pingSuccess = Ping.ping(PING_TARGET, PING_COUNT);

    Point pointDevice(CFG_MEASUREMENT_NAME);
    pointDevice.addTag("device_name", CFG_DEVICE_NAME_TAG);
    pointDevice.addField("wifi_rssi", WiFi.RSSI());
    pointDevice.addField("uptime_ms", millis());
    pointDevice.addField("ping_success", pingSuccess);
    pointDevice.addField("ping_avg_time_ms", Ping.averageTime());
    influxClient.writePoint(pointDevice);
}

// LED blink management:
volatile bool privLEDState;
bool privLEDBlinkStarted;
unsigned long privLEDTimeOff, privLEDTimeOn;

/**
 * Start LED blinking at the specified rate, restarting blinking immediately
 * if it's already running at a different blink rate. Blinking (re)starts in
 * the ON state.
 */
void blinkLED(unsigned long timeOn, unsigned long timeOff) {
    if (!privLEDBlinkStarted || timeOff != privLEDTimeOff || timeOn != privLEDTimeOn) {
        privLEDTimeOn = timeOn;
        privLEDTimeOff = timeOff;
        privLEDState = true;
        digitalWrite(LED_BUILTIN, LOW);
        if (privLEDBlinkStarted) {
            ITimer.setInterval(privLEDTimeOn * 1000, ledTimerISR);
        } else {
            ITimer.attachInterruptInterval(privLEDTimeOn * 1000, ledTimerISR);
            privLEDBlinkStarted = true;
        }
    }
}

IRAM_ATTR void ledTimerISR() {
    if (privLEDState) {
        privLEDState = false;
        digitalWrite(LED_BUILTIN, HIGH);
        ITimer.setInterval(privLEDTimeOff * 1000, ledTimerISR);
    } else {
        privLEDState = true;
        digitalWrite(LED_BUILTIN, LOW);
        ITimer.setInterval(privLEDTimeOn * 1000, ledTimerISR);
    }
}
