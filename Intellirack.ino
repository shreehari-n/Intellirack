/**************************************************************************************
 * INCLUDE
 **************************************************************************************/

#include <Arduino_FreeRTOS.h>
#include <WiFiS3.h>
#include "arduino_secrets.h" 
#include <Arduino.h>
#include "HX711.h" // Library HX711 by Bogdan Necula: https://github.com/bogde/HX711
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>


/**************************************************************************************
 * GLOBAL VARIABLES
 **************************************************************************************/
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int status = WL_IDLE_STATUS;     // the WiFi radio's status
WiFiUDP Udp; // A UDP instance to let us send and receive packets over UDP
NTPClient timeClient(Udp);
const char serverAddress[] = "192.168.29.46";  
const int serverPort = 5000;
WiFiClient wifiCl;  
HttpClient http = HttpClient(wifiCl, serverAddress, serverPort);

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = 2;
const int LOADCELL_SCK_PIN = 3;

// Define IR pin numbers
const int irSensorPin = 4; // IR Sensor output pin connected to digital pin 4
const int ledPin = 13;     // LED connected to digital pin 13 (built-in LED on many Arduinos)

HX711 scale;
int currentWeight;
int prevWeight;
int prodWeight = 25; //Weight of single product in Grams
int prodCount;
int newProdCount;
int prodAddToCart;

//REPLACE WITH YOUR CALIBRATION FACTOR
#define CALIBRATION_FACTOR 225.5

int footfall = 0;
int interestCount = 0;

unsigned long StartTime;
unsigned long StopTime;
bool objectDetected = false;     // Flag to track detection state
unsigned long currentTime;


// char msg[256]= {0};

TaskHandle_t IRReadTask, WeightReadTask, sendDataTask;

/**************************************************************************************
 * SETUP/LOOP
 **************************************************************************************/

void setup()
{
  Serial.begin(115200);
  while (!Serial) { }


  /* Init a task that calls 'loop'
   * since after the call to
   * 'vTaskStartScheduler' we'll never
   * get out of setup() and therefore
   * would never get to loop(), as we
   * are leaving the default execution
   * flow.
   */
  auto const IRRead = xTaskCreate
    (
      IRRead_func,
      static_cast<const char*>("IRRead Thread"),
      1024 / 4,   //    usStackDepth in words 
      nullptr,   /* pvParameters */
      1,         /* uxPriority */
      &IRReadTask /* pxCreatedTask */
    );

  if (IRRead != pdPASS) {
    Serial.println("Failed to create 'IRRead' thread");
    return;
  }

  auto const WeightRead = xTaskCreate
    (
      WeightRead_func,
      static_cast<const char*>("WeightRead Thread"),
      1024 / 4,   //   usStackDepth in words 
      nullptr,     /* pvParameters */
      2,           /* uxPriority */
      &WeightReadTask /* pxCreatedTask */
    );

  if (WeightRead != pdPASS) {
    Serial.println("Failed to create 'WeightRead' thread");
    return;
  }
/*  auto const connectWiFi = xTaskCreate
    (
      connectWiFi_func,
      static_cast<const char*>("connectWiFi Thread"),
      512 / 4,     // usStackDepth in words
      nullptr,     // pvParameters 
      5,           // uxPriority 
      &WifiTask // pxCreatedTask 
    ); 

    

  if (connectWiFi != pdPASS) {
    Serial.println("Failed to create 'connectWiFi' thread");
    return;
  }

  auto const timeStamp = xTaskCreate
    (
      timeStamp_func,
      static_cast<const char*>("timeStamp Thread"),
      512 / 4,   // usStackDepth in words 
      nullptr,   // pvParameters 
      2,         // uxPriority 
      &timeStampTask // pxCreatedTask 
    );

  if (timeStamp != pdPASS) {
    Serial.println("Failed to create 'timeStamp' thread");
    return;
  }
*/
  auto const sendData = xTaskCreate
    (
      sendData_func,
      static_cast<const char*>("sendData Thread"),
      2048 / 4,  // 16384 bytes -> 4096 words (conservative for WiFi/NTP/Serial) usStackDepth in words 
      nullptr,   /* pvParameters */
      4,         /* uxPriority */
      &sendDataTask /* pxCreatedTask */
    );

  if (sendData != pdPASS) {
    Serial.println("Failed to create 'sendData' thread");
    return;
  }
  

// check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);
  delay(10000); //Wait for 10 seconds to connect
  }
  // you're connected now, so print out the data:
  Serial.print("You're connected to the network");
  printCurrentNet();
  printWifiData();


  Serial.println("Starting scheduler ...");
  /* Start the scheduler. */
  vTaskStartScheduler();
  /* We'll never get here. */
  for( ;; );
}

void loop()
{
  // vTaskDelay(configTICK_RATE_HZ/5);
}

void IRRead_func(void *pvParameters)
{
  /* setup() */
  //IRRead Setup Here 
  // Set the IR sensor pin as input
  pinMode(irSensorPin, INPUT);
  // Set the LED pin as output
  pinMode(ledPin, OUTPUT);

  for(;;)
  {
    // IR Read Func Here
    // Read the state from the IR sensor
  // Digital IR sensors typically output LOW when an obstacle is detected
  // and HIGH when no obstacle is present.
  int sensorState = digitalRead(irSensorPin);

  // Check if an obstacle is detected
  if (sensorState == LOW && !objectDetected) {
    StartTime = millis();
    objectDetected = true;
    footfall = footfall + 1;
    // Serial.print("StartTime:");
    // Serial.println(StartTime);
    digitalWrite(ledPin, HIGH); // Turn the LED on
    // Serial.println("Obstacle detected!"); // Print a message to the serial monitor (optional)
  } else if (sensorState == HIGH && objectDetected){
    StopTime = millis();
     objectDetected = false;
    // Serial.print("StopTime:");
    // Serial.println(StopTime);
    unsigned long ElapsedTime = StopTime - StartTime;
    // Serial.print("ElapsedTime:");
    // Serial.println(ElapsedTime);
    digitalWrite(ledPin, LOW); // Turn the LED off
    // Serial.println("No obstacle.");// Print a message to the serial monitor (optional)
    if(ElapsedTime > 10000){
        interestCount = interestCount + 1;
    }
  }
  // Serial.print("footfall:");
  // Serial.println(footfall);
  // Serial.print("interestCount:");
  // Serial.println(interestCount); 
  vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void WeightRead_func(void *pvParameters)
{
  /* setup() */
  //Weight Read Setup here  
  Serial.println("Initializing the scale");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);   // this value is obtained by calibrating the scale with known weights
  scale.tare();               // reset the scale to 0
  /* loop() */
  for(;;)
  {
    // Weight Read Func Here
    if (scale.wait_ready_timeout(200)) {
    currentWeight = round(scale.get_units(10));
    // Serial.print("Weight: ");
    // Serial.println(currentWeight);
    // prodCount = currentWeight/prodWeight;
    // Serial.print("Product Count:");
    // Serial.println(prodCount);


    if (abs(currentWeight - prevWeight)< 2){
      currentWeight = prevWeight;
    }
    prevWeight = currentWeight;
    newProdCount = currentWeight/prodWeight;

    if(newProdCount != prodCount){
      // Serial.print("Prev count: ");
        // Serial.print(prodCount);
        // Serial.print("  New count: ");
        // Serial.println(newProdCount);

       // if products were removed from shelf (customer added to cart)
        if (newProdCount < prodCount) {
          int removed = prodCount - newProdCount;
          prodAddToCart += removed;
        }
      prodCount = newProdCount;
      }

    }
    
    else {
    Serial.println("HX711 not found.");
     }
   vTaskDelay(pdMS_TO_TICKS(1000));
   }
}

/*
void connectWiFi_func(void *pvParameters)
{
  
}
*/



void printWifiData() {
  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  
  Serial.println(ip);

  // print your MAC address:
  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC address: ");
  printMacAddress(mac);
}

void printCurrentNet() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print the MAC address of the router you're attached to:
  byte bssid[6];
  WiFi.BSSID(bssid);
  Serial.print("BSSID: ");
  printMacAddress(bssid);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.println(rssi);

  // print the encryption type:
  byte encryption = WiFi.encryptionType();
  Serial.print("Encryption Type:");
  Serial.println(encryption, HEX);
  Serial.println();
}


void printMacAddress(byte mac[]) {
  for (int i = 0; i < 6; i++) {
    if (i > 0) {
      Serial.print(":");
    }
    if (mac[i] < 16) {
      Serial.print("0");
    }
    Serial.print(mac[i], HEX);
  }
  Serial.println();
}



/*
  void timeStamp_func(void *pvParameters)
{
  // setup() 
  timeClient.begin();
  // loop() 
  for(;;)
  {
  timeClient.update();

  // Get the current date and time from an NTP server and convert
  // it to UTC +5.5 by passing the time zone offset in hours.
  // You may change the time zone offset to your local one.
  auto timeZoneOffsetHours = 5.5;
  auto unixTime = timeClient.getEpochTime() + (timeZoneOffsetHours * 3600);
  Serial.print("Unix time = ");
  Serial.println(unixTime); 
  currentTime = unixTime;
  }
    
}
*/


void sendData_func(void *pvParameters)
{
/* setup() */
  timeClient.begin();

  /* loop() */
  for(;;)
  {
    timeClient.update();

  // Get the current date and time from an NTP server and convert
  // it to UTC +5.5 by passing the time zone offset in hours.
  // You may change the time zone offset to your local one.
  // auto timeZoneOffsetHours = 5.5;
  auto unixTime = timeClient.getEpochTime(); //+ (timeZoneOffsetHours * 3600);
  
      //TODO Convert unixTime to human-readable UTC string

    StaticJsonDocument<256> msg;
    // snprintf(msg, sizeof(msg) - 1, "{\"storeID\": \"storeName\",\"deviceType\":\"sensor\",\"shelfID\": \"promoShelf100\",\"footfalls\":%d,\"interests\":%d,\"productAddedToCart\":%d,\"timestamp\":%lu}",footfall,interestCount,prodAddToCart,unixTime);
    msg["storeID"] = "storeName";
    msg["deviceType"] = "sensor";
    msg["shelfID"] = "promoShelf100";
    msg["footfalls"] = footfall;
    msg["interests"] = interestCount;
    msg["productAddedToCart"] = prodAddToCart;
    msg["timestamp"] = unixTime;

    String payload;
    serializeJson(msg, payload);


    // Serial.print("msg:");
    // Serial.println(msg);

    // Serial.print("payload:");
    Serial.println(payload);
/*
  // Perform POST request
  http.beginRequest();
  http.post("/post-data");
  http.sendHeader("Content-Type", "application/json");
  // http.sendHeader("Content-Length", msg.length());
  http.beginBody();
  http.print(payload);
  http.endRequest();


  // Get response
  int statusCode = http.responseStatusCode();
  String response = http.responseBody();

  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
*/
   vTaskDelay(pdMS_TO_TICKS(30000)); // send once in 30 second
  }
}