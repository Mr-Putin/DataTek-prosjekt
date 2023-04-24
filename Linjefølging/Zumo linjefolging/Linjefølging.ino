#include <Wire.h>
#include <Zumo32U4.h>

const int maxSpeed = 400;

int lastError;
int rightSpeed;
int leftSpeed;

String chargeMessage = "";

bool chargeTurn = false; //Bool som brukes til å sjekke om bilen er på banen eller om den kjører til ladestasjon
bool emergencyCharge = false;
bool overrideFollower = false;

//De forskjellige pause-intervallene som brukes:
unsigned long timeNow;
unsigned long lastMessageTime;
unsigned long sekund = 1000;
unsigned long chargeTurnBreak = 100; //Denne må fin-justeres basert på banen.

enum State{
  drive,
  charge,
  trash,
  reverse
};

State state = drive;
//definerer de nødvendige sensorene/moduelene på zumo'en for videre bruk.
Zumo32U4Motors motors;
Zumo32U4ButtonC buttonC;
Zumo32U4OLED display;
Zumo32U4LineSensors lineSensors;

int prevError = 0;

#define NUM_SENSORS 5
unsigned int lineSensorValues[NUM_SENSORS];

void loadCustomCharacters(){
  static const char levels[] = {
    0, 0, 0, 0, 0, 0, 0, 63, 63, 63, 63, 63, 63, 63
  };
  display.loadCustomCharacter(levels + 0, 0); 
  display.loadCustomCharacter(levels + 1, 1);
  display.loadCustomCharacter(levels + 2, 2);
  display.loadCustomCharacter(levels + 3, 3);
  display.loadCustomCharacter(levels + 4, 4);
  display.loadCustomCharacter(levels + 5, 5);
  display.loadCustomCharacter(levels + 6, 6);
}

void printBar(uint8_t height){
  if(height > 8){
    height = 8;
  } 
  const char barChars[] = {' ', 0, 1, 2, 3, 4, 5 , 6, (char)255};
  display.print(barChars[height]); //dersom height er 4, printes barChars[4], som er 3.
}

void calibrateSensors(){
    timeNow = millis();
    while(millis() < timeNow + sekund){}

    for(int i; i < 125; i++){ 
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
    }
    motors.setSpeeds(0, 0); //Når kalibreringen er ferdig, stanser bilen tilbake der den startet.
}

void showReadings(){
  display.clear();
  while(!buttonC.getSingleDebouncedPress()){
    lineSensors.readCalibrated(lineSensorValues);
    display.gotoXY(0,0);
    for (int i = 0; i < NUM_SENSORS; i++)
    {
      int barHeight = map(lineSensorValues[i], 0, 1000, 0, 8);
      printBar(barHeight);
    }
  }
}

void lineFollower(){
  int position = lineSensors.readLine(lineSensorValues);
  int error = position - 2000; 

  //Default funksjonen for å finne forskjellen, må finjusters for 
  //vår zumo
  int speedDifference = error / 4 + 6 * (error - lastError); 
  lastError = error;

  leftSpeed = maxSpeed + speedDifference;
  rightSpeed = maxSpeed - speedDifference;
  leftSpeed = constrain(leftSpeed, 0, maxSpeed);
  rightSpeed = constrain(rightSpeed, 0, maxSpeed);

  motors.setSpeeds(leftSpeed, rightSpeed);
}

void driveToCharging(){
  //Funksjonen skal kjøres når bilen skal kjøres til ladestasjonen
  //Ser for meg at denne skal kjøres når bilen er satt i "lademodus"
  //og kjører over en bit i teipen som signaliserer at den skal ta av.
  //Denne funkjsonen blir ganske hardkodet. Skal bare være en rask venstre/høyresving
  //før den går tilbake til å følge en ny linje.
  if(!chargeTurn){
    motors.setSpeeds(0, 0); //Stopper en kort stund
    timeNow = millis();
    while(millis() < timeNow + 500){}
  
    motors.setSpeeds(200, -200); //Svinger vekk fra banen og mot ladestasjonen
    timeNow = millis();
    while(millis() < timeNow + chargeTurnBreak){}
   //kjører et lite stykke vekk fra hovedbanen
   //og inn på en ny teipbit som leder til ladestasjonen.
    motors.setSpeeds(200, 200); 
    chargeTurn = true;
  }
  lineFollower();

}

void setup(){
 //To første tingene som må gjøres er å sette opp sensorene og laste karakterene
 //som lager en bar. 
 lineSensors.initFiveSensors();
 loadCustomCharacters();

 Serial.begin(9600);
 Serial1.begin(9600);

 //Setter bilen i en state der den venter på at en knapp skal trykkes før noe skjer
 //sånn at bilen ikke starter å kjøre før man faktisk er klar
 display.clear();
 display.print(F("Press C"));
 display.gotoXY(0,1);
 display.print(F("to calib")); 
 buttonC.waitForButton();

 calibrateSensors();
 showReadings();
 lastMessageTime = millis();
}

void loop(){
  //Kort kodesnutt som leser meldinger fra esp'en og bestemmer hva bilen skal gjøre.
  while(Serial1.available()){
    motors.setSpeeds(0, 0);
    String incomingString = Serial1.readStringUntil('\n');
    if(incomingString == "change state"){
      while(Serial1.available()){
        state = State(readStringUntil('\n');
      }
    }
  }

  switch (state){
    case drive:
      motors.flipLeftMotor(false);
      motors.flipRightMotor(false);
      lineFollower();
      break;
    case charge:
      driveToCharging();
      break;
    case trash:
      break;
    case reverse:
      motors.flipLeftMotor(true);
      motors.flipRightMotor(true);
      lineFollower();
      break;
  }

  //Sender en melding til esp-en montert på zumoen med info om hvor fort bilen kjører
  //Dette brukes til å beregne hvor mye strøm som trekkes.
  //Sender 1 gang hvert sekund, dersom bilen skal nødlade ved rygging skal en negativ verdi sendes.
  if(millis() >= lastMessageTime + sekund){
    if(emergencyCharge){
      if(leftSpeed < 0 && rightSpeed < 0){ //Dersom begge motorene går bakover skal verdien som sendes være negativ, for å lade batteriet
        chargeMessage = String((leftSpeed + rightSpeed)/2);
        Serial1.println(chargeMessage);
      }
    }
    else{
    chargeMessage = String((abs(leftSpeed) + abs(rightSpeed))/2);
    lastMessageTime = millis();
    Serial1.println(chargeMessage);
    }
  }
}
