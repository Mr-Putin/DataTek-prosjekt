//#define DEBUG // Aktiverer debug mode. Kommenter ut ved ferdigstillelse

#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Preferences.h>

Preferences preferences; // Brukes for å lagre data i flash
// Globale variabler
int motorSpeed = 0;
bool processSensorData = false;
uint8_t sensorDataIndex = 0;
uint8_t lastPercentage = 100;

// Variabler for å holde styr på batteriet
float batteryPercentage = 100.0; 
unsigned long lastBatteryUpdate = 0; // Timer for å holde styr på hvor lenge det har gått 
                                // siden sist batterioppdatering for simulering
float batteryCapacity = 20.0; // Batterikapasitet i kWh
unsigned int batteryChargeCycles = 0; // Antall ganger batteriet har blitt ladet

float powerConstant = 0.001; // Faktor for å justere forbruket til Zumo

unsigned long lastSpeedTime = 0; // Timer for å holde styr på hvor lenge det har gått 
                                 // siden sist hastighetsoppdatering for simulering


// Setter opp timer og meldingsvariabler
unsigned long lastMsg = 0;
char msg[50];

// Nettverksinformasjon
const char *ssid = "RED_VELVET";
const char *password = "abcd1234";

// MQTT Server IP-adresse
const char *mqtt_server = "84.52.229.122";

typedef enum { // Definerer tilstandene til Zumo
    DRIVE, // Linjefølging
    CHARGE, // Normal lading
    GARAGE, // Lading i parkeringshus
    REVERSE, // Linjefølging i revers
    CONTROL, // Manuell kontroll med joystick
    STOP, // Stopper Zumo
    CALIBRATE // Kalibrerer sensorene
} State;
State state = DRIVE; // Setter tilstanden til Zumo til DRIVE
int lastState = 0; // Variabel for å holde styr på forrige tilstand

// Setter opp WiFiClient
WiFiClient zumoClient;
PubSubClient client(zumoClient);

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
    }

    // Når batteriet blir byttet ut
    if (String(topic) == "zumo/battery/swapped") 
    {
        Serial.print("Battery capacity: ");
        Serial.println(messageTemp);
        batteryCapacity = messageTemp.toFloat();
        batteryPercentage = 100;
        batteryChargeCycles = 0;
        
        preferences.putDouble("batCap", batteryCapacity); // Lagrer kapasiteten til batteriet i flash
        preferences.putDouble("batPrcnt", batteryPercentage); // Lagrer prosenten til batteriet i flash"
        preferences.putUInt("batCycles", batteryChargeCycles); // Lagrer antall ladninger i flash
    }

    // Når tilstand Zumo skal endres til mottas gjennom MQTT
    if (String(topic) == "zumo/state") 
    {
        Serial.println(messageTemp);
        if (messageTemp == "drive")
        {
            state = DRIVE;
        }
        else if (messageTemp == "charge")
        {
            state = CHARGE; 
        }
        else if (messageTemp == "garage")
        {
            state = GARAGE; 
        }
        else if (messageTemp == "reverse")
        {
            state = REVERSE;
        }
        else if (messageTemp == "control")
        {
            state = CONTROL;
        }
        else if (messageTemp == "stop")
        {
            state = STOP;
        }
        else if (messageTemp == "calibrate")
        {
            state = CALIBRATE;
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
        Serial.printf("Price for charging: %s\n", messageTemp);
        account.transfer(messageTemp.toInt(), "charger"); // Overfører penger til laderen
        batteryChargeCycles++; // Øker antall ladninger
        batteryCapacity -= 0.1; // Reduserer kapasiteten til batteriet som følge av oppladning
        preferences.putDouble("batCap", batteryCapacity); // Lagrer kapasiteten til batteriet i flash
        preferences.putUInt("batCycles", batteryChargeCycles); // Lagrer antall ladninger i flash
        state = DRIVE;
    }
    if ((String(topic) == "zumo/control/direction") && (state == CONTROL)) {
        batteryPercentage -= powerConstant * 200 * 0.2; // Zumo kjører med motorspeed 200 i 0.2 sekunder
        if (batteryPercentage <= 0.0) {
            batteryPercentage = 0.0;
        }
        preferences.putDouble("batPrcnt", batteryPercentage); // Lagrer prosenten til batteriet i flash
        client.publish("zumo/battery/currentLevel", String(batteryPercentage, 2).c_str()); // Sender nåværende batterinivå til serveren
        Serial2.println(messageTemp); // Sender kommando til Zumo
    }
    if (String(topic) == "ir/break" && (state == CHARGE || state == GARAGE)) { // Hvis Zumo er i lademodus og 
        Serial2.println("break-beam");                                         // break-beam-sensoren registrerer noe
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
          client.subscribe("zumo/battery/swapped");
          client.subscribe("zumo/state");
          client.subscribe("zumo/control/direction");
          client.subscribe("charger/start");
          client.subscribe("charger/stop");
          client.subscribe("ir/break");
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
        //Serial.printf("Received message Serial2: %s\n", input);

        // Omgjør meldingen til JSON
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
            lastSpeedTime = millis(); // Oppdaterer tiden for når batterinivået ble oppdatert
            motorSpeed = doc["speed"];
            //Serial.println("Motor speed: ");
            //Serial.println(motorSpeed);
            client.publish(topic, String(motorSpeed).c_str());
            // Maksfart: 400 (0,78 m/s)
            // Deler på 50000 for å få fart i cm/s
            // Dette foregår på node-red
        }
    }
}

void calculateBatteryLevel()
{
    unsigned long now = millis();
    if (now - lastSpeedTime  > 2000) {
        motorSpeed = 0;
        client.publish("zumo/speed", String(motorSpeed).c_str());
    }
    else if (state == DRIVE || state == REVERSE || state == CHARGE || state == GARAGE) {
        // Oppdaterer batterinivået hvert sekund
        if (now - lastBatteryUpdate > 1000 && motorSpeed > 0)
        {   
            lastBatteryUpdate = now;
            float batteryLevel = batteryPercentage / 100 * batteryCapacity; // Regner ut batterinivået i mAh
            batteryLevel -= motorSpeed * powerConstant ; // P=mv
            batteryPercentage = batteryLevel / batteryCapacity * 100; // Regner ut prosent

            preferences.putDouble("batPrcnt", batteryPercentage); // Lagrer batterinivået i flash
        }

        if (batteryCapacity < 0)
        {
            batteryCapacity = 0.0;
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
            batteryCapacity -= 0.5; // 0,5kWh av batterikapasiteten er tapt på grunn av lavt batterinivå
            preferences.putDouble("batCap", batteryCapacity);
            // Si fra til Zumo at batterinivået er kritisk lavt
        }
        else if (batteryPercentage <= 20 && lastPercentage > 20) {
            lastPercentage = batteryPercentage;
            // Finn ladestasjon
            state = CHARGE;
        }
        else if (batteryPercentage <= 40 && lastPercentage > 40) // Kjører bare en gang
        {
            lastPercentage = batteryPercentage;
            // Si fra til Zumo at batterinivået er lavt
        }
        else if (batteryPercentage > 100)
        {
            batteryPercentage = 100;
        }
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
    if (lastState != state)
    {
        lastState = state;
        switch (state)
        {
            case DRIVE:
                Serial2.println("drive");
                break;
            case CHARGE:
                Serial2.println("charge");
                break;
            case GARAGE:
                Serial2.println("garage");
                break;
            case REVERSE:
                Serial2.println("reverse");
                break;
            case CONTROL:
                Serial2.println("control");
                break;
            case STOP:
                Serial2.println("stop");
                break;
            case CALIBRATE:
                Serial2.println("calibrate");
                break;
        }
    }
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(9600); // Start seriell kommunikasjon med Zumo
    preferences.begin("my-app", false); // Start preferences for å lagre variabler
    batteryPercentage = preferences.getDouble("batPrcnt", batteryPercentage); // Leser batterinivå fra flash
    batteryCapacity = preferences.getDouble("batCap", batteryCapacity); // Leser batterikapasitet fra flash
    batteryChargeCycles = preferences.getUInt("batCycles", batteryChargeCycles); // Leser antall ladninger fra flash


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
    controlZumo();

    if (!client.connected())
    {
        reconnect();
    }
    client.loop();

    unsigned long now = millis();
    if (now - lastMsg > 1000)
    {
        // Sender over MQTT
        client.publish("zumo/battery/currentLevel", String(batteryPercentage, 2).c_str());
        client.publish("zumo/battery/capacity", String(batteryCapacity, 2).c_str());

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
