#include <ESP8266WiFi.h>         
#include <ESP8266Firebase.h>     
#include <NTPClient.h>           
#include <WiFiUdp.h>             
#include <LiquidCrystal_I2C.h>   
#include <Wire.h>                

// === WiFi & Firebase ===
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define REFERENCE_URL ""

// === Hardware Pins ===
const int ENCODER_CLK = D5;          
const int ENCODER_DT  = D6;          
const int ENCODER_SW  = D3;          
const int FINISH_SENSOR_PIN = D7;    

// === Rotary Encoder Settings ===
const int stepsPerRevolution = 30;   
const float distancePerStep = 100.0 / stepsPerRevolution;  

volatile int lastClkState;           
volatile float currentDistance = 0.0; 


LiquidCrystal_I2C lcd(0x27, 16, 2);  

// === Firebase & Time ===
Firebase firebase(REFERENCE_URL);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// === Race State ===
String currentRaceId = "";
String status = "";
int targetLaps = 0;
float targetDistance = 0.0;
int currentLap = 1;
bool raceStarted = false;
bool sensorTriggered = false;

// === Time ===
unsigned long lastStatusCheck = 0;
unsigned long lastSensorTriggerTime = 0;
unsigned long lastTimeUpdate = 0;

// === Lap Finish ===
#define MAX_LAPS 20
unsigned long finishTimes[MAX_LAPS];
int finishTimeIndex = 0;

// === LCD Flicker ===
float lastDistanceDisplayed = -1.0;

// === Declare ISR prototype ===
void IRAM_ATTR updateEncoder();

void setup() {
  Serial.begin(115200);

  pinMode(ENCODER_CLK, INPUT);
  pinMode(ENCODER_DT, INPUT);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(FINISH_SENSOR_PIN, INPUT);

  Wire.begin(D2, D1);     
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.print("Initializing...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    lcd.setCursor(0, 1);
    lcd.print("Connecting WiFi");
    Serial.println("Connecting WiFi...");
    delay(500);
  }

  timeClient.begin();
  timeClient.setTimeOffset(19800); 

  firebase.json(true);

  lastClkState = digitalRead(ENCODER_CLK); 

  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), updateEncoder, CHANGE);

  lcd.clear();
  lcd.print("System Ready");
  Serial.println("System Ready");
  delay(500);
}

void loop() {
  if (millis() - lastTimeUpdate > 60000) {
    timeClient.update();
    lastTimeUpdate = millis();
  }

  if (millis() - lastStatusCheck > 500) {
    checkStatus();
    lastStatusCheck = millis();
  }

  // Reset with push button
  if (digitalRead(ENCODER_SW) == LOW) {
    currentDistance = 0;
    Serial.println("Distance reset!");
    delay(500);
  }

  // Only display if distance changed
  if (status == "init") {
    if (abs(currentDistance - lastDistanceDisplayed) > 0.01) {
      displayDistanceAdjust();
      lastDistanceDisplayed = currentDistance;
    }
  }

  if (raceStarted) {
    handleRace();
  }
}

// === ISR for Rotary Encoder ===
void IRAM_ATTR updateEncoder() {
  int newClkState = digitalRead(ENCODER_CLK);
  if (newClkState != lastClkState) {
    if (digitalRead(ENCODER_DT) != newClkState) {
      currentDistance += distancePerStep; 
    } else {
      currentDistance -= distancePerStep; 
      if (currentDistance < 0) currentDistance = 0;
    }
    lastClkState = newClkState;
  }
}

// === Check Firebase for status ===
void checkStatus() {
  String newRaceId = firebase.getString("liveRace/raceId");
  if (newRaceId != currentRaceId) {
    currentRaceId = newRaceId;
    lcd.clear();
    lcd.print("Race detected!");
    Serial.println("New race: " + currentRaceId);
    delay(200);
  }

  String newStatus = firebase.getString("liveRace/status");
  newStatus.trim();
  newStatus.replace("\"", "");

  if (newStatus != status) {
    status = newStatus;
    lcd.clear();
    Serial.println("Status: " + status);

    if (status == "init") {
      targetDistance = firebase.getInt("liveRace/targetDistance");
      currentDistance = 0;
      Serial.println("Target: " + String(targetDistance) + "m");
      displayDistanceAdjust();

    } else if (status == "created") {
      firebase.setFloat("liveRace/adjustedDistance", currentDistance);
      lcd.print("Ready to Start");
      currentLap = 1;
      finishTimeIndex = 0;
      Serial.println("Distance saved: " + String(currentDistance));
      Serial.println("System READY");

    } else if (status == "started") {
      raceStarted = true;
      targetLaps = firebase.getInt("liveRace/targetLaps");
      if (targetLaps > MAX_LAPS) {
        targetLaps = MAX_LAPS;
      }
      lcd.print("Race Started!");
      lcd.setCursor(0, 1);
      lcd.print("Lap " + String(currentLap) + "/" + String(targetLaps));
      Serial.println("Race STARTED, Laps: " + String(targetLaps));

    } else if (status == "completed") {
      raceStarted = false;
      lcd.print("Race Completed!");
      Serial.println("Race COMPLETED");
    }
  }
}

// === LCD display ===
void displayDistanceAdjust() {
  lcd.setCursor(0, 0);
  lcd.print("Distance:      ");
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print(String(currentDistance, 2) + "m/" + String(targetDistance, 1) + "m");
}

// === Handle finish line IR ===
void handleRace() {
  if (currentLap > targetLaps) {
    raceStarted = false;
    uploadFinishTimes();
    return;
  }

  int sensorState = digitalRead(FINISH_SENSOR_PIN);
  if (sensorState == LOW && !sensorTriggered && millis() - lastSensorTriggerTime > 1000) {
    sensorTriggered = true;
    lastSensorTriggerTime = millis();

    unsigned long finishTime = timeClient.getEpochTime();

    if (finishTimeIndex < MAX_LAPS) {
      finishTimes[finishTimeIndex] = finishTime;
      finishTimeIndex++;
    }

    lcd.clear();
    lcd.print("Lap " + String(currentLap));
    lcd.setCursor(0, 1);
    lcd.print("Finish Saved");

    Serial.println("Lap " + String(currentLap) + ": " + String(finishTime));
    currentLap++;

  } else if (sensorState == HIGH) {
    sensorTriggered = false;
  }
}

// === Upload all finish times ===
void uploadFinishTimes() {
  Serial.println("Uploading all finish times...");
  for (int i = 0; i < finishTimeIndex; i++) {
    String path = "liveRace/laps/lap" + String(i + 1);
    unsigned long finishTime = finishTimes[i];
    firebase.setInt(path + "/finishTime", finishTime);

    int startTime = firebase.getInt(path + "/startTime");

    if (startTime > 0) {
      int lapTime = finishTime - startTime;
      firebase.setInt(path + "/lapTime", lapTime);
      Serial.println("Lap " + String(i + 1) + ": lapTime = " + String(lapTime));
    } else {
      Serial.println("Lap " + String(i + 1) + ": No startTime found");
    }

    Serial.println("Lap " + String(i + 1) + " uploaded");
  }
  lcd.clear();
  lcd.print("Times Uploaded!");
  Serial.println("All times uploaded!");
}
