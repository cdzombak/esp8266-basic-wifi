# ESP8266/Arduino/PlatformIO Demos

This project's branches contain several different [ESP8266 + Arduino](https://arduino-esp8266.readthedocs.io/en/latest/) demos, built using [PlatformIO](https://platformio.org), for the [Wemos D1 Mini board](https://www.wemos.cc/en/latest/d1/d1_mini.html):

- [Branch `basic-wifi`](https://github.com/cdzombak/esp8266-basic-wifi/tree/basic-wifi) connects to a WiFi network and then periodically pings Google
- [Branch `influxdb`](https://github.com/cdzombak/esp8266-basic-wifi/tree/influxdb) reports ping results and WiFi statistics to an InfluxDB server periodically
- [Branch `https`](https://github.com/cdzombak/esp8266-basic-wifi/tree/https) performs GET and POST requests over HTTPS, using JSON and performing proper TLS certificate verification using a root store
- [Branch `letsencrypt`](https://github.com/cdzombak/esp8266-basic-wifi/tree/letsencrypt) performs a GET over HTTPS, performing proper TLS certificate verification using a small set of root certificates (Let's Encrypt's roots)

My goals for this project are:

- Learn about this platform for myself; get good enough at it to do hobby projects.
- Provide some good examples using what I think are best practices, since a lot of tutorial/demonstration Arduino code available online is not very good.

In particular, I'd like to note:

- I felt it was important to provide reliable feedback via [blinking the onboard LED](https://github.com/cdzombak/esp8266-basic-wifi/blob/basic-wifi/src/led.cpp), which also provides a demonstration of using timer interrupts.
- I demonstrate some basic usage of [a cooperative multitasking scheduler for Arduino](https://www.arduino.cc/reference/en/libraries/taskscheduler/).
- These demo projects make the board show up as a `.local` domain on the network using [multicast DNS](http://www.multicastdns.org).
- Contrary to code from the majority of Ardunio/ESP8266 HTTPS tutorials & forums, [my HTTPS demo project](https://github.com/cdzombak/esp8266-basic-wifi/blob/https/src/main.cpp) fetches the current time and loads a certificate root store, allowing for secure and flexible TLS certificate validation.

Notable third-party open-source libraries demonstrated:

- [InfluxDB client for Arduino](https://github.com/cdzombak/esp8266-basic-wifi/blob/influxdb/src/main.cpp)
- [ArduinoJSON](https://github.com/cdzombak/esp8266-basic-wifi/blob/https/src/main.cpp)

Blog posts for these demos will eventually be posted at [dzombak.com/blog](https://www.dzombak.com/blog). Thus far:

- [ESP8266 + PlatformIO Serial Console Monitoring with Exception Decoding *(October 7, 2021)*](https://www.dzombak.com/blog/2021/10/ESP8266-PlatformIO-Serial-Console-Monitoring-with-Exception-Decoding.html)
- [Debugging an Intermittent Arduino/ESP8266 ISR Crash *(October 7, 2021)*](https://www.dzombak.com/blog/2021/10/Debugging-an-Intermittent-Arduino-ESP8266-ISR-Crash.html)
- [Reusing an ESP8266HTTPClient *(October 8, 2021)*](https://www.dzombak.com/blog/2021/10/Reusing-an-ESP8266HTTPClient.html)
- [How to enable debug logging for Arduino's ESP8266HTTPClient with PlatformIO *(October 8, 2021)*](https://www.dzombak.com/blog/2021/10/ESP8266-How-to-enable-debug-logging-for-Arduino-s-ESP8266HTTPClient-with-PlatformIO.html)
- [Initial Impressions of ESP8266 + Arduino *(October 15, 2021)*](https://www.dzombak.com/blog/2021/10/Initial-Impressions-of-ESP8266-Arduino.html)
- [Shipping Data to InfluxDB using Arduino + ESP8266 *(October 15, 2021)*](https://www.dzombak.com/blog/2021/10/Shipping-Data-to-InfluxDB-using-Arduino-ESP8266.html)
- [HTTPS Requests with a Root Certificate Store on ESP8266 + Arduino *(October 28, 2021)*](https://www.dzombak.com/blog/2021/10/HTTPS-Requests-with-a-Root-Certificate-Store-on-ESP8266-Arduino.html)
- [HTTPS Requests with a Small Set of Root Certificates on ESP8266 + Arduino *(October 29, 2021)*](https://www.dzombak.com/blog/2021/10/HTTPS-Requests-with-a-Small-Set-of-Root-Certificates-on-ESP8266-Arduino.html)
