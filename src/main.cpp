#include <Arduino.h>
#include <ESP8266WiFi.h>    // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html
#include <ESP8266mDNS.h>    // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS
#include <ESP8266Ping.h>    // https://github.com/dancol90/ESP8266Ping
#include <InfluxDbClient.h> // https://github.com/tobiasschuerg/InfluxDB-Client-for-Arduino

#define _TASK_SLEEP_ON_IDLE_RUN
#include <TaskScheduler.h> // https://github.com/arkhipenko/TaskScheduler

#include "config.h"  // git-ignored file for configuration including secrets

// Goal here is to:
// - connect to WiFi
//    - esp8266wifi should handle reconnecting after a failure; I just need to monitor and blink the LED
// - broadcast mDNS name
// - blink LED fast while (re)connecting; display slow LED feedback when OK
// - allow doing useful work once it's connected (simulated here by pinging Google DNS)

#define SERIAL_BAUD 115200

// Scheduler & callback method prototypes
Scheduler ts;
void connectInit();
void ledCallback();
bool onLedEnable();
void onLedDisable();
void ledOn();
void ledOff();
bool onMDNSEnable();
void mDNSCallback();
void connMonitorCallback();
void loggerCallback();

// Ping task + Influx logging
#define PING_COUNT   (2)
#define PING_TARGET  IPAddress(8, 8, 4, 4)
#define LOGGER_TASK_INTERVAL (TASK_SECOND*5)
#ifdef CFG_INFLUXDB_1_DB_NAME
InfluxDBClient influxClient(CFG_INFLUXDB_URL, CFG_INFLUXDB_1_DB_NAME);
#else
InfluxDBClient influxClient(CFG_INFLUXDB_URL, CFG_INFLUXDB_ORG, CFG_INFLUXDB_BUCKET, CFG_INFLUXDB_TOKEN);
#endif

// LED
bool ledState;
long ledTimeOff, ledTimeOn;
#define CONNECTED_LED_TIME_ON   (TASK_SECOND/4)
#define CONNECTED_LED_TIME_OFF  (TASK_SECOND)
#define CONNECTING_LED_TIME_ON  (TASK_SECOND/5)
#define CONNECTING_LED_TIME_OFF (TASK_SECOND/5)

// Tasks
Task  tConnect     (TASK_SECOND, TASK_FOREVER, &connectInit, &ts, true);  // handles waiting on initial WiFi connection
Task  tLED         (TASK_IMMEDIATE, TASK_FOREVER, &ledCallback, &ts, false, &onLedEnable, &onLedDisable);
Task  tMDNS        (TASK_MILLISECOND*50, TASK_FOREVER, &mDNSCallback, &ts, false, &onMDNSEnable, nullptr);
Task  tConnMonitor (TASK_SECOND, TASK_FOREVER, &connMonitorCallback, &ts, false);
Task  tDataLogger  (LOGGER_TASK_INTERVAL, TASK_FOREVER, &loggerCallback, &ts, false);  // simulates starting our "useful work"

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
    Serial.print(CFG_WIFI_ESSID);
    Serial.println(F(")"));

    WiFi.mode(WIFI_STA);
    WiFi.hostname(CFG_HOSTNAME);
    WiFi.begin(CFG_WIFI_ESSID, CFG_WIFI_PASSWORD);
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
        ledTimeOff = CONNECTED_LED_TIME_OFF;
        ledTimeOn = CONNECTED_LED_TIME_ON;
        tLED.enableIfNot();
        tDataLogger.enableIfNot();
    } else {
        Serial.print(millis()); Serial.print(F(": WiFi connection status: ")); Serial.println(status);
        ledTimeOff = CONNECTING_LED_TIME_OFF;
        ledTimeOn = CONNECTING_LED_TIME_ON;
        tLED.enableIfNot();
        tDataLogger.disable();
    }
}

/**
 * Ping Google DNS (8.8.4.4), per configuration defined above.
 * Log the results, plus uptime and WiFi status, to InfluxDB.
 * This simulates the "useful work" this project performs.
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
