#include <Wire.h>
#include <Zumo32U4.h>
#include <ArduinoJson.h>

const int maxSpeed = 250;
const int NUM_SENSORS = 5;

String lastState = "";
String zumoDirection = "";

int lastError; //tidligere forskjell mellom linjen og posisjonen til Zumo'en
int rightSpeed; // hastighet til høyre motor
int leftSpeed; // hastighet til venstre motor
int chargeMessage; // informasjon om strømforbruken

//Bools som brukes for å styre turen til garasjen
//Når bilen først får beskjed om å kjøre til garasjen er alle false
//Disse er rester fra garasje-tilstanden, som måtte skrapes
bool passedSensor = false;
bool garageTurn = false;
bool garageStop = false;

bool readySent = false;
bool chargeStop = false; //stopper bilen når den har kommet frem til ladestasjonen
bool deadEndRoad = true; 
bool calibrated = false;
bool emergencyCharge = false;


//De forskjellige pause-intervallene som brukes:
unsigned long timeNow;
unsigned long lastMessageTime;
unsigned long lastSensorTime;
unsigned long turnTime = 370; 
unsigned long deadEndTurn = 800;

enum State{
  Drive,
  Stop,
  Charge,
  Control,
  Calibrate
};
 State state = Drive;


//definerer de nødvendige sensorene/moduelene på zumo'en for videre bruk.
Zumo32U4Motors motors;
Zumo32U4ButtonC buttonC;
Zumo32U4LineSensors lineSensors;
//Zumo32U4OLED display;
Zumo32U4Buzzer buzzer;

unsigned int lineSensorValues[NUM_SENSORS];

//Funksjon som legger sensorverdiene inn i et JsonArray og sender denne over Serial1
//Funksjonen holder tiden opp mot seg selv og printer ganger i sekundet, uavhenging av hvor/når den blir kalt på.
void printSensorValues(){
  static unsigned long z = 0;
  if(millis()- z >= 500){
    StaticJsonDocument<200> doc;
    doc["topic"] = "zumo/sensorData";
    JsonArray verdier = doc.createNestedArray("sensorverdier");
    verdier.add(lineSensorValues[0]);
    verdier.add(lineSensorValues[1]);
    verdier.add(lineSensorValues[2]);
    verdier.add(lineSensorValues[3]);
    verdier.add(lineSensorValues[4]);
    String json;
    serializeJson(doc, json);
    Serial1.println(json);
    z = millis();
  }
}

//Funksjon som sender snittfarten til motorene, 
//slik at SW-batteriet kan beregne hvor mye strøm som brukes.
void batteryInfo(){
  StaticJsonDocument<200> doc;
  doc["topic"] = "zumo/speed";
  if(emergencyCharge){
    if(leftSpeed < 0 && rightSpeed < 0){ //Dersom begge motorene går bakover skal verdien som sendes være negativ, for å lade batteriet
      doc["speed"] = (leftSpeed + rightSpeed)/2;
    }
  }
  else{
    doc["speed"] = (abs(leftSpeed) + abs(rightSpeed))/2;
  }
  lastMessageTime = millis();
  String json;
  serializeJson(doc, json);
  Serial1.println(json);
}

//Funksjonen for å kalibrere sensorene.
//Sveiper zumo'en i sirkler over teipbanen
void calibrateSensors(){
    timeNow = millis();
    while(millis() < timeNow + 1000){}

    for(int i = 0; i < 123; i++){ 
      if(i > 30 && i <=90){ 
      //Starter kalibreringen med å sveipe til høyre i 30 intervaller
      //før den sveiper andre retningen i 60 og tilbake til midten med 35.
      //(Fant ut med testing at den stanser litt før midten på 30)
      //Hver gang for-loopen kjører blir sensorene kalibrert.
        motors.setSpeeds(-200, 200);
      }
      else{
        motors.setSpeeds(200, -200);
      }
      lineSensors.calibrate();
      lineSensors.readLine(lineSensorValues);
      //Skriver sensorverdiene og batteri-info til Serial1 2 ganger i sekundet,
      //slik at esp'en kan lese de av og printe på dashbordet.
      if(millis() >= lastSensorTime + 500){
        printSensorValues();
        batteryInfo();
        lastSensorTime = millis();
      }   
    }
    motors.setSpeeds(0, 0);//Når kalibreringen er ferdig, stanser bilen tilbake der den startet.
}

void deadEnd(){
  motors.setSpeeds(200, -200);
  timeNow = millis();
  while(millis() < timeNow + deadEndTurn){}
  motors.setSpeeds(0, 0);
}

//Funksjonene som må til for å få de perfekte høyre og venstresvingene
//kjører på en gitt fart for å få den til å bli mest mulig reproduserbar
void rightTurn(){
  motors.setSpeeds(200, 200);
  timeNow = millis();
  while(millis() < timeNow + 100){}
  motors.setSpeeds(200, -200);
  timeNow = millis();
  while(millis() < timeNow + turnTime){} 
  motors.setSpeeds(200, 200);
  timeNow = millis();
  while(millis() < timeNow + 150){}
}

void leftTurn(){
  motors.setSpeeds(200, 200);
  timeNow = millis();
  while(millis() < timeNow + 100){}
  motors.setSpeeds(-200, 200);
  timeNow = millis();
  while(millis() < timeNow + turnTime){} 
  motors.setSpeeds(200, 200);
  timeNow = millis();
  while(millis() < timeNow + 150){}
}


/*Funksjon som skjekker hva bilen skal gjøre 
 * når den kommer til en teipbit som dekker begge yttersensorene
 * Når deadEndRoad == true skal den kjøre rett frem, 
 * inn på blindveien og snu nå den kommer til enden
Når deadEndRoad == false skal den ta en høyresving*/
void checkIntersection(){
  if(deadEndRoad){
    motors.setSpeeds(150, 150);
    timeNow = millis();
    while(millis() < timeNow + 300){}
    lineSensors.readCalibrated(lineSensorValues);
    while(!lostTrack()){
      motors.setSpeeds(150, 150);
    }
    deadEnd();
    deadEndRoad = false;
  }
  else{
    rightTurn();
    deadEndRoad = true;
  }
}


//bool som undersøker om bilen har kontakt med banen eller ikke
//starter med å oppdatere sensorverdiene, slik at funksjonen har størst mulighet til å returnere korrekt verdi.
//dersom alle sensorene har verdier under 100, skal dette regnes som om at bilen har mistet banen
//og funksjonen returnerer true
bool lostTrack(){
  lineSensors.readCalibrated(lineSensorValues);
  printSensorValues();
  if(lineSensorValues[0] <= 100 && lineSensorValues[4] <= 100){
    if(lineSensorValues[1] <= 100 && lineSensorValues[3] <= 100){
      if(lineSensorValues[2] <= 100){
        return true;
      }
    }
  }
  return false;
}

//Funksjonen som sørger for at bilen klarer å følge linjen
void lineFollower(){
  //Finner bilens posisjon i forhold til linjen.
  int position = lineSensors.readLine(lineSensorValues);
  int error = position - 2000;

  
  //Default funksjonen for å finne forskjellen, må finjusters for 
  //vår zumo
  int speedDifference = error / 4 + 6 * (error - lastError); 
  lastError = error;

  if(lineSensorValues[0] == 1000 && lineSensorValues[4] == 1000){
    if(!passedSensor){//liten ekstra bool som bare gjelder dersom den skal til garasjen
      checkIntersection();
    }
  }

  //Hvis zumoen  mister linjen skal den fortsette å kjøre rett frem til den finner banen igjen
  while(lostTrack()){
    motors.setSpeeds(150, 150);
  }


  

  //Setter farten til venstre- og høyremotoren basert på maxfart og fartsforskjellen som må til for linjefølgingen
  //Deretter blir ene motoren satt til maxfart og den andre til maxfart -/+ fartsforskjellen 
  //ved å bruke constrain(). 
  //(+ dersom fartsforskjellen er negativ)
  leftSpeed = maxSpeed + speedDifference;
  rightSpeed = maxSpeed - speedDifference;
  leftSpeed = constrain(leftSpeed, 0, maxSpeed);
  rightSpeed = constrain(rightSpeed, 0, maxSpeed);

  motors.setSpeeds(leftSpeed, rightSpeed);
}

void driveToCharging(){
  while(!chargeStop){
    lineFollower();
    if(Serial1.available()){
      String incomingString = Serial1.readStringUntil('\n');
      incomingString.trim();
      Serial.println(incomingString);
      if(incomingString == "break-beam"){
        chargeStop = true;
        leftSpeed = 0;
        rightSpeed = 0;
        motors.setSpeeds(leftSpeed, rightSpeed);
        Serial1.println("ready");
        batteryInfo();
      }
    }
  }
}

//Funksjon som får bilen ut fra garasjen og inn på banen igjen.
//Bilen skal kjøre rett frem til begge yttersensorene dekkes, hvor den tar en høyresving.
void driveOutFromGarage(){
  passedSensor = false;
  while(lineSensorValues[0] <= 200 && lineSensorValues[4] <= 200){
    lineSensors.readCalibrated(lineSensorValues);
    motors.setSpeeds(150, 150);
  }
  rightTurn();
}

//Bilen settes i en state der den følger linjen som vanlig frem til den kommer til break-beam-sensoren
//Dersom den skal til garasjen skal den ta av banen til høyre og følge en ny linje
//som den følger til begge yttersensorene dekkes, noe som sier ifra at den har kommet frem til garasjen
//Ved garasjen skal den ta en 180 og si ifra at den er klar til lading. 
//Denne ideen måtte skrapes, da uansett hva vi gjorde så stoppet den når den kom frem til ladestasjonen
//Har derfor fjernet denne fra tilstandsmaskinen, men beholdt koden.
void driveToGarage(){
  while(!garageTurn){
    lineFollower();
    while(Serial1.available()){
      String incomingString = Serial1.readStringUntil('\n');
      Serial.println(incomingString);
      if(incomingString == "break-beam"){
        passedSensor = true;
        timeNow = millis(); 
        while(lineSensorValues[4] <= 300){
          lineFollower();
        }
        rightTurn();
        garageTurn = true;
      }
    }
  }
  
  while(!garageStop){
    lineFollower();
    lineSensors.readCalibrated(lineSensorValues);
    if(lineSensorValues[0] == 1000 && lineSensorValues[4] == 1000){
      motors.setSpeeds(0, 0);
      garageStop = true;
    }
  }

  //Dersom bilen er i garasjen skal den ta en 180 før den sier at den er klar til å lade
  if(!readySent){
    motors.setSpeeds(200, -200);
    timeNow = millis();
    while(millis() < timeNow + deadEndTurn){}
    motors.setSpeeds(0, 0);
    Serial1.println("ready");
    readySent = true;
  }
  //mens den står i garasjen skal den lete etter nye beksjeder 
  while(Serial1.available()){
    String incomingString = Serial1.readStringUntil('\n');
    convertState(incomingString);
    driveOutFromGarage();
  }
}

void convertState(String x){
  if(lastState != x){
    if(x == "drive"){
      state = Drive;
      deadEndRoad = true;
    }
    else if(x == "charge"){
      chargeStop = false;
      readySent = false;
      state = Charge;
    }
    else if(x == "control"){
      state = Control;
    }
    else if(x == "stop"){
      state = Stop;
    }
    else if(x == "calibrate"){
      state = Calibrate;
      calibrated = false;
    }
    lastState = x;
  }
}

//Funksjon som leser av meldinger fra Serial1 og kjører funksjonen convertState, 
//for å konvertere meldingen om til en state.
void readStateFromSerial1(){
  while(Serial1.available()){
    motors.setSpeeds(0, 0);
    String incomingString = Serial1.readStringUntil('\n');
    incomingString.trim();
    convertState(incomingString);
    break;
  }
}

//Funksjon som kjører når bilen skal kontrolleres over Node-RED
//Tar inn meldinger over Serial1 og bestemmer hvilken retning bilen skal kjøre
void controlZumoRemote(){
  while(Serial1.available()){
    zumoDirection = Serial1.readStringUntil('\n');
    zumoDirection.trim();
    timeNow = millis();
    Serial.println(zumoDirection);
  }
  if (zumoDirection == "forward"){
    motors.setSpeeds(200, 200);
    while(millis() < timeNow + 200);
    }
  else if (zumoDirection == "backward"){
    motors.setSpeeds(-200, -200);
    while(millis() < timeNow + 200);
    }
  else if (zumoDirection == "left"){
    motors.setSpeeds(-200, 200);
    while(millis() < timeNow + 100);
    }
  else if (zumoDirection == "right"){
    motors.setSpeeds(200, -200);
    while(millis() < timeNow + 100);
    }
  else{ 
    //Dersom meldingen over Serial1 ikke matcher noen retninger betyr dette at det er en ny state 
    //og convertState() funksjonen kjører istedet.
    convertState(zumoDirection);
  }
  batteryInfo();
  motors.setSpeeds(0,0); 
  zumoDirection = "";
}

void setup(){
 lineSensors.initFiveSensors();

 //Starter seriall overvåker 1, som kommuniserer med ESP-32 montert på Zumo
 Serial1.begin(9600);
 
 //venter på at knapp C skal bli trykket inn før den går videre
 while(!buttonC.getSingleDebouncedPress()){}
 calibrateSensors();
 while(!buttonC.getSingleDebouncedPress()){}
 lastMessageTime = millis();
 buzzer.play("L4 c#");
 while(buzzer.isPlaying());

}

void loop(){
  /*Så lenge staten ikke er Control, skal bilen lese av Serial1
  *for å sjekke om de thar kommet ny beskjeder om hva den skal gjøre
  *Dersom den er i Control, skal denne ignoreres
  *og nye beskjeder takles direkte i controlZumoRemote funksjonen*/
  if(state != Control){
    readStateFromSerial1();
  }

  //Switch-case som bestemmer hvilket funkjsonskall som skal komme
  switch (state){
     case Drive:
       motors.flipLeftMotor(false);
       motors.flipRightMotor(false);
       lineFollower();
       break;
     case Charge:
       driveToCharging();
       break;
     case Stop:
       leftSpeed = 0;
       rightSpeed = 0;
       motors.setSpeeds(leftSpeed, rightSpeed);
       break;
     case Control:
         controlZumoRemote();
       break;
     case Calibrate:
       if(!calibrated){
        calibrateSensors();
        //Setter calibrated til true, slik at den ikke blir låst inne i evig kalibrering.
        //Settes tilbake til false når det kommer ny beskjed om å kalibrere
        calibrated = true; 
       }
       break;
    }

  //Printer sensorverdiene til Serial1
  printSensorValues();
  
  //Sender en melding til esp-en montert på zumoen med info om hvor fort bilen kjører
  //Dette brukes til å beregne hvor mye strøm som trekkes.
  //Sender 1 gang hvert sekund, dersom bilen skal nødlade ved rygging skal en negativ verdi sendes.
  if(millis() >= lastMessageTime + 1000){
    batteryInfo();
  }
}
