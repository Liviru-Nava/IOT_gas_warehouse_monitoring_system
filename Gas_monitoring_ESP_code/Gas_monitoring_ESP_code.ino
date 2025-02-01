#include <DHT.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Wi-Fi and Firebase configuration
#define WIFI_SSID "" // Your WiFi SSID
#define WIFI_PASSWORD "" // Your WiFi Password
#define API_KEY "" // Firebase API Key
#define DATABASE_URL "" // Firebase Database URL

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
#define GREEN_LED_PIN 16
#define RED_LED_PIN 17
#define FAN_PIN 18
#define BUZZER_PIN 19
#define ORANGE_LED_PIN 4 // Orange LED for flame detection
#define BLUE_LED_PIN 22 // blue led for indicate everything okay

// Threshold values
#define TEMP_THRESHOLD 30.0   // Temperature threshold to turn on the fan
#define MQ2_THRESHOLD 1700     // MQ2 value to detect gas/smoke

float mapGasPercentage(int rawValue) {
  // Constrain the raw value between 900 and 4095
  int constrainedValue = constrain(rawValue, 900, 4095);
  
  // Map the value to percentage (0-100)
  float percentage = map(constrainedValue, 900, 4095, 0, 100);
  
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
  if (Firebase.ready() && signupOk && (millis() - sendDataPrevMillis > 2000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    // Read sensor data
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    mq2Value = analogRead(MQ2PIN);
    flameDetected = (digitalRead(FLAME_PIN) == LOW); // Flame detected if LOW

    

    // Control actuators based on sensor data

    bool fanState = false; // Initialize fan state to OFF

    if (!isnan(temperature) && temperature > TEMP_THRESHOLD) {
      digitalWrite(FAN_PIN, LOW); // Turn on the fan
      digitalWrite(BLUE_LED_PIN, HIGH); // Turn on the blue LED
      fanState = true; // Fan is ON
      Serial.println("Fan ON and Blue LED ON");

    } else {
      digitalWrite(FAN_PIN, HIGH); // Turn off the fan
      digitalWrite(BLUE_LED_PIN, LOW); // Turn off the blue LED
      fanState = false; // Fan is OFF
      Serial.println("Fan OFF and Blue LED OFF");

    }

    if (mq2Value > MQ2_THRESHOLD) {
      digitalWrite(RED_LED_PIN, HIGH);  // Turn on the red LED for gas detection
      digitalWrite(GREEN_LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, HIGH);  // Turn on the buzzer
      Serial.println("Gas detected!");
    } else {
      digitalWrite(RED_LED_PIN, LOW);   // Turn off the red LED
      digitalWrite(GREEN_LED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, LOW);   // Turn off the buzzer
    }

    if (flameDetected) {
      digitalWrite(ORANGE_LED_PIN, HIGH); // Turn on the orange LED for flame detection
      Serial.println("Flame detected!");
    } else {
      digitalWrite(ORANGE_LED_PIN, LOW);  // Turn off the orange LED
      Serial.println("No flame detected");
    }


    float gasPercentage = mapGasPercentage(mq2Value);  // Convert raw value to percentage

    // Send data to Firebase
    if (Firebase.RTDB.setFloat(&fbdo, "sensor_data/temperature", temperature) &&
        Firebase.RTDB.setFloat(&fbdo, "sensor_data/humidity", humidity) &&
        // Firebase.RTDB.setInt(&fbdo, "Sensor/mq2Value", mq2Value) &&
        Firebase.RTDB.setFloat(&fbdo, "sensor_data/gas_level_percent", gasPercentage) &&
        Firebase.RTDB.setBool(&fbdo, "sensor_data/flame_detected", flameDetected) &&
        Firebase.RTDB.setBool(&fbdo, "sensor_data/fans_status", fanState)) { // Add fan state
      
      Serial.println("Data sent successfully:");
      Serial.printf("Temperature: %.2f°C, Humidity: %.2f%%, MQ2: %d, Flame Detected: %s\n", 
                    temperature, humidity, mq2Value, flameDetected ? "true" : "false" ,fanState ? "true" : "false");
    } else {
      Serial.println("Failed to send data: " + fbdo.errorReason());
    }
  }
}