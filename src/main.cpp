#include <Arduino.h>
#include <ESP8266WiFi.h>       // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html
#include <ESP8266HTTPClient.h> // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266HTTPClient
#include <WiFiClientSecureBearSSL.h>
#include <BearSSLHelpers.h>
#include <time.h> // NOLINT(modernize-deprecated-headers)
#include <TZ.h>
#define _TASK_SLEEP_ON_IDLE_RUN // NOLINT(bugprone-reserved-identifier)
#include <TaskScheduler.h> // https://github.com/arkhipenko/TaskScheduler

#include "led.h"
#include "config.h"
#include "certs.h"

#define SERIAL_BAUD 115200

#ifndef CFG_TZ
#define CFG_TZ TZ_America_Detroit
#endif

// Scheduler & callback method prototypes
Scheduler ts;
void connectInit();
void connMonitorCallback();
void httpsDemoCallback();

// HTTPS Requests Demo
#define EXT_IP_URL "https://ip.dzdz.cz" // Exclusively uses certificates from Let's Encrypt
#define HTTP_TASK_INTERVAL (TASK_SECOND*30)
BearSSL::WiFiClientSecure wifiClient;
BearSSL::X509List trustedRoots;
// We could also write this, then append the second & any remaining certs in setup():
// BearSSL::X509List trustedRoots(cert_ISRG_X1);

// LED (all times in milliseconds)
#define CONNECTED_LED_TIME_ON   (TASK_SECOND/5)
#define CONNECTED_LED_TIME_OFF  (TASK_SECOND*1.5)
#define CONNECTING_LED_TIME_ON  (TASK_SECOND/5)
#define CONNECTING_LED_TIME_OFF CONNECTING_LED_TIME_ON

// Tasks
Task  tConnect     (TASK_SECOND, TASK_FOREVER, &connectInit, &ts, true);  // handles waiting on initial WiFi connection
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

    trustedRoots.append(cert_ISRG_X1);
    trustedRoots.append(cert_ISRG_X2);

    wifiClient.setTrustAnchors(&trustedRoots);
    wifiClient.setSSLVersion(BR_TLS12, BR_TLS12);
}

void loop() {
    ts.execute();
}

/**
 * Get my external IP address and print it to the serial console.
 */
void httpsDemoCallback() {
    HTTPClient httpClient;
    httpClient.begin(wifiClient, EXT_IP_URL);
    Serial.printf_P(PSTR("%lu: Starting GET request to %s\r\n"), millis(), EXT_IP_URL);
    String getResult = "";
    int respCode = httpClient.GET();
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

        setClock(); // blocking call to set clock for x.509 validation as soon as WiFi is connected
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
