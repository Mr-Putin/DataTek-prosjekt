#include <Arduino.h>
#include <Serial.h>

uint8_t batteryLevel=100;

String success;

typedef struct struct_message
{
    uint8_t batteryLevel;
} struct_message;

void setup() 
{
    Serial.begin(115200);
    Serial2.begin(9600);
}

void loop() 
{
    getReadings();
}
void setBatteryLevel() {
    uint8_t motorSpeed;
    uint8_t speedFactor = 0.1;
    while (Serial2.available() > 0) 
    {
        motorSpeed = Serial2.read();
    }
    //Calculate battery usage based on motor speed and e=1/2*mv^2
    batteryLevel -= (motorSpeed * speedFactor)^2 * 0.5;
}