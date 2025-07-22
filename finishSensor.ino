#include <ESP8266WiFi.h>
#include <ESP8266Firebase.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define WIFI_SSID "Harindu"
#define WIFI_PASSWORD "123456789"
#define REFERENCE_URL "https://smart-sprint-tracker-default-rtdb.firebaseio.com/"

// Hardware pins
const int FINISH_SENSOR_PIN = D7;
const int ENCODER_CLK = D5;
const int ENCODER_DT = D6;
const int ENCODER_SW = D3;

// Encoder variables
int encoderPos = 0;
int lastEncoderA = LOW;
bool encoderMoved = false;
float currentDistance = 0;

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Firebase and NTP
Firebase firebase(REFERENCE_URL);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Race variables
String currentRaceId = "";
String status = "";
int targetLaps = 0;
int currentLap = 1;
bool raceStarted = false;
bool sensorTriggered = false;
unsigned long lastEncoderChange = 0;
unsigned long lastStatusCheck = 0;
unsigned long lastSensorTriggerTime = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize encoder pins
  pinMode(ENCODER_CLK, INPUT);
  pinMode(ENCODER_DT, INPUT);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(FINISH_SENSOR_PIN, INPUT);
  
  // Initialize LCD
  Wire.begin(D2, D1);
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.print("Initializing...");
  
  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    lcd.setCursor(0, 1);
    lcd.print("Connecting WiFi...");
    Serial.println("Connecting to WiFi...");
    delay(500);
  }
  
  // Initialize time and Firebase
  timeClient.begin();
  timeClient.setTimeOffset(19800);
  firebase.json(true);
  
  lcd.clear();
  lcd.print("System Ready");
  Serial.println("System Ready");
  delay(200);
}

void loop() {
  timeClient.update();
  
  // Check Firebase status every 500ms
  if (millis() - lastStatusCheck > 500) {
    checkStatus();
    lastStatusCheck = millis();
  }

  // Manual encoder reading
  readEncoder();

  // Race operations
  if (raceStarted) {
    handleRace();
  }
  
  delay(10);
}

void checkStatus() {
  // Check for new race
  String newRaceId = firebase.getString("liveRace/raceId");
  if (newRaceId != currentRaceId) {
    currentRaceId = newRaceId;
    lcd.clear();
    lcd.print("Race has been detected!");
    Serial.println("New race detected: " + currentRaceId);
    delay(200);
  }

  // Monitor status changes
  String newStatus = firebase.getString("liveRace/status");
  newStatus.trim();
  newStatus.replace("\"", "");
  if (newStatus != status) {
    status = newStatus;
    lcd.clear();
    Serial.println("Status changed to: " + status);
    
    if (status == "init") {
      lcd.print("Adjust Distance:");
      currentDistance = 0;
      encoderPos = 0;
      Serial.println("Entered INIT mode");
    } 
    else if (status == "created") {
      lcd.print("Ready to Start");
      currentLap = 1;
      Serial.println("System READY");
    }
    else if (status == "started") {
      raceStarted = true;
      targetLaps = firebase.getInt("liveRace/targetLaps");
      lcd.print("Race Started!");
      lcd.setCursor(0, 1);
      lcd.print("Lap " + String(currentLap) + "/" + String(targetLaps));
      Serial.println("Race STARTED with " + String(targetLaps) + " laps");
    }
    else if (status == "completed") {
      raceStarted = false;
      lcd.print("Race Completed!");
      Serial.println("Race COMPLETED");
    }
  }
}

void readEncoder() {
  int encoderA = digitalRead(ENCODER_CLK);
  if (encoderA != lastEncoderA && millis() - lastEncoderChange > 5) {
    if (digitalRead(ENCODER_DT) != encoderA) {
      encoderPos--;
      currentDistance -= 50.0/30;
    } else {
      encoderPos++;
      currentDistance += 50.0/30;
    }
    encoderMoved = true;
    lastEncoderChange = millis();
    Serial.println("Encoder moved: " + String(currentDistance) + "m");
  }
  lastEncoderA = encoderA;
  
  if (encoderMoved && status == "init") {
    lcd.setCursor(0, 1);
    lcd.print("Cur:" + String(currentDistance) + "m Tgt:" + 
              String(firebase.getInt("liveRace/targetDistance")) + "m ");
    encoderMoved = false;
  }
}

void handleRace() {
  if (currentLap > targetLaps) {
    raceStarted = false;
    return;
  }

  int sensorState = digitalRead(FINISH_SENSOR_PIN);
  Serial.println("IR Sensor State: " + String(sensorState));  // Debug

  if (sensorState == LOW && !sensorTriggered && millis() - lastSensorTriggerTime > 1000) {
    sensorTriggered = true;
    lastSensorTriggerTime = millis();
    unsigned long finishTime = timeClient.getEpochTime();

    // Save finish time
    String path = "liveRace/laps/lap" + String(currentLap) + "/finishTime";
    bool success = firebase.setInt(path, finishTime);
    if (success) {
      Serial.println("Saved finish time for lap " + String(currentLap));
    } else {
      Serial.println("Failed to save finish time for lap " + String(currentLap));
    }

    // Save lap time
    String startPath = "liveRace/laps/lap" + String(currentLap) + "/startTime";
    int startTime = firebase.getInt(startPath);
    if (startTime > 0) {
      unsigned long lapTime = finishTime - startTime;
      String timePath = "liveRace/laps/lap" + String(currentLap) + "/lapTime";
      firebase.setInt(timePath, lapTime);

      lcd.clear();
      lcd.print("Lap " + String(currentLap));
      lcd.setCursor(0, 1);
      lcd.print("Time: " + String(lapTime) + "s");
      Serial.println("Lap " + String(currentLap) + " time: " + String(lapTime) + "s");
    }

    currentLap++;
    if (currentLap > targetLaps) {
      Serial.println("All laps completed");
    }
  } 
  else if (sensorState == HIGH) {
    sensorTriggered = false;
  }
}
