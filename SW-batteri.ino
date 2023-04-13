#include <Arduino.h>
#include <Serial.h>

uint8_t batteryLevel = 100;

void setup() 
{
    Serial.begin(115200);
    Serial2.begin(9600);
}

void loop() 
{
    calculateBatteryLevel();
}

void MQTTsetup()
{
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

void calculateBatteryLevel() {
    uint8_t motorSpeed;
    uint8_t speedFactor = 0.1;
    while (Serial2.available() > 0) 
    {
        motorSpeed = Serial2.read();
    }
    //P=mv
    batteryLevel -= motorSpeed * speedFactor;
}
