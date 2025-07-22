#include <ESP8266WiFi.h>
#include <ESP8266Firebase.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define WIFI_SSID "Harindu"
#define WIFI_PASSWORD "123456789"
#define REFERENCE_URL "https://smart-sprint-tracker-default-rtdb.firebaseio.com/"

const int START_SENSOR_PIN = D7;  // IR sensor pin

Firebase firebase(REFERENCE_URL);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String currentRaceId = "";
String status = "";
unsigned long startTime = 0;
int targetLaps = 0;
int currentLap = 0;
bool raceStarted = false;
bool sensorTriggered = false;

void setup() {
  Serial.begin(115200);
  
  // Initialize sensor pin - try both configurations
  pinMode(START_SENSOR_PIN, INPUT);  // Start with regular INPUT
  // pinMode(START_SENSOR_PIN, INPUT_PULLUP); // Alternative if above doesn't work
  
  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  
  timeClient.begin();
  timeClient.setTimeOffset(19800);
  firebase.json(true);
  
  // Extended sensor test
  Serial.println("\nSensor Test - Wave your hand in front of the sensor");
  Serial.println("Should change from 1 to 0 when object detected");
  for(int i=0; i<10; i++) {
    Serial.println("Sensor state: " + String(digitalRead(START_SENSOR_PIN)));
    delay(500);
  }
}

void loop() {
  timeClient.update();
  
  // Check for new race
  String newRaceId = firebase.getString("liveRace/raceId");
  if (newRaceId != currentRaceId) {
    currentRaceId = newRaceId;
    Serial.println("New race detected: " + currentRaceId);
  }

  // Monitor status changes
  String newStatus = firebase.getString("liveRace/status");
  newStatus.trim();
  newStatus.replace("\"", "");
  if (newStatus != status) {
    status = newStatus;
    Serial.println("Status changed to: " + status);
    
    if (status == "started") {
      raceStarted = true;
      currentLap = 1;
      targetLaps = firebase.getInt("liveRace/targetLaps");
      Serial.println("Race started! Laps: " + String(targetLaps));
    } else if (status == "created") {
      raceStarted = false;
    }
  }
  
  // During active race
  if (raceStarted && currentLap <= targetLaps) {
    int sensorState = digitalRead(START_SENSOR_PIN);
    static unsigned long lastPrint = 0;
    
    // Print sensor state every second for debugging
    if (millis() - lastPrint > 1000) {
      Serial.println("Sensor state: " + String(sensorState));
      lastPrint = millis();
    }
    
    // Try BOTH detection methods (comment out one)
    if (sensorState == LOW && !sensorTriggered) {  // For PULLUP configuration
    // if (sensorState == HIGH && !sensorTriggered) { // For regular INPUT
      sensorTriggered = true;
      startTime = timeClient.getEpochTime();
      
      String path = "liveRace/laps/lap" + String(currentLap) + "/startTime";
      if (firebase.setInt(path, startTime)) {
        Serial.println("Lap " + String(currentLap) + " started at: " + String(startTime));
      } else {
        Serial.println("Failed to write to Firebase!");
      }
      
      currentLap++;
      if (currentLap > targetLaps) {
        raceStarted = false;
        Serial.println("All start times recorded!");
      }
    } else if (sensorState == HIGH) {  // Change to LOW if using regular INPUT
      sensorTriggered = false;
    }
  }
  delay(50); // Reduced delay for more responsive detection
}
