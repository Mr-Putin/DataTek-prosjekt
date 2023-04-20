// Ladestasjon
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "time.h"
#include <Arduino.h>
#include <string.h>


// SSID og Passord til nettverk
const char* ssid = "iPhone";
const char* password = "lolololol";

//Konstanter for å hente tiden
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

//
enum chargeProgress {
  chargeNotRequested,

  lowBatteryCharge,

  userDefinedCharge,

  waitingForZumo
  };

//Reset time
long resetMillis;

// Legger til mqtt brokeren
const char* mqtt_server = "84.52.229.122";
 
//Topic variabler med navn for innkommende data, lagret i ett array
bool garage, fullCharge, fastCharge;

bool  defaultboolArr[3] = {false, true, false};

bool boolArr[3];

int chargeToPercentage, readyForCharge = 0, chargeType = 0, chargeTime = -1, chargeButton = 0;

float accountBalance = 50;

char* currentTopic = "";

// Array for å lagre topics som er subscribet på.
char* topics[10] = {"ladestasjon/garasje", "ladestasjon/fullading", "ladestasjon/hurtiglading", "ladestasjon/lading_til_prosent", "ladestasjon/ladetid", "ladestasjon/start_lading", "ladestasjon/reset", "zumo/ready", "zumo/accountBalance", "zumo/lowBattery"};

//Setter opp WiFi client og PubSubClient (for mqtt publishing og subscribing)
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;


void setup() {
  Serial.begin(115200);
  
  //Kjører setup wifi
  setup_wifi();

  //Setter server ip og port. client.setcallback gjør at funksjonen callback runner hver gang vi mottar informasjon fra en subscribed topic.
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  //Initialiserer tiden 
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

}

void setup_wifi() {
  delay(20);

  // Kobler til nettverk
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  //Venter til WiFi er connected.
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  //Printer ut den locale WiFi IP-addressen
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

//Funksjonen for å få tiden på døgnet oppgitt i millisekunder etter midnatt. Node-Red opererer med samme tidsformat, gunstig å ha tiden som et tall.
unsigned long getTimeInMilliSeconds() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
  }
  unsigned long milliseconds = ((timeinfo.tm_hour+1) * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec)*1000;
  return milliseconds;
}

void callback(char* topic, byte* message, unsigned int length) {    //Callback funksjonen som kjører hver gang vi mottar informasjon fra en subscribed topic
  String messageTemp;
  currentTopic = topic;
  
  for (int i = 0; i < length; i++) {                   //Gjør message fra mqtt om til String
    messageTemp += (char)message[i];
  }

  checkTopic(currentTopic, messageTemp);                                // Her skjekker vi hvilket topic som er mottatt og endrer topic variabelen. 



}

chargeProgress initializeCharge() {

 if((chargeButton) && (accountBalance > 20)) {
  if ((chargeType) && (readyForCharge)) {                               //Hvis zumoen har lavt batteri (under 5%) blir lading igangsatt med standard innstillinger. chargeType = 1 indikerer nødlading.
    return lowBatteryCharge;
  }
  else if ((chargeTime > 0) && (getTimeInMilliSeconds() > chargeTime) && (readyForCharge)) {
    Serial.println("Charging will start now");
    return userDefinedCharge;
  }
    return waitingForZumo;
 }
 return chargeNotRequested;
}


void checkTopic(char* topic, String messageTemp) {
  if ((resetMillis + 200) > millis()) {                            //Forsikrer at reset knappen ikke lar andre signaler komme gjennom
    return;
  }
  currentTopic = topic;
  for (int i = 0; i<3; i++) {

    if (strcmp(topic, topics[i]) == 0) {                        //Skjekker mellom de tre første variablene og setter variablene lik det som er mottatt over mqtt. true -> 1 og false -> 0.
      if (messageTemp == "true") {
        boolArr[i] = true;
        Serial.println(topic);
        break; 
      }
      else if (messageTemp == "false"){
        boolArr[i] = false;
        Serial.println(topic);
        break;
      }
    }
  }
  if (strcmp(topic, topics[3]) == 0) {                   //Skjekker om topic er lading_til_prosent, setter variablen lik mqtt verdien.
    chargeToPercentage = messageTemp.toInt();
    Serial.println(topic);
  }
  else if (strcmp(topic, topics[4]) == 0) {              //Skjekker om topic er ladetid, setter verdien lik mqtt verdien (sekunder etter midnatt).
    chargeTime = messageTemp.toInt();
    Serial.println(topic);
  }
  else if (strcmp(topic, topics[5]) == 0) {
    chargeButton = 1;
  }
  else if (strcmp(topic, topics[6]) == 0) {             //Skjekker om reset-knappen er trykket, vi lagrer tiden for å lage et delay. (delay funksjonen fungerer ikke her)
    resetMillis = millis();
    chargeTime = -1;
  }
  else if (strcmp(topic, "zumo/accountBalance") == 0) {
    const char* messageTemp_cstr = messageTemp.c_str();
    accountBalance = atof(messageTemp_cstr);
  }
  else if (strcmp(topic, "zumo/ready")){
    readyForCharge = 1;
    chargeType = messageTemp.toInt(); 
  }
}

void startCharge(bool boolArr[], int chargeToPercentage, float accountBalance) {     //Denne funksjonen setter igang ladingen
  //Publish drive to charging station
  //Wait for message received on topic /charge
  while(readyForCharge == 0) {
    Serial.println("THIS WORKS");
    client.loop();
  }
}



void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      
      // Subscribe to all topics
      for (int i = 0; i<=9; i++) {
        client.subscribe(topics[i]);
      }

      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;
  }

  switch (initializeCharge()) {
      case chargeNotRequested:
        break;
        
      case lowBatteryCharge:
        startCharge(defaultboolArr, 100, accountBalance);
        break;
        
      case userDefinedCharge:
        startCharge(boolArr, chargeToPercentage, accountBalance);
        chargeTime = -1; 
        break;
        
      case waitingForZumo: 
        //client.publish goToStation
        Serial.println("Waiting for topic on zumo/ready");
        break;
              
  }  
  
    //client.publish("(I am going to type in the topics here)", payload);
  }
