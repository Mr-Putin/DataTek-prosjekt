
// Library’s 
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi settings
const char* ssid = "Iphone (4)";
const char* password = "12345678";

// MQTT broker settings
const char* mqttServer = "84.52.229.122";
const int mqttPort = 1883;

// MQTT Topic
const char* ir_state_topic = "ir/sensor";
const char* lap_time_topic = "lap/time";
const char* light_state_topic = "light/state";
const char* light_set_topic = "light/set";
const char* temp_state_topic = "temp/state";
const char* light_value_topic = "light/value";

// Pin
const uint16_t kRecvPin = 12;
const int light_diode = 32;
const int light_sensor = 33;
const int temp_sensor = 34;

// Constants
#define REFERENCE_RESISTANCE 10000
#define NOMINAL_TEMPERATURE 25
#define NOMINAL_RESISTANCE 10000
#define B_COEFFICIENT 3950

unsigned long lapStartTime = 0;
unsigned long lapTime;
unsigned long lastIRTime = 0;
unsigned long lastTempPublishTime = 0;
unsigned long lastLightPublishTime = 0;

bool override_on = false;
int counter_1 = 0;
int counter_2 = 0;

// Clients
WiFiClient espClientSensor;
PubSubClient mqttClient(espClientSensor);

IRrecv irrecv(kRecvPin);
decode_results results;

// Connect to WiFi network
void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
}

// Connect to MQTT
void setup_mqtt() {
  mqttClient.setServer(mqttServer, mqttPort);
  while (!mqttClient.connected()) {
    Serial.println("Connecting to MQTT broker...");

    if (mqttClient.connect("ESP32Client")) {
      Serial.println("Connected to MQTT broker");
      mqttClient.subscribe(light_set_topic);
    } else {
      Serial.print("Failed with state ");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  setup_mqtt();

  mqttClient.setCallback(callback);

  // Pin state
  pinMode(light_diode, OUTPUT);
  pinMode(light_sensor, INPUT);
  
  // Start receiver
  irrecv.enableIRIn();
  // Wait for the serial connection to be established.  
  while (!Serial)
    delay(50);
  Serial.println();
  Serial.println("IRrecviver is now running");
}

// Temperature function 
void temp(){
  if (millis() - lastTempPublishTime >= 1000) {
    // Reset timer
    lastTempPublishTime = millis();
    // Read thermistor sensor
    int temp = analogRead(temp_sensor);
    // Convert thermistor value to celsius with Steinhart-Hart equation
    float voltage = temp * (3.3 / 4096.0);
    float resistance = REFERENCE_RESISTANCE * voltage / (3.3 - voltage);
    float temperature = 1.0 / (1.0 / (NOMINAL_TEMPERATURE + 273.15) + log(resistance / NOMINAL_RESISTANCE) / B_COEFFICIENT) - 273.15;
    // Publish to mqtt
    mqttClient.publish(temp_state_topic, String(temperature).c_str());    
  }
}

// Light function
void light(){
  // Read light sensor
  int light_value = analogRead(light_sensor);
  // Publish light value to mqtt every 1000 ms
  if (millis() - lastLightPublishTime >= 1000){
    lastLightPublishTime = millis();
    mqttClient.publish(light_value_topic, String(light_value).c_str());
  }
  // If the light value is below the threshold and override is not on, turn on the light
  if (light_value <= 2300 && !override_on) {
      digitalWrite(light_diode, HIGH);
      // Publish state change to MQTT
      mqttClient.publish(light_state_topic, "ON");
  }
  // If the light value is over the threshold and override is not on, turn off the light
  if (light_value > 2300 && !override_on){
    digitalWrite(light_diode, LOW);
    // Publish state change to MQTT
    mqttClient.publish(light_state_topic, "OFF"); 
  }
}

// IR function
void ir(){
  if (irrecv.decode(&results)) {
    irrecv.resume();  // Receive the next value
    lastIRTime = millis(); // update lastIRTime when IR signal is received
    counter_1 = 0;
    if (counter_2 == 0){
      mqttClient.publish(ir_state_topic, "0");
      counter_2 = 1;          
    }
  } 
  // Check if signal is broken
  else if((millis() - lastIRTime > 110) && (counter_1 == 0)) {  
    counter_1 = 1;
    counter_2 = 0;        
    Serial.println("No IR signal detected");
    mqttClient.publish(ir_state_topic, "1");
    // Start timer for lap 1
    if (lapStartTime == 0) {
      lapStartTime = millis();
    }  
    // Check if debounce time is done
    else if ((millis() - lapStartTime) > 1500) { // Check if debounce time has elapsed
      // Record lap time and start timer for next lap
      lapTime = millis() - lapStartTime;
      lapStartTime = millis();
      Serial.println(lapTime);
      // Publish laptime
      mqttClient.publish(lap_time_topic, String(lapTime).c_str());  
    }  
  }
}

void loop() {
  // If not connected to MQTT, attempt to reconnect
  if (!mqttClient.connected()) {
    setup_mqtt();
  }
  
  mqttClient.loop();

  // Calls functions 
  temp();
  light();
  ir();
  
  delay(10);
}

// Receive messages from subscribed 
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
    // When you receive the right message start light override
    if (message == "on") {
      override_on = true;
      // Turn on the light
      digitalWrite(light_diode, HIGH);
      // Publish state change to MQTT
      mqttClient.publish(light_state_topic, "ON");
    }else{
      override_on = false;
  }
}
