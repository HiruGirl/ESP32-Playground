// arduino_wifi_led — Arduino WiFi APIs: connect to the virtual AP, print the
// local IP, and run a web server that toggles the LED on GPIO2.
#include "Arduino.h"
#include <WiFi.h>

#define LED_PIN 2

const char *ssid = "ESP32Playground";
const char *password = "playground123";

WiFiServer server(80);
int ledOn = 0;

void setup()
{
    pinMode(LED_PIN, OUTPUT);
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    Serial.print("connecting to ");
    Serial.println(ssid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("connected, ip=");
    Serial.println(WiFi.localIP());
    server.begin();
    Serial.println("arduino_wifi_led ready");
}

void loop()
{
    WiFiClient client = server.available();
    if (client) {
        String request = client.readStringUntil('\r');
        client.flush();
        if (request.indexOf("GET /toggle") >= 0) {
            ledOn = !ledOn;
            digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
            Serial.printf("LED toggled -> %d\n", ledOn);
        }
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println();
        client.print("<html><body><h1>ESP32 Playground (Arduino)</h1><p>LED is ");
        client.print(ledOn ? "ON" : "OFF");
        client.println("</p><a href='/toggle'>Toggle</a></body></html>");
        client.stop();
    }
    delay(10);
}

// Custom entry point (arduino-as-IDF-component): run the sketch directly.
extern "C" void app_main(void)
{
    initArduino();
    setup();
    for (;;) {
        loop();
        delay(2);
    }
}
