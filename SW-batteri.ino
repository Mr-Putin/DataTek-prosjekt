#include <Arduino.h>
#include <Serial.h>
#include <esp_now.h>
#include <WiFi.h>

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint8_t batteryLevel;

String success;

typedef struct struct_message
{
    uint8_t batteryLevel;
} struct_message;

esp_now_peer_info_t peerInfo;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    Serial.print("\r\nLast Packet Send Status:\t"); 
    // \r\n is used to move the cursor to the beginning of the next line.
    // \t is used to move the cursor to the next tab stop.
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
    if (status == 0) 
    {
        success = "Delivery Success";
    }
    else
    {
        success = "Delivery Fail";
    }
}

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    Serial.print("Last Packet Recv Status:\t");
    Serial.println("Delivery Success");
    struct_message *message = (struct_message *)incomingData;
    Serial.print("Battery level:\t");
    batteryLevel = message->batteryLevel;
}

void espSetup()
{
    //Set devide as a Wi-Fi Station
    WiFi.mode(WIFI_STA);

    // Init ESP-NOW
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // Once ESPNow is successfully Init, we will register for Send CB to
    esp_now_register_send_cb(OnDataSent);

    // Register peer
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0; // pick a channel
    peerInfo.encrypt = false;

    // Add peer
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add peer");
        return;
    }
    // Register for a callback function that will be called when data is received
    esp_now_register_recv_cb(onDataRecv);
}

void setup() 
{
    Serial.begin(115200);
    Serial2.begin(9600);
    espSetup();
}

void loop() 
{
    getReadings();
    
    // Send message via ESP-NOW
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&batteryLevel, sizeof(batteryLevel));

    // Check if the data was sent with success
    if (result == ESP_OK)
    {
        Serial.println("Sent with success");
    }
    else
    {
        Serial.println("Error sending the data");
    }
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