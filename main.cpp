#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "never.h"

// Hardware settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Global OLED and BME280 objects (hardware interfaces)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_BME280 bme;

// Status flags and timing
bool sensorReady = false;
bool oledReady = false;
unsigned long lastUpdate = 0;

// How often the sensor is read and the OLED is refreshed (in ms)
const long UPDATE_INTERVAL = 2000;

// Pin assignments
const int BUTTON_PIN = 4;
const int LED_BUTTON = 5;

// Temperature threshold for the warning LED and "Warm!" message (in °C)
const float TEMP_DREMPEL = 28.00;

// Display modes
const int MODE_SENSOR = 0; // Default mode: live sensor readings
const int MODE_INFO = 1;   // Triggered by a long press: shows IP + uptime
int currentMode = MODE_SENSOR;

// Flag for the "Button pressed!" one-shot message on the OLED
bool buttonPressed = false;

// Button state tracking (INPUT_PULLUP: HIGH = released, LOW = pressed)
bool buttonState = HIGH;
bool lastButtonState = HIGH;
int lastReading = HIGH;
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50;

// Long-press detection
unsigned long startTime = 0;
bool longPressDetected = false;
const unsigned long longPressTime = 2000; // Press duration that triggers info mode

// Auto-return from info mode
unsigned long infoModeStartTime = 0;
const unsigned long infoModeDuration = 5000;

// Cached sensor values so the OLED can be refreshed instantly on a mode change
// without having to re-read the sensor
float lastTemp = NAN;
float lastHum = NAN;

// Web server object listening on port 80 (standard HTTP port)
WebServer server(80);

// Generates the HTML webpage with sensor values (temperature & humidity)
String createWebPage(float temperature, float humidity) {

  // Highlight the temperature in red when it is above the threshold
  String tempRegel;
  if (temperature >= 28.0) {
    tempRegel = "Temperature: <span style='color:red'>" + String(temperature, 2) + "</span> &deg;C";
  } else {
    tempRegel = "Temperature: " + String(temperature, 2) + " &deg;C";
  }

  // Build the HTML page as a single string
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<title>ESP32 Sensor</title>";
  html += "<style>body { font-family: monospace; }</style>";
  html += "</head><body>";

  // Display sensor values in a clean monospace layout
  html += "<h2>ESP32 Sensor Data</h2>";
  html += "<div style='font-family: monospace; white-space: pre; margin:0;'>";
  html += tempRegel;
  html += "</div>";
  html += "<div style='font-family: monospace; white-space: pre; margin:0;'>";
  html += "Humidity:    " + String(humidity, 2) + " %";
  html += "</div>";

  // Auto-refresh the page every 2 seconds so values stay up to date
  html += "<script>setTimeout(function(){location.reload();}, 2000);</script>";
  html += "</body></html>";

  return html;
}

// Handles HTTP requests to the root URL ("/")
void handleRoot() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();

  // Check if the sensor returned invalid data (NaN = Not a Number)
  if (isnan(temp) || isnan(hum)) {
    server.send(200, "text/html", "<html><body>Sensor not available</body></html>");
    return;
  }

  String html = createWebPage(temp, hum);

  // Send the HTML webpage to the browser (HTTP 200 = OK)
  server.send(200, "text/html", html);
}

// Renders the current mode and sensor data on the OLED
void showSensorData(float temp, float hum) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Show a warning at the top when the temperature is above the threshold
  // (The LED is controlled in loop() to avoid duplicate logic)
  if (!isnan(temp) && temp > TEMP_DREMPEL) {
    display.println("Warm!");
  }

  if (currentMode == MODE_INFO) {
    // === INFO MODE: shown after a long press, auto-returns after 5 seconds ===
    display.print("IP: ");
    display.println(WiFi.softAPIP());

    // Calculate uptime in hours, minutes, and seconds
    unsigned long totalSeconds = millis() / 1000;
    unsigned long hours = totalSeconds / 3600;
    unsigned long minutes = (totalSeconds % 3600) / 60;
    unsigned long seconds = totalSeconds % 60;

    // Display uptime in compact format (e.g. "1h 2m 17s" or "2m 17s")
    display.print("Uptime: ");
    if (hours > 0) {
      display.print(hours);
      display.print("h ");
    }
    display.print(minutes);
    display.print("m ");
    display.print(seconds);
    display.print("s");
  } else {
    // === SENSOR MODE: default view with live readings ===
    if (buttonPressed) {
      display.println("Button pressed!"); // One-shot acknowledgment
      buttonPressed = false;              // Reset flag so it only shows once
    }
    display.println("Measure Environment");

    if (!isnan(temp) && !isnan(hum)) {
      display.println("Temperature: " + String(temp, 2) + " C");
      display.println("Humidity:    " + String(hum, 2) + " %");
    } else {
      display.println("Sensor error");
    }
  }

  // Push the buffered content to the OLED
  display.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BUTTON, OUTPUT);

  // Start the WiFi access point (no password)
  WiFi.softAP("ESP32WiFi");
  
  // instructions for connecting MyESP32-WiFi on your phone
  Serial.println("Webserver started");
  Serial.println("ESP32 is ready for use");
  Serial.println("Go on your telephone:");
  Serial.println("1. Go to wiFi settings");
  Serial.println("2. Connect to WiFi > MyESP32");
  Serial.println("3. If connected tab on the right side settings");
  Serial.println("4. Go to manage router");
  Serial.print("5. It opens browser > http://");

  server.on("/", handleRoot); // link the URL (main page) to the handleRoot function
  server.begin(); // Start webServer

  Serial.println(WiFi.softAPIP()); // Give me the IP address of my own network
  Serial.println("See live sensor values!");

  // Initialize BME280 sensor on I2C address 0x76
  if (bme.begin(0x76)) {
    sensorReady = true;
  } else {
    Serial.println("Sensor not found!");
  }

  // Initialize OLED screen on I2C address 0x3C
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3c)) {
    oledReady = true;
  } else {
    Serial.println("Oled not found!");
  }
}

void loop() {
  // === Read the button every loop with debounce ===
  int reading = digitalRead(BUTTON_PIN);

  // Reset the debounce timer whenever the raw reading changes
  if (reading != lastReading) {
    lastDebounceTime = millis();
  }
  lastReading = reading;

  // Once the reading has been stable longer than the debounce delay,
  // treat it as the new official button state
  if (millis() - lastDebounceTime > debounceDelay) {
    buttonState = reading;
  }

  // Detect a press transition (HIGH → LOW): start the long-press timer
  if (buttonState != lastButtonState) {
    if (buttonState == LOW) {
      startTime = millis();
      longPressDetected = false; // Allow detection for this new press
      buttonPressed = true;      // Trigger the one-shot OLED message
    }
    lastButtonState = buttonState;
  }

  // While the button is held, check whether it has been held long enough
  if (buttonState == LOW && !longPressDetected) {
    if (millis() - startTime >= longPressTime) {
      currentMode = MODE_INFO;
      infoModeStartTime = millis(); // Start the auto-return timer
      longPressDetected = true;     // Prevent repeated activation

      // Refresh the OLED immediately so the user sees info mode appear
      if (oledReady) {
        showSensorData(lastTemp, lastHum);
      }
    }
  }

  // Auto-return from info mode after the configured duration
  if (currentMode == MODE_INFO) {
    if (millis() - infoModeStartTime >= infoModeDuration) {
      currentMode = MODE_SENSOR;

      // Refresh the OLED immediately so the user sees sensor mode return
      if (oledReady) {
        showSensorData(lastTemp, lastHum);
      }
    }
  }

  // === Sensor + screen update once per UPDATE_INTERVAL ===
  if (millis() - lastUpdate >= UPDATE_INTERVAL) {
    if (sensorReady && oledReady) {
      float temp = bme.readTemperature();
      float hum = bme.readHumidity();

      // Control the warning LED based on the temperature threshold
      if (!isnan(temp) && temp > TEMP_DREMPEL) {
        digitalWrite(LED_BUTTON, HIGH); // Turn on LED if above threshold
      } else {
        digitalWrite(LED_BUTTON, LOW);  // Turn off LED if below threshold
      }

      if (!isnan(temp) && !isnan(hum)) {
        // Cache the latest values so a mode change can refresh the OLED
        // instantly without re-reading the sensor
        lastTemp = temp;
        lastHum = hum;

        Serial.println("Temperature: " + String(temp, 2) + " °C");
        Serial.println("Humidity:    " + String(hum, 2) + " %");
      } else {
        Serial.println("Sensor error");
        sensorReady = false; // Sensor fails > mark as not ready
      }

      showSensorData(temp, hum);
    }

    lastUpdate = millis();
  }

  // The web server must always listen for incoming requests
  server.handleClient();
}
