
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// IR transmitter pin
const uint16_t kIrLed = 5;

IRsend irsend(kIrLed); 

// Makes raw data
uint16_t rawData[67] = {9000, 4500, 650, 550, 650, 1650, 600, 550, 650, 550,
                        600, 550, 650, 1650, 650, 1650, 650, 550, 600, 1650,
                        650, 1650, 650, 550, 650, 550, 650, 1650, 650, 550,
                        650, 550, 650, 550, 600, 550, 650, 550, 650, 550,
                        650, 1650, 600, 550, 650, 1650, 650, 1650, 650, 1650,
                        650, 1650, 650, 1650, 650, 1650, 600};

void setup() {
  Serial.begin(115200);
  irsend.begin();
}

void loop() {
  Serial.println("Send rawData");
  // Send the raw data at 38kHz
  irsend.sendRaw(rawData, 67, 38); 
  delay(50);
}