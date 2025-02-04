#include <DHT.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Wi-Fi and Firebase configuration
#define WIFI_SSID "Dialog 4G" // Your WiFi SSID
#define WIFI_PASSWORD "9RYA9F1D9E5" // Your WiFi Password
#define API_KEY "AIzaSyC4wyM5AqrC_4GX0KPtfUObhmOx7g_ad8w" // Firebase API Key
#define DATABASE_URL "https://liviru-test-default-rtdb.asia-southeast1.firebasedatabase.app/" // Firebase Database URL

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
bool signupOk = false;
float temperature, humidity;
int mq2Value;
bool flameDetected; // Store flame detection as a boolean

// Sensor pins
#define DHTPIN 15
#define MQ2PIN 34
#define FLAME_PIN 5 // Flame sensor pin (digital pin)
DHT dht(DHTPIN, DHT11);

// Actuator pins
#define GREEN_LED_PIN 33
#define RED_LED_PIN 17
#define FAN_PIN 18
#define BUZZER_PIN 19
#define ORANGE_LED_PIN 4 // Orange LED for flame detection
#define BLUE_LED_PIN 22 // blue led for indicate everything okay

// Threshold values
#define TEMP_THRESHOLD 36.0   // Temperature threshold to turn on the fan
#define MQ2_THRESHOLD 1700     // MQ2 value to detect gas/smoke

float mapGasPercentage(int rawValue) {
  int constrainedValue = constrain(rawValue, 300, 2500);
  
  // Convert to float before mapping
  float percentage = (float)(constrainedValue - 300) / (2500 - 300) * 100.0;

  // Round to two decimal places
  percentage = round(percentage * 100.0) / 100.0;

  return percentage;
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  // Initialize WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
  
  // Firebase setup
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Sign up successful");
    signupOk = true;
  } else {
    Serial.printf("Sign up error: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback; // Required for ESP32 token handling
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Initialize actuator pins
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(ORANGE_LED_PIN, OUTPUT); // Initialize the orange LED pin
  pinMode(BLUE_LED_PIN , OUTPUT);

  // Initialize flame sensor pin
  pinMode(FLAME_PIN, INPUT);

  // Ensure actuators are off initially
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(ORANGE_LED_PIN, LOW); // Turn off orange LED
  digitalWrite(BLUE_LED_PIN, LOW);

}

void loop() {
  if (Firebase.ready() && signupOk && (millis() - sendDataPrevMillis > 1000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    // Read sensor data
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    mq2Value = analogRead(MQ2PIN);
    flameDetected = (digitalRead(FLAME_PIN) == LOW); // Flame detected if LOW

    Serial.printf("Temp: %.2f°C\n", temperature);
    Serial.printf("Humidity: %.2f%%\n", humidity);
    Serial.printf("Gas: %d\n", mq2Value);
    Serial.printf("Flame detected: %s\n", flameDetected ? "true" : "false");

    // ** Reset all actuators first **
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BLUE_LED_PIN, LOW);
    digitalWrite(ORANGE_LED_PIN, LOW);
    digitalWrite(FAN_PIN, HIGH); // Default to OFF

    bool fanState = false;

    // ** Priority Order: Flame > Gas > Temperature **
    
    if (flameDetected) {  
      // ** Flame detected **
      digitalWrite(ORANGE_LED_PIN, HIGH); // Orange LED ON
      digitalWrite(RED_LED_PIN, HIGH);    // Red LED ON
      digitalWrite(FAN_PIN, HIGH);        // Fan OFF
      digitalWrite(GREEN_LED_PIN, LOW);   // Green OFF
      digitalWrite(BLUE_LED_PIN, LOW);    // Blue OFF
      Serial.println("Flame detected! Orange LED ON, Fan OFF");
    } 
    else if (mq2Value > MQ2_THRESHOLD) {  
      // ** Gas detected **
      digitalWrite(FAN_PIN, LOW);         // Fan ON
      digitalWrite(BLUE_LED_PIN, HIGH);   // Blue LED ON
      digitalWrite(RED_LED_PIN, HIGH);    // Red LED ON
      digitalWrite(GREEN_LED_PIN, LOW);   // Green OFF
      fanState = true;
      Serial.println("Gas detected! Fan ON, Blue ON, Red ON");
    } 
    else if (!isnan(temperature) && temperature > TEMP_THRESHOLD) {  
      // ** Temperature too high **
      digitalWrite(FAN_PIN, LOW);         // Fan ON
      digitalWrite(BLUE_LED_PIN, HIGH);   // Blue LED ON
      digitalWrite(RED_LED_PIN, HIGH);    // Red LED ON
      digitalWrite(GREEN_LED_PIN, LOW);   // Green OFF
      fanState = true;
      Serial.println("High Temperature! Fan ON, Blue ON, Red ON");
    }
    else if((!isnan(temperature) && temperature > TEMP_THRESHOLD) && (mq2Value > MQ2_THRESHOLD) && (flameDetected)){
      digitalWrite(FAN_PIN, LOW);
      digitalWrite(RED_LED_PIN, HIGH);
      digitalWrite(ORANGE_LED_PIN, HIGH);
      digitalWrite(BLUE_LED_PIN, HIGH);
      digitalWrite(GREEN_LED_PIN, LOW);
      fanState = true;
      Serial.println("DANGER, WAREHOUSE IN DANGER, FAN ON, RED LED ON, ORANGE LED ON, BLUE LED ON!");
    } 
    else {  
      // ** Normal Condition (Everything is OK) **
      digitalWrite(GREEN_LED_PIN, HIGH);  // Green LED ON
      digitalWrite(BLUE_LED_PIN, LOW);    // Blue LED OFF
      digitalWrite(RED_LED_PIN, LOW);     // Red LED OFF
      digitalWrite(FAN_PIN, HIGH);        // Fan OFF
      Serial.println("Normal Condition. Green ON, Everything Else OFF");
    }

    float gasPercentage = mapGasPercentage(mq2Value);

    // ** Force update Firebase even if values are the same **
    Firebase.RTDB.setFloat(&fbdo, "sensor_data/temperature", temperature);
    Firebase.RTDB.setFloat(&fbdo, "sensor_data/humidity", humidity);
    Firebase.RTDB.setFloat(&fbdo, "sensor_data/gas_level_percent", gasPercentage);
    Firebase.RTDB.setBool(&fbdo, "sensor_data/flame_detected", flameDetected);
    Firebase.RTDB.setBool(&fbdo, "sensor_data/fans_status", fanState);

    Serial.println("Data sent to Firebase (forced update).");
  }
}