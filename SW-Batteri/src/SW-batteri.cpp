#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <EEPROM.h>

// Pinout
#define BUILTIN_LED 4

// EEPROM størrelse allokasjon 512 bytes er tilgjengelig på ESP32
#define EEPROM_SIZE 4

// Globale variabler
int motorSpeed = 50;
int state = 0;
bool processSensorData = false;
uint8_t sensorDataIndex = 0;
uint8_t lastPercentage = 100;

// Variabler for å holde styr på batteriet
float batteryLevel = 100.0;
unsigned long batteryTimer = 0;
float batteryCapacity = 100.0;
unsigned int batteryChargeCycles = 0;

// Setter opp timer og meldingsvariabler
unsigned long lastMsg = 0;
char msg[50];

// Nettverksinformasjon
const char *ssid = "RED_VELVET 4802";
const char *password = "03175B|j";

// MQTT Server IP-adresse
const char *mqtt_server = "84.52.229.122";

// Setter opp WiFiClient
WiFiClient espClient;
PubSubClient client(espClient);

class BankAccount
{
private:
    int balance = 0; // Penger på kontoen
    const char fromAccount[50] = "zumo"; // Kontoen som skal brukes
    char topic[50]; // Topic placeholder
    String accounts[2] = {"charger", "toll"}; // Kontoer som kan overføres til
    const char pin[5] = "1337"; // Pin for å overføre penger

public:
    void transfer(int amount, char *toAccount)
    {
        if (balance >= amount) // Sjekker om det er nok penger på kontoen
        {
            bool inArray = false;
            for (int i = 0; i < 2; i++) // Sjekker om kontoen som skal overføres til er i arrayen
            {
                if (accounts[i] == toAccount)
                {
                    inArray = true; 
                    break;
                }
            }
            if (inArray)
            {
                char payload[50]; 
                itoa(amount, payload, 10); // Konverterer amount til char array
                strcat(payload, pin); // Legger pin på slutten av payloaden

                strcpy(topic, fromAccount); // Legger kontoen som skal overføre penger på topicen
                strcat(topic, "/bank/transfer/"); 
                strcat(topic, toAccount); // Legger kontoen som skal motta penger på topicen
                client.publish(topic, payload); // Sender melding til serveren
            }
            else
            {
                Serial.println("Account not in local database"); 
            }
            requestServerBalance(); // Forespør balanse fra server
        }
        else
        {
            Serial.println("Not enough money"); 
        }
    }
    int getBalance()
    {
        return balance; 
    }
    void updateLocalBalance(int amount)
    {
        balance = amount; // Oppdaterer balansen lokalt
    }
    void requestServerBalance() // Forespør balanse fra server
    {
        strcpy(topic, fromAccount); 
        strcat(topic, "/bank/getBalance");
        client.publish(topic, pin);
    }
};
BankAccount account;

void setup_wifi()
{
    delay(10);

    // Starter med å koble til WiFi
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    // Venter på at WiFi skal koble til
    while (WiFi.status() != WL_CONNECTED) 
    {
        delay(500);
        Serial.print(".");
    }

    // Når WiFi er koblet til
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP()); // Printer IP-adressen til ESP32
}

// Denne funksjonen kjøres når en melding mottas
void callback(char *topic, byte *message, unsigned int length) 
{
    Serial.print("Message arrived on topic: ");
    Serial.print(topic);
    Serial.print(". Message: ");
    String messageTemp;

    for (int i = 0; i < length; i++)
    {
        Serial.print((char)message[i]);
        messageTemp += (char)message[i];
    }
    Serial.println();

    // Hvis en melding mottas på topicen esp32/output, sjekker du om meldingen er enten "on" eller "off".
    // Endrer output-staten i henhold til meldingen
    if (String(topic) == "esp32/output")
    {
        Serial.print("Changing output to ");
        if (messageTemp == "on")
        {
            Serial.println("on");
            digitalWrite(BUILTIN_LED, HIGH);
        }
        else if (messageTemp == "off")
        {
            Serial.println("off");
            digitalWrite(BUILTIN_LED, LOW);
        }
    }

    // Når balansen mottas fra serveren
    if (String(topic) == "zumo/bank/currentBalance") 
    {
        Serial.print("Current balance: ");
        Serial.println(messageTemp);
        int currentBalance = messageTemp.toInt();
        account.updateLocalBalance(currentBalance);
    }

    // Når det er en feil fra serveren
    if (String(topic) == "zumo/bank/error") 
    {
        Serial.print("Error: ");
        Serial.println(messageTemp);
    }
    
    // Når batterinivået mottas gjennom MQTT
    if (String(topic) == "zumo/battery/newLevel") 
    {
        Serial.print("Battery level: ");
        Serial.println(messageTemp);
        batteryLevel = messageTemp.toFloat();
    }

    // Når tilstand Zumo skal endres til mottas gjennom MQTT
    if (String(topic) == "zumo/state") 
    {
        Serial1.print("change state");
        Serial.println(messageTemp);
        if (messageTemp == "charging")
        {
            state = 0; // charging
        }
        else if (messageTemp == "toll")
        {
            state = 1; // toll
        }
        else if (messageTemp == "idle")
        {
            state = 2; //idle
        }
    }
}

void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect("Zumo"))
        {
            Serial.println("connected");
          // Subscribe or resubscribe to a topic
          client.subscribe("esp32/output");
          client.subscribe("zumo/bank/currentBalance");
          client.subscribe("zumo/bank/error");
          client.subscribe("zumo/battery/newLevel");
          client.subscribe("zumo/state");
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void processSerial2() 
{
    while (Serial.available()) 
    {   
        String input = Serial.readStringUntil('\n');
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, input);
        const char* topic = doc["topic"];
        if (topic == "zumo/sensorData")
        {
            const char* payload = doc["sensorArray"];
            client.publish(topic, payload);
        }
        else if (topic == "zumo/speed")
        {
            motorSpeed = doc["speed"];
            Serial.println("Motor speed: ");
            Serial.println(motorSpeed);
            char tempString[10];
            itoa(motorSpeed, tempString, 10);
            client.publish(topic, tempString);
            // Maksfart: 400 (0,78 m/s)
            // Deler på 500 for å få fart i m/s
            // Dette foregår på node-red
        }
    }
}

void calculateBatteryLevel()
{
    float speedFactor = 0.01;
    unsigned long now = millis();
    float batteryPercentage;

    // Oppdaterer batterinivået hvert sekund
    if (now - batteryTimer > 1000 && motorSpeed > 0)
    {   
        batteryTimer = now;
        batteryLevel -= motorSpeed * speedFactor; // P=mv
        batteryPercentage = batteryLevel / batteryCapacity * 100;
        // Lagrer batterinivået i EEPROM
        EEPROM.write(0, round(batteryLevel)); 
        EEPROM.commit();
    }

    if (batteryLevel < 0)
    {
        batteryLevel = 0.0;
    
        // TODO: Send message to zumo to stop
        Serial2.println("Battery empty"); //REPLACE MESSAGE LATER

        //death sound

    }
    else if (batteryPercentage <= 10 && lastPercentage <= 10) // Kjører bare en gang
    {
        lastPercentage = batteryPercentage;
        batteryCapacity -= 5; // 5% av batterikapasiteten er tapt på grunn av lavt batterinivå
        // Si fra til Zumo at batterinivået er kritisk lavt

    }
    else if (batteryPercentage <= 40 && lastPercentage <= 40) // Kjører bare en gang
    {
        lastPercentage = batteryPercentage;
        // Si fra til Zumo at batterinivået er lavt

        // Eventuelt se etter ladestasjon her
    }
    else if (batteryPercentage > 100)
    {
        batteryLevel = batteryCapacity;
    }
}

void calculateBatteryHealth()
{
       
}

void changeZumoState()
{
    // Kjører ved mottatt melding fra MQTT-server 
    // Sender melding over serial for å endre tilstand på Zumo
    // States:
    // drive
    // charge 
    // trash
    // reverse
}
void setup()
{
    Serial.begin(115200);
    Serial2.begin(9600); // Start seriell kommunikasjon med Zumo
    EEPROM.begin(EEPROM_SIZE); // Start EEPROM
    batteryLevel =  EEPROM.read(0); // Leser batterinivå fra EEPROM
    batteryCapacity = EEPROM.read(2); // Leser batterikapasitet fra EEPROM

    setup_wifi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    pinMode(BUILTIN_LED, OUTPUT);

    // Koble til MQTT-server
    reconnect();    

    // MQTT kode:
    // Synkroniserer lokal konto med server
    account.requestServerBalance();
}

void loop()
{
    calculateBatteryLevel();
    processSerial2();

    if (!client.connected())
    {
        reconnect();
    }
    client.loop();

    unsigned long now = millis();
    if (now - lastMsg > 1000)
    {
        lastMsg = now;

        char tempString1[10];
        dtostrf(batteryLevel, 1, 2, tempString1);
        client.publish("zumo/battery/currentLevel", tempString1);
    }
}