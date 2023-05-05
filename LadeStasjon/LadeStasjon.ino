//LadeStasjon til Smart City

//Inkluderer nødvendige bibloteker
#include <WiFi.h>                     //Nettverk
#include <PubSubClient.h>             //MQTT                   
#include "time.h"                     //For tiden
#include <Wire.h>                     //For Oled           
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//SSID og Passord til nettverket
const char* ssid = "mqttnetwork";
const char* password = "esp32esp32";

//Konstanter for å hente tiden
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

//Definerer størrelsen i piksler på OLED Displayet 
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);    // Deklarerer displayet 
                                                                     
//De ulike tilstandene ladeStasjonen er i på forskjellige tidspunkter
enum chargeProgress {
  chargeNotRequested,         //Lading er ikke forespurt
  insufficientFunds,          //Bruker har utilstrekkelig penger til lading
  chargeError,                //Ugyldig ladingsvalg
  waitingForChargeTime,       //Venter til ladingen skal igangsettes etter bestilling på tid
  waitingForZumo,             //Venter på Zumoen skal ankommme ladestasjonen
  lowBatteryChargeStart,      //Lading når Zumoen er kommet under 20% har startet
  userDefinedChargeStart,     //Lading fra Node-Red har startet
  batterySwapStart            //Bytte av batteri har startet
  };

chargeProgress lastState;                           
chargeProgress currentState = chargeNotRequested;

enum chargingOperation  {
  noChargeSelected,           //Tilstand der lading ikke er valgt
  lowBatteryCharge,           //Ladingstypen når Zumoen er kommet under 10%, automatisk av Zumoen
  userDefinedCharge,          //Ladingstypen for selvvalgt lading via Node-Red
  batterySwap,                //Bytte av batteriet på Zumoen via Node-Red
};

chargingOperation chargeOperation = noChargeSelected;  //Lager en tilstands variabel for hvilken ladeoperasjon som skal gjennomføres           

//Reset time
long resetMillis;

// Legger til mqtt broker ipen.
const char* mqtt_server = "84.52.229.122";
 
//Topic variabler i en struct
struct topicVariable {
  int garage;
  int fullCharge;
  int fastCharge;
  int chargeToPercentage;
  int readyForCharge;
  int chargeType;
  int chargeTime;
  int chargeButton;
  int lastPercentage;
  int currentBalance;
  float electricityPrice;
};

topicVariable topicVar = {0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0};   

// Array for å lagre topics som er subscribet på
char* topics[12] = {"chargingStation/garage", "chargingStation/fullCharge", "chargingStation/fastCharge", "chargingStation/chargeToPercentage", "chargingStation/chargeTime", "chargingStation/chargeButton", "chargingStation/reset", "zumo/ready", "zumo/battery/currentLevel", "zumo/lowBattery", "chargingStation/electricityPrice", "charger/batterySwap"};


//Setter opp WiFi client og PubSubClient (for mqtt publishing og subscribing)
WiFiClient chargingStation;
PubSubClient client(chargingStation);
long lastMsg = 0;
char msg[50];
int value = 0;


//Setup funksjonen som kjører en gang ved startup
void setup() {
  Serial.begin(115200);
  
  //Kjører setup wifi
  setup_wifi();

  //Setter server ip og port. client.setcallback gjør at funksjonen receivedTopic runner hver gang vi mottar informasjon fra en subscribed topic.
  client.setServer(mqtt_server, 1883);
  client.setCallback(receivedTopic);

  //Initialiserer tiden
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  //OLED
  oledSetup();
}

//Setup Wifi funksjon som skal sette up WiFien 
void setup_wifi() {
  delay(20);

  // Kobler til nettverk
  WiFi.begin(ssid, password);

  //Venter til WiFi er connected.
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

//Funksjon for å få tiden på døgnet oppgitt i millisekunder etter midnatt. Node-Red opererer med samme tidsformat.
unsigned long getTimeInMilliSeconds() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){}
  unsigned long milliseconds = ((timeinfo.tm_hour+1) * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec)*1000;
  return milliseconds;
}

void receivedTopic(char* topic, byte* message, unsigned int length) {    //Funksjone kjører hver gang vi mottar informasjon fra en subscribed topic
  String messageTemp;
  char* currentTopic = topic;
  
  for (int i = 0; i < length; i++) {                                //Gjør message fra mqtt om til String
    messageTemp += (char)message[i];
  }

  checkTopic(currentTopic, messageTemp);                           //Skjekker hvilket topic som er mottatt
}

//Skjekker hvilket topic som er mottatt, lagrer meldingen i topic variablen 
void checkTopic(char* topic, String messageTemp) {
  if ((resetMillis + 600) > millis()) {                            //Forsikrer at reset knappen ikke lar andre signaler komme gjennom
    return;
  } 
  
  if (strcmp(topic, "chargingStation/garage") == 0) {              
    topicVar.garage = messageTemp.toInt();
  }

  if (strcmp(topic, "chargingStation/fullCharge") == 0) {          
    topicVar.fullCharge = messageTemp.toInt();
  }

  if (strcmp(topic, "chargingStation/fastCharge") == 0) {         
    topicVar.fastCharge = messageTemp.toInt();
  }
  
  if (strcmp(topic, "chargingStation/chargeToPercentage") == 0) {            
    topicVar.chargeToPercentage = messageTemp.toInt();
  }
  else if (strcmp(topic, "chargingStation/chargeTime") == 0) {               
    topicVar.chargeTime = messageTemp.toInt();
  }
  else if (strcmp(topic, "chargingStation/chargeButton") == 0) {             
    topicVar.chargeButton = 1;          
  }
  else if (strcmp(topic, "chargingStation/reset") == 0) {                    //Dersom reset knapp er trykket, reseter vi alle verdiene
    resetMillis = millis();
    topicVar.chargeTime = -1;
    topicVar.chargeButton = 0;
    topicVar.fastCharge = 0;
    topicVar.readyForCharge = 0;
    topicVar.fullCharge = 0;
    topicVar.chargeToPercentage = 0;
    topicVar.garage = 0;
    chargeOperation = noChargeSelected;
  }
  else if (strcmp(topic, "zumo/ready") == 0) {                          
    topicVar.currentBalance = messageTemp.toInt();
    topicVar.readyForCharge = 1;
  }
  else if (strcmp(topic, "zumo/battery/currentLevel") == 0) {           
    topicVar.lastPercentage = messageTemp.toFloat();
  }
  else if (strcmp(topic, "chargingStation/electricityPrice") == 0) {    
    topicVar.electricityPrice = messageTemp.toFloat();
  }
  else if(strcmp(topic, "charger/batterySwap") == 0) {
    chargeOperation = batterySwap;                                          //Setter ladeoperasjon til batteri bytte
  }
}

//Denne funksjonen kobler til MQTT
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      // Subscribe to all topics
      for (int i = 0; i<=11; i++) {
        client.subscribe(topics[i]);
      }
    } 
    else {
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void loop() {
  if (!client.connected()) reconnect();     //Forsikrer at den er koblet til MQTT
  
  client.loop();                            //Behandler MQTT for å motta meldinger
  
  currentState = initializeCharge();        //Setter currentState lik tilstanden som blir returnert fra initializeCharge

  checkForCharge();                         //Skjekker om lading eller bytte av batteri skal iverksettes, og iverksetter handlinger
}


//Funksjonen returnerer tilstanden ladestasjonen er i
chargeProgress initializeCharge() {
  if (topicVar.chargeButton == 1) {           //Skjekker om ladeknappen er trykket på
    if (topicVar.currentBalance > 5) {        //Skjekker om det er nok penger på konto til å starte lading
      if (topicVar.readyForCharge == 1) {     //Skekker om Zumo er klar til å lade
          return userDefinedChargeStart;  
        }
        //Skjekker at kun fullading eller lading til prosent er valgt.
        if ((topicVar.fullCharge == 1 && topicVar.chargeToPercentage == 0) || (topicVar.fullCharge == 0 && topicVar.chargeToPercentage > 0)) {      
           //Skjekker for lading innstilt etter tid
          if ((topicVar.chargeTime == -1) || (getTimeInMilliSeconds() > topicVar.chargeTime)) {                                                   
            return waitingForZumo;
          }
          return waitingForChargeTime;
        }
        return chargeError; 
    }
    return insufficientFunds;
  }
  else if(chargeOperation == batterySwap) {   //Skjekker om batteribytte er forespurt
    if (topicVar.currentBalance >= 20){       //Skjekker om det er nok penger
      if(topicVar.readyForCharge) {           //Skekker om Zumo er klar til å lade
        return batterySwapStart;
      }
      return waitingForZumo;
    }
    return insufficientFunds;
  }
  else if(topicVar.readyForCharge == 1) {    //Skjekker om Zumoen er klar til å lade, betyr at det er lavbatterilading
    if (topicVar.currentBalance > 5) {       //skjekker om det er nok penger
      return lowBatteryChargeStart;
      }
    return insufficientFunds;
    }
  
  return chargeNotRequested;            
 }


//Setter opp OLED
void oledSetup() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    for(;;);
  }
  delay(2000);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.display();
}

//Regner ut prisen for per prosent
float pricePerPercent(float tempPrice) {
  float price = tempPrice;    //Innfører en tempPrice slik at pris tileggene ikke tar prosent av hverandre (renters renter).
    
  if (topicVar.garage) {      //Tillegg på 20% dersom garasje er valgt
    price += tempPrice*0.2;
  }

  if(topicVar.fastCharge) {   //Tillegg på 35% dersom hurtiglading er valgt
    price += tempPrice*0.35;
  }
  return price/5;             //Returnerer prisen delt på 5, altså: (pris per kWH) * 20/100, pris per prosent for 20kwH batteri.
}

//Funksjonen for å starte ladingen
void startCharge() {
  if (topicVar.chargeToPercentage == 0) {     //Skjekker om fullading er valgt
    topicVar.chargeToPercentage = 100;
  }

  int chargeSpeed = topicVar.fastCharge + 1;  //Definerer chargeSpeed
  float pricePercent = pricePerPercent(topicVar.electricityPrice); //Regner ut prisen per prosent
  
  display.clearDisplay();   
  display.setCursor(0,0);     
  if (topicVar.garage) display.println("garage");           //Printer garage dersom det er valgt
  if (topicVar.fastCharge) display.println("fastCharge");   //Printer fastCharge dersom det er valgt
  if (topicVar.garage == 0 && topicVar.fastCharge == 0) display.print("default");     //Printer default dersom vanlig lading
  display.display();
  delay(4000);

  int currentPercent = topicVar.lastPercentage;         
  
  for (currentPercent; currentPercent <= topicVar.chargeToPercentage && topicVar.currentBalance > 5; currentPercent++) {      //Itererer gjennom for-loopen som simulerer ladingen. Vi starter på nåværende prosent og skjekker om prosenten er lavere eller lik prosenten som skal lades til. Skjekkes også om kunden har råd til lading. Øker prosenten med en for hver iterasjon.
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Prcnt:");
    display.print(currentPercent);                          //Printer prosenten
    display.println("%");
    delay( (1/chargeSpeed) *50*log(currentPercent+1));       //Venter en angitt tid mellom hver iterasjon. Følger en logaritmisk funksjon slik ladingen går fortest i starten, og deretter saktere.
    display.print("Price:");
    display.print(int(pricePercent*((currentPercent+1) - topicVar.lastPercentage)));  //Printer prosenten den har
    display.print("kr");
    display.display();
    client.publish("zumo/battery/newLevel", String(currentPercent).c_str());          //Publisher prosenten
  }
  display.clearDisplay();
  display.setCursor(0,0);
  display.print("The total is:");
  display.print(pricePercent*((currentPercent+1) - topicVar.lastPercentage));         //Printer prisen
  display.print("kr");
  display.setCursor(0,35);
  display.print("Prcnt:");
  display.print(currentPercent - 1);                                                  //Printer den ferdig ladede prosenten 
  display.print("%");
  display.display();

  topicVar.readyForCharge = 0;                                                       
  client.publish("charger/stop", String(round(pricePercent*((currentPercent+1) - topicVar.lastPercentage))).c_str() );    //Publisher prisen
  
}

//Bytte av batteri
void startBatterySwap() {

  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(2);
  display.print("Battery Swap , 20kWh");
  display.display();
  delay(4000);
  for(int counter = 0; counter<=100; counter++) {       //For loop for bytte av batteriet
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(2); 
    display.print("Progress: ");
    display.print(counter);                             //Printer progresjonen
    display.print("%");
    delay(100);                                         //Setter et delay på 100ms
    display.display();  
  }
   display.clearDisplay();
   display.setCursor(0,0);
   display.setTextSize(2);
   display.println("Finished");
   display.println("Price:20kr");                       //Printer ferdig pris
   delay(100);
   display.display();
   
   chargeOperation = noChargeSelected;                 
   topicVar.readyForCharge = 0;
   client.publish("zumo/battery/swapped", String(50).c_str());    //Publisher prisen
    
}

//Undersøker currentState til ladestasjonen og iverksetter lading eller bytte av batteri om nødvendig
void checkForCharge() {
  
  switch (currentState) {                   //Vi skjekker hvilke tilstand currentState er i, og gjennomfører casen én gang
      case chargeNotRequested:
        if(currentState != lastState) {    
          lastState = currentState;
          client.publish("charger/state", String("Lading ikke forespurt").c_str());
        }
        break;
        
      case lowBatteryChargeStart:
        if(currentState != lastState) {
          lastState = currentState;
          client.publish("charger/state", String("Lav batteri lading har startet").c_str());
          startCharge();                    //Starter lading
        }
        break;
        
      case userDefinedChargeStart:
        //startCharge(userDefinedCharge)
        if(currentState != lastState) {
          lastState = currentState;
          client.publish("charger/state", String("Brukervalgt lading har startet").c_str()); 
          startCharge();                  //Starter lading
        }
        break;
        
      case waitingForZumo:                //Her sier vi ifra når zumoen skal kjøre til lading, bytte av batteri sendes direkte fra Node-Red til Zumo
        if(currentState != lastState) {
          lastState = currentState;
          client.publish("charger/state", String("Venter på zumo").c_str());
          if(topicVar.garage){
            client.publish("zumo/state", String("garage").c_str());
            break;
            }
          else if(!topicVar.garage) {
            client.publish("zumo/state", String("charge").c_str());
            break;
            } 
        }
        break;

      case batterySwapStart:
      if (lastState != currentState) {
          lastState = currentState;
          startBatterySwap();
          client.publish("charger/state", String("Bytte av batteri har startet").c_str());
      }
      break;
      
      case insufficientFunds:
        if(currentState != lastState) {
          lastState = currentState;
          client.publish("charger/state", String("Utilstrekkelig midler til lading").c_str());
        }
        break;

      case chargeError:
        if(currentState != lastState) {
          lastState = currentState;
          topicVar.chargeButton = 0;
          client.publish("charger/state", String("Error ved ladingsvalget.").c_str());
        }
        break;
        
      case waitingForChargeTime:
        if(currentState != lastState) {
          lastState = currentState;
          client.publish("charger/state", String("Venter til angitt tidspunkt").c_str());
        }
  }  
}
