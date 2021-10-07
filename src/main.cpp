#include <Arduino.h>
#include <ESP8266WiFi.h>       // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html
#include <ESP8266mDNS.h>       // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS
#include <ESP8266HTTPClient.h> // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266HTTPClient
#include <CertStoreBearSSL.h>
#include <time.h> // NOLINT(modernize-deprecated-headers)
#include <FS.h>
#include <LittleFS.h>
#include <TZ.h>
#include <ArduinoJson.h> // https://arduinojson.org

#define _TASK_SLEEP_ON_IDLE_RUN
#include <TaskScheduler.h> // https://github.com/arkhipenko/TaskScheduler

#define TIMER_INTERRUPT_DEBUG         0
#define _TIMERINTERRUPT_LOGLEVEL_     0
#include <ESP8266TimerInterrupt.h> // https://platformio.org/lib/show/11385/ESP8266TimerInterrupt

#include "config.h"

#define SERIAL_BAUD 115200

#ifndef CFG_TZ
#define CFG_TZ TZ_America_Detroit
#endif

// Hardware interrupt-based timer, for LED management
ESP8266Timer ITimer;

// Scheduler & callback method prototypes
Scheduler ts;
bool ready = false;
void connectInit();
bool onMDNSEnable();
void mDNSCallback();
void connMonitorCallback();
void httpsDemoCallback();
void ledTimerISR();
void blinkLED(unsigned long timeOn, unsigned long timeOff);

// HTTPS Requests Demo
#define EXT_IP_URL "https://ip.dzdz.cz"
//#define HTTP_TASK_INTERVAL (TASK_SECOND*30)
#define HTTP_TASK_INTERVAL (TASK_SECOND*5)
//#define USE_DYNAMIC_JSON_DOC // demos ArduinoJson's DynamicJsonDoc
BearSSL::CertStore certStore;
BearSSL::Session tlsSession;
WiFiClientSecure wifiClient;

// LED (all times in milliseconds)
#define CONNECTED_LED_TIME_ON   (TASK_SECOND/5)
#define CONNECTED_LED_TIME_OFF  (TASK_SECOND*1.5)
#define CONNECTING_LED_TIME_ON  (TASK_SECOND/5)
#define CONNECTING_LED_TIME_OFF CONNECTING_LED_TIME_ON
#define CERT_ERR_LED_TIME_ON    (TASK_SECOND/1.5)
#define CERT_ERR_LED_TIME_OFF   CERT_ERR_LED_TIME_ON

// Tasks
Task  tConnect     (TASK_SECOND, TASK_FOREVER, &connectInit, &ts, true);  // handles waiting on initial WiFi connection
Task  tMDNS        (TASK_MILLISECOND*50, TASK_FOREVER, &mDNSCallback, &ts, false, &onMDNSEnable, nullptr);
Task  tConnMonitor (TASK_SECOND, TASK_FOREVER, &connMonitorCallback, &ts, false);
Task  tHttpsDemo   (HTTP_TASK_INTERVAL, TASK_FOREVER, &httpsDemoCallback, &ts, false);

/**
 * Sets the system time via NTP, as required for x.509 validation
 * see https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WiFi/examples/BearSSL_CertStore/BearSSL_CertStore.ino
 */
void setClock() {
    configTime(CFG_TZ, "pool.ntp.org", "time.nist.gov");

    Serial.printf_P(PSTR("%lu: Waiting for NTP time sync "), millis());
    time_t now = time(nullptr);
    while (now < 8 * 3600 * 2) {
        delay(250);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.print(F("\r\n"));
    struct tm timeinfo; // NOLINT(cppcoreguidelines-pro-type-member-init)
    gmtime_r(&now, &timeinfo);
    Serial.printf_P(PSTR("Current time (UTC):   %s"), asctime(&timeinfo));
    localtime_r(&now, &timeinfo);
    Serial.printf_P(PSTR("Current time (Local): %s"), asctime(&timeinfo));
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    pinMode(LED_BUILTIN, OUTPUT);

    LittleFS.begin();
    int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
    Serial.printf_P(PSTR("%lu: read %d CA certs into store\r\n"), millis(), numCerts);
    if (numCerts == 0) {
        Serial.println(F("!!! No certs found. Did you run certs-from-mozilla.py and upload the LittleFS directory?"));
        blinkLED(CERT_ERR_LED_TIME_ON, CERT_ERR_LED_TIME_OFF);
        return;
    }
    wifiClient.setCertStore(&certStore);
    wifiClient.setSession(&tlsSession);

    ready = true;
}

void loop() {
    if (ready) { // ready flag blocks running the main program if startup tasks failed
        ts.execute();
    }
}

/**
 * Get my external IP address and post it as the body to CFG_POST_URL.
 */
void httpsDemoCallback() {
    HTTPClient httpClient;
    httpClient.begin(wifiClient, EXT_IP_URL);
    Serial.printf_P(PSTR("%lu: Starting GET request to %s\r\n"), millis(), EXT_IP_URL);
    yield();
    String getResult = "";
    int respCode = httpClient.GET();
    yield();
    if (respCode >= 400) {
        Serial.printf_P(PSTR("%lu: HTTP Error %d\r\n"), millis(), respCode);
    } else if (respCode > 0) {
        Serial.printf_P(PSTR("%lu: HTTP %d\r\n"), millis(), respCode);
        getResult = httpClient.getString();
        Serial.printf_P(PSTR("\t%s\r\n"), getResult.c_str());
    } else {
        Serial.printf_P(PSTR("%lu: error: %s\r\n"), millis(), HTTPClient::errorToString(respCode).c_str());
    }
    httpClient.end();

    if (respCode < 0 || respCode >= 400) {
        return;
    }

    // avoid a reset/crash due to blocking WiFi stack for too long
    // that reset looks like: rst cause:2, boot mode:(3,6)
    yield();

    String jsonBody;
#ifdef USE_DYNAMIC_JSON_DOC
    DynamicJsonDocument jsonDoc(96);
#else
    StaticJsonDocument<96> jsonDoc;
#endif
    jsonDoc["ext_ip"] = getResult;
    jsonDoc["int_ip"] = WiFi.localIP().toString();
    serializeJson(jsonDoc, jsonBody);

    httpClient.setReuse(false); // holy hell, this took forever to figure out
    httpClient.begin(wifiClient, CFG_POST_URL);
    httpClient.addHeader(F("Content-Type"), F("application/json"));
    Serial.printf_P(PSTR("%lu: Starting POST request to %s\r\n"), millis(), CFG_POST_URL);
    yield();
    respCode = httpClient.POST(jsonBody);
    yield();
    if (respCode >= 400) {
        Serial.printf_P(PSTR("%lu: HTTP Error %d\r\n"), millis(), respCode);
    } else if (respCode > 0) {
        Serial.printf_P(PSTR("%lu: HTTP %d\r\n"), millis(), respCode);
    } else {
        Serial.printf_P(PSTR("%lu: error: %s\r\n"), millis(), HTTPClient::errorToString(respCode).c_str());
    }
    httpClient.end();
}

/**
   Wait for initial WiFi connection, then set clock & enable connection monitor
*/
void connectWait() {
    Serial.printf_P(PSTR("%lu: Waiting for initial WiFi connection\r\n"), millis());

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf_P(PSTR("%lu: Connected. My IP: %s\r\n"), millis(), WiFi.localIP().toString().c_str());
        blinkLED(CONNECTED_LED_TIME_ON, CONNECTED_LED_TIME_OFF);
        tConnect.disable();

        yield();
        setClock(); // blocking call to set clock for x.509 validation as soon as WiFi is connected
        tMDNS.enable();
        tConnMonitor.enable();
    }
}

/**
   Initiate connection to the WiFi network
*/
void connectInit() {
    Serial.printf_P(PSTR("%lu: Connecting to WiFi (%s)\r\n"), millis(), CFG_WIFI_ESSID);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(CFG_HOSTNAME);
    WiFi.begin(CFG_WIFI_ESSID, CFG_WIFI_PASSWORD);
    blinkLED(CONNECTING_LED_TIME_ON, CONNECTING_LED_TIME_OFF);
    yield();  // esp8266 yield allows WiFi stack to run
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
 * - Whether HTTPS requests (our "work") is enabled
 */
void connMonitorCallback() {
    auto status = WiFi.status();
    if (status == WL_CONNECTED) {
        blinkLED(CONNECTED_LED_TIME_ON, CONNECTED_LED_TIME_OFF);
        tHttpsDemo.enableIfNot();
    } else {
        Serial.print(millis()); Serial.print(F(": WiFi connection status: ")); Serial.println(status);
        blinkLED(CONNECTING_LED_TIME_ON, CONNECTING_LED_TIME_OFF);
        tHttpsDemo.disable();
    }
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
