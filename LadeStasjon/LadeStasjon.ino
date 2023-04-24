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

  insufficientFunds,

  chargeError,

  waitingForChargeTime,

  waitingForZumo,

  lowBatteryCharge,

  userDefinedCharge,
  };

chargeProgress lastState;

int counter = 0;

//Reset time
long resetMillis;

// Legger til mqtt brokeren
const char* mqtt_server = "84.52.229.122";
 
//Topic variabler med navn for innkommende data, lagret i ett array
int garage = 0, fullCharge = 0, fastCharge = 0;


int chargeToPercentage = 0, readyForCharge = 0, chargeType = 0, chargeTime = -1, chargeButton = 0;

float accountBalance = 50;

char* currentTopic = "";

// Array for å lagre topics som er subscribet på.
char* topics[10] = {"chargingStation/garage", "chargingStation/fullCharge", "chargingStation/fastCharge", "chargingStation/chargeToPercentage", "chargingStation/chargeTime", "chargingStation/chargeButton", "chargingStation/reset", "zumo/ready", "zumo/accountBalance", "zumo/lowBattery"};

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
  
  for (int i = 0; i < length; i++) {                              //Gjør message fra mqtt om til String
    messageTemp += (char)message[i];
  }

  checkTopic(currentTopic, messageTemp);                                // Her skjekker vi hvilket topic som er mottatt og endrer topic variabelen. 


}



void checkTopic(char* topic, String messageTemp) {
  if ((resetMillis + 600) > millis()) {                            //Forsikrer at reset knappen ikke lar andre signaler komme gjennom
    return;
  }
  
  
  if (strcmp(topic, "chargingStation/garage") == 0) {                   //Skjekker om topic er lading_til_prosent, setter variablen lik mqtt verdien.
    garage = messageTemp.toInt();
    Serial.println(topic);
    chargeButton = 0;
  }

  if (strcmp(topic, "chargingStation/fullCharge") == 0) {                   //Skjekker om topic er lading_til_prosent, setter variablen lik mqtt verdien.
    fullCharge = messageTemp.toInt();
    Serial.println(topic);
    chargeButton = 0;
  }

  if (strcmp(topic, "chargingStation/fastCharge") == 0) {                   //Skjekker om topic er lading_til_prosent, setter variablen lik mqtt verdien.
    fastCharge = messageTemp.toInt();
    Serial.println(topic);
    chargeButton = 0;
  }
  

  if (strcmp(topic, "chargingStation/chargeToPercentage") == 0) {                   //Skjekker om topic er lading_til_prosent, setter variablen lik mqtt verdien.
    chargeToPercentage = messageTemp.toInt();
    Serial.println(topic);
    chargeButton = 0;
  }
  else if (strcmp(topic, "chargingStation/chargeTime") == 0) {             //Skjekker om topic er ladetid, setter verdien lik mqtt verdien (sekunder etter midnatt).
    chargeTime = messageTemp.toInt();
    Serial.println(topic);
    chargeButton = 0;
  }
  else if (strcmp(topic, "chargingStation/chargeButton") == 0) {
    Serial.println(topic);
    chargeButton = 1;
  }
  else if (strcmp(topic, "chargingStation/reset") == 0) {           //Skjekker om reset-knappen er trykket, vi lagrer tiden for å lage et delay. (delay funksjonen fungerer ikke her)
    resetMillis = millis();
    chargeTime = -1;
    chargeButton = 0;
    readyForCharge = 0;
    fullCharge = 0;
    chargeToPercentage = 0;
    garage = 0;
    Serial.println(topic);
  }
  else if (strcmp(topic, "zumo/ready") == 0) {
    readyForCharge = 1;
    Serial.println(topic);
  }
  else if (strcmp(topic, "zumo/accountBalance") == 0) {
    const char* messageTemp_cstr = messageTemp.c_str();
    accountBalance = atof(messageTemp_cstr);
    Serial.println(topic);
  }
  
  
}

void startCharge() {     //Denne funksjonen setter igang ladingen
  //Kalkuler tiden for ladingen
  //calculatePercentage
  if (chargeType != 1) {
    
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
      lastState = initializeCharge();

      case chargeNotRequested:
        break;
        
      case lowBatteryCharge:
        //startCharge(lowBatteryCharge);
        if(counter != 1) {
          counter = 1;
          Serial.println("LowBatteryCharge");
        }
        break;
        
      case userDefinedCharge:
        //startCharge(userDefinedCharge)
        if(counter != 2) {
          counter = 2;
          Serial.println("UserDefinedCharge");
          chargeTime = -1; 
        }
        break;
        
      case waitingForZumo: 
        //client.publish goToStation
        if(counter != 3) {
          counter = 3;
          Serial.println("Waiting for topic on zumo/ready");
        }
        break;

      case insufficientFunds:
        if(counter != 4) {
        counter = 4;
        Serial.println("Insufficient Funds for Charge");
        }
        break;

      case chargeError:
        if(counter != 5) {
        counter = 5;
        Serial.println("Charge Configuration Error");
        }
        break;
      case waitingForChargeTime:
        if (counter != 6) {
          counter = 6;
          Serial.println("Waiting for charge time");
        }
        
             
  
    //client.publish("(I am going to type in the topics here)", payload);
  }

}


  chargeProgress initializeCharge() {
  // Check if the charge button is pressed (chargeButton is true)
  if (chargeButton == 1) {
    // Check if the account balance is greater than 20
    if (accountBalance > 20) {
      // Check if chargeType is 1 and readyForCharge is 1
      
        if (readyForCharge == 1) {
          if (chargeType == 1) {
            return userDefinedCharge;
          }
          // Check if chargeTime is -1 or greater than the current time, and readyForCharge is 1
          return lowBatteryCharge;
          }
    
      
        if ((fullCharge == 1 && chargeToPercentage == 0) || (fullCharge == 0 && chargeToPercentage > 0)) {
          if ((chargeTime == -1) || (getTimeInMilliSeconds() > chargeTime)) {
            chargeType = 1;                           //ChargeType 1 for userDefinedCharge
            return waitingForZumo;
          }
          return waitingForChargeTime;
        }
        return chargeError;
        
    }
    return insufficientFunds;
  }
  else if (chargeButton == 2) {    //Vi innfører en tilstand 2 for ladeknappen, slik at vi kan holde oss i samme tilstand og kreve chargeButton for å endre tilstand.
    return lastState;
  }
  return chargeNotRequested;
}

