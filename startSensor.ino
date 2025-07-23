#include <ESP8266WiFi.h>          
#include <ESP8266Firebase.h>      
#include <NTPClient.h>            
#include <WiFiUdp.h>              

// === WiFi & Firebase ===
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define REFERENCE_URL ""

const int START_SENSOR_PIN = D7;   

// === Firebase & Time ===
Firebase firebase(REFERENCE_URL);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// === Race State ===
String currentRaceId = "";
String status = "";
unsigned long startTimes[20];  
int targetLaps = 0;
int currentLap = 0;
volatile bool irDetected = false;   

// === Timing ===
unsigned long lastTimeUpdate = 0;

void IRAM_ATTR handleIR() {
  irDetected = true;  
}

void setup() {
  Serial.begin(115200);

  pinMode(START_SENSOR_PIN, INPUT);  

  // Attach interrupt to IR pin — detect FALLING edge (beam break)
  attachInterrupt(digitalPinToInterrupt(START_SENSOR_PIN), handleIR, FALLING);

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

  Serial.println("System ready — waiting for race.");
}

void loop() {
  // Update NTP time every 60 sec
  if (millis() - lastTimeUpdate > 60000) {
    timeClient.update();
    lastTimeUpdate = millis();
  }

  // === Check for new race ===
  String newRaceId = firebase.getString("liveRace/raceId");
  if (newRaceId != currentRaceId) {
    currentRaceId = newRaceId;
    Serial.println("New race detected: " + currentRaceId);
  }

  // === Check race status ===
  String newStatus = firebase.getString("liveRace/status");
  newStatus.trim();
  newStatus.replace("\"", "");

  if (newStatus != status) {
    status = newStatus;
    Serial.println("Status changed to: " + status);

    if (status == "started") {
      currentLap = 1;
      targetLaps = firebase.getInt("liveRace/targetLaps");
      if (targetLaps > 20) targetLaps = 20;  
      Serial.println("Race started! Laps: " + String(targetLaps));
    }
  }

  // === Handle IR detection ===
  if (status == "started" && irDetected && currentLap <= targetLaps) {
    irDetected = false;  

    timeClient.update(); 
    unsigned long startTime = timeClient.getEpochTime();

    startTimes[currentLap - 1] = startTime;  

    Serial.println("Lap " + String(currentLap) + " started at: " + String(startTime));

    currentLap++;

    if (currentLap > targetLaps) {
      Serial.println("All laps recorded. Uploading...");
      uploadStartTimes();
    }
  }
}

// === Upload all start times to Firebase ===
void uploadStartTimes() {
  for (int i = 0; i < targetLaps; i++) {
    String path = "liveRace/laps/lap" + String(i + 1) + "/startTime";
    bool ok = firebase.setInt(path, startTimes[i]);
    Serial.println(ok ? "Lap " + String(i + 1) + " uploaded." : "Failed upload Lap " + String(i + 1));
  }
  Serial.println("All start times uploaded!");
}
