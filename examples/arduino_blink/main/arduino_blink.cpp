// arduino_blink — classic Arduino blink on GPIO2.
#include "Arduino.h"

#define LED_PIN 2

void setup()
{
    pinMode(LED_PIN, OUTPUT);
    Serial.begin(115200);
    Serial.println("arduino_blink ready");
}

void loop()
{
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED ON");
    delay(500);
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED OFF");
    delay(500);
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
