#define DEBUG // Aktiverer debug mode. Kommenter ut ved ferdigstillelse

#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Preferences.h>

Preferences preferences; // Brukes for å lagre data i flash
// Globale variabler
int motorSpeed = 200;
bool processSensorData = false;
uint8_t sensorDataIndex = 0;
uint8_t lastPercentage = 100;

// Variabler for å holde styr på batteriet
float batteryPercentage = 100.0; 
unsigned long batteryTimer = 0; // Timer for å holde styr på hvor lenge det har gått 
                                // siden sist batterioppdatering for simulering
float batteryCapacity = 3000.0; // Batterikapasitet i mAh
unsigned int batteryChargeCycles = 0; // Antall ganger batteriet har blitt ladet

float powerConstant = 2; // Faktor for å justere forbruket til Zumo

// Setter opp timer og meldingsvariabler
unsigned long lastMsg = 0;
char msg[50];

// Nettverksinformasjon
const char *ssid = "RED_VELVET 4802";
const char *password = "03175B|j";

// MQTT Server IP-adresse
const char *mqtt_server = "84.52.229.122";

typedef enum { // Definerer tilstandene til Zumo
    DRIVE,
    CHARGE,
    GARAGE,
    TRASH,
    REVERSE
} State;
State state = DRIVE; // Setter tilstanden til Zumo til DRIVE
int lastState = 0; // Variabel for å holde styr på forrige tilstand

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
    void transfer(int amount, String toAccount)
    {
        char recipient[10];
        toAccount.toCharArray(recipient, 50); // Konverterer String til char array
        if (balance >= amount) // Sjekker om det er nok penger på kontoen
        {
            bool inArray = false;
            for (int i = 0; i < 2; i++) // Sjekker om kontoen som skal overføres til er i arrayen
            {
                if (accounts[i] == recipient)
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
                strcat(topic, recipient); // Legger kontoen som skal motta penger på topicen
                client.publish(topic, payload); // Sender melding til serveren
            }
            else
            {
                Serial.print(recipient);
                Serial.println(" not in local database."); 
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
        batteryPercentage = messageTemp.toFloat();
        lastPercentage = batteryPercentage;

        // Midlertidig
        #ifdef DEBUG
        batteryChargeCycles++; // Øker antall ladninger
        batteryCapacity -= 5; // Reduserer kapasiteten
        preferences.putDouble("batteryCapacity", batteryCapacity); // Lagrer kapasiteten til batteriet i flash
        preferences.putUInt("batteryChargeCycles", batteryChargeCycles); // Lagrer antall ladninger i flash
        #endif
    }

    // Når batteriet blir byttet ut
    if (String(topic) == "zumo/battery/changed") 
    {
        Serial.print("Battery capacity: ");
        Serial.println(messageTemp);
        batteryCapacity = messageTemp.toFloat();
        batteryPercentage = 100;
        batteryChargeCycles = 0;
        preferences.putDouble("batteryCapacity", batteryCapacity); // Lagrer kapasiteten til batteriet i flash
        preferences.putDouble("batteryPercentage", batteryPercentage); // Lagrer prosenten til batteriet i flash"
        preferences.putUInt("batteryChargeCycles", batteryChargeCycles); // Lagrer antall ladninger i flash

    }

    // Når tilstand Zumo skal endres til mottas gjennom MQTT
    if (String(topic) == "zumo/state") 
    {
        Serial1.print("change state");
        Serial.println(messageTemp);
        if (messageTemp == "drive")
        {
            state = DRIVE;
        }
        else if (messageTemp == "charge")
        {
            state = CHARGE; 
        }
        else if (messageTemp == "trash")
        {
            state = TRASH; 
        }
        else if (messageTemp == "reverse")
        {
            state = REVERSE;
        }
    }
    if (String(topic) == "charger/start") 
    {
        if (messageTemp = "garage")
        {
            state = GARAGE;
        }
        else
        {
            state = CHARGE;
        }
    }
    if (String(topic) == "charger/stop") 
    {
        account.transfer(messageTemp.toInt(), "charger"); // Overfører penger til laderen
        batteryChargeCycles++; // Øker antall ladninger
        batteryCapacity -= 5; // Reduserer kapasiteten til batteriet som følge av oppladning
        preferences.putDouble("batteryCapacity", batteryCapacity); // Lagrer kapasiteten til batteriet i flash
        preferences.putUInt("batteryChargeCycles", batteryChargeCycles); // Lagrer antall ladninger i flash
        state = DRIVE;
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
    while (Serial2.available()) 
    {   
        String input = Serial2.readStringUntil('\n');
        Serial.printf("Received message Serial2: %s\n", input);
        StaticJsonDocument<200> doc;
        deserializeJson(doc, input);
        const char* topic = doc["topic"];
        if (!strcmp(topic, "zumo/sensorData"))
        {
            const char* payload = doc["sensorverdier"];
            client.publish(topic, payload);
        }
        else if (!strcmp(topic, "zumo/speed"))
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
    unsigned long now = millis();

    // Oppdaterer batterinivået hvert sekund
    if (now - batteryTimer > 1000 && motorSpeed > 0)
    {   
        batteryTimer = now;
        float batteryLevel = batteryPercentage / 100 * batteryCapacity; // Regner ut batterinivået i mAh
        batteryLevel -= motorSpeed * powerConstant ; // P=mv
        batteryPercentage = batteryLevel / batteryCapacity * 100; // Regner ut prosent

        preferences.putDouble("batteryPercentage", batteryPercentage); // Lagrer batterinivået i flash
        preferences.end();
    }

    if (batteryPercentage < 0)
    {
        batteryPercentage = 0.0;
    
        // TODO: Send message to zumo to stop
        //Serial2.println("Battery empty"); //REPLACE MESSAGE LATER

        //death sound

    }
    else if (batteryPercentage <= 10 && lastPercentage > 10) // Kjører bare en gang
    {
        Serial.println("Battery low");
        lastPercentage = batteryPercentage;
        batteryCapacity -= 5; // 5% av batterikapasiteten er tapt på grunn av lavt batterinivå
        preferences.putDouble("batteryCapacity", batteryCapacity);

        // Si fra til Zumo at batterinivået er kritisk lavt
    }
    else if (batteryPercentage <= 40 && lastPercentage > 40) // Kjører bare en gang
    {
        lastPercentage = batteryPercentage;
        // Si fra til Zumo at batterinivået er lavt

        // Eventuelt se etter ladestasjon her
    }
    else if (batteryPercentage > 100)
    {
        batteryPercentage = 100;
    }
}

void calculateBatteryHealth()
{
    // Hvis ferdig med lading, oppdater antall ladninger
}

void controlZumo()
{
    // Kjører ved mottatt melding fra MQTT-server 
    // Sender melding over serial for å endre tilstand på Zumo
    // States:
    // drive
    // charge 
    // trash
    // reverse
    StaticJsonDocument<200> doc;
    String json;
    doc["topic"] = "zumo/state";
    if (lastState != state)
    {
        lastState = state;
        switch (state)
        {
            case DRIVE:
                doc["state"] = "drive";
                break;
            case CHARGE:
                doc["state"] = "charge";
                break;
            case GARAGE:
                doc["state"] = "garage";
                break;
            case TRASH:
                doc["state"] = "trash";
                break;
            case REVERSE:
                doc["state"] = "reverse";
                break;
        }
        serializeJson(doc, json);
        Serial2.println(json);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(9600); // Start seriell kommunikasjon med Zumo
    preferences.begin("my-app", false); // Start preferences for å lagre variabler
    batteryPercentage = preferences.getDouble("batteryPercentage", batteryPercentage); // Leser batterinivå fra flash
    batteryCapacity = preferences.getDouble("batteryCapacity", batteryCapacity); // Leser batterikapasitet fra flash
    batteryChargeCycles = preferences.getUInt("batteryChargeCycles", batteryChargeCycles); // Leser antall ladninger fra flash

    setup_wifi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

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
        // Sender over MQTT
        char tempString1[10];
        dtostrf(batteryPercentage, 1, 2, tempString1);
        client.publish("zumo/battery/currentLevel", tempString1);

        char tempString2[10];
        dtostrf(batteryCapacity, 1, 2, tempString2);
        client.publish("zumo/battery/capacity", tempString2);

#ifdef DEBUG
    Serial.print("Battery percentage: ");
    Serial.println(batteryPercentage);
    Serial.print("Battery capacity: ");
    Serial.println(batteryCapacity);
    Serial.print("Battery charge cycles: ");
    Serial.println(batteryChargeCycles);
    Serial.print("State: ");
    Serial.println(state);
    Serial.print("Balance: ");
    Serial.println(account.getBalance());
    Serial.println();
#endif
        lastMsg = now;
    }
}
