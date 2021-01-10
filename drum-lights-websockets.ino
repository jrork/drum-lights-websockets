// Lily's Board LED control
// Light up the strip of 100 WS2811s around a board
// Webpage also available to control board
// Rest interface that supports simple commands, JSON animation, and loading
// of LUA scripts also available
#include <WebSocketsServer_Generic.h>

#include <Adafruit_NeoPixel.h>  // For controling the Light Strip
#include <WiFiManager.h>        // For managing the Wifi Connection by TZAPU
#include <ESP8266WiFi.h>        // For running the Web Server
#include <ESP8266WebServer.h>   // For running the Web Server
#include <ESP8266mDNS.h>        // For running OTA and Web Server
#include <WiFiUdp.h>            // For running OTA
#include <ArduinoOTA.h>         // For running OTA
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <EEPROM.h>
#include "drums.h"

#define PIXEL_PIN    D6 //
#define PIEZO_PIN  A0  // Piezo attached to Analog A0 on Wemos or Gemma D2 (A1)

#define PIXEL_COUNT 13  // Number of NeoPixels

// Device Info
const char* devicename = "DrumTest";
const char* devicepassword = "onairadmin";

// Declare NeoPixel strip object:
// Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, NEO_BRG + NEO_KHZ400);
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)

#include <Ticker.h>
Ticker ticker;
boolean ledState = LOW;   // Used for blinking LEDs when WifiManager in Connecting and Configuring

//typedef struct drumLight Drumlight;

// State of the light and it's color
//uint8_t gLightBrightness = 100;
//int gColor = 65234;
//int gThreshold = 100;

drumLight myDrumLight;

// For Web Server
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
//uint8_t connectedClient = 0;
uint8_t connectedClients[20];
uint8_t connectedClientCount = 0;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  switch (type)
  {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        connectedClients[connectedClientCount] = num;
        // send message to client
        webSocket.sendTXT(num, "Connected");
      }
      break;
    case WStype_TEXT:
      {
        StaticJsonDocument<200> requestDoc;
        DeserializationError error = deserializeJson(requestDoc, payload);
        //      DeserializationError error = deserializeJson(requestDoc, server.arg("plain"));
        if (error) {
          //      server.send(400, "text/plain", "Bad Request - Parsing JSON Body Failed");
          //      return false;
        }
        if (requestDoc.containsKey("color")) {
          String colorStr = requestDoc["color"];
          myDrumLight.color = string2color(colorStr);
        }
        if (requestDoc.containsKey("threshold")) {
          myDrumLight.threshold = requestDoc["threshold"];
          //need to add bounds and error checking here
        }
        if (requestDoc.containsKey("saveValues")) {
          saveValues();
          //need to add bounds and error checking here
        }
      }
      break;

    default:
      break;
  }
}

// Main Page
static const char MAIN_PAGE[] PROGMEM = R"====(
<HTML>
<HEAD>
<link rel="icon" href="data:,">
<SCRIPT>

var drumId = 0;
var light_color = '#000000';
var light_brightness = 100;
var threshold_setpoint = 100;
var delay_value = 20;
var triggerMode = 0;

var light_on = false;

var connection = new WebSocket('ws://'+location.hostname+':81/', ['arduino']);
  
connection.onopen = function () {  connection.send('Connect ' + new Date()); }; 
connection.onerror = function (error) {    console.log('WebSocket Error ', error);};
connection.onmessage = function (e) {  console.log('Server: ', e.data);};

//
// Print an Error message
//
function displayError (errorMessage) {
  document.getElementById('errors').style.visibility = 'visible';
  document.getElementById('errors').innerHTML = document.getElementById('errors').innerHTML + errorMessage + '<BR>';
  
}

//
// Print a Debug message
//
function displayDebug (debugMessage) {
  document.getElementById('debug').style.visibility = 'visible';
  document.getElementById('debug').innerHTML = document.getElementById('debug').innerHTML + debugMessage + '<BR>';
  
}
//
// Function to make a REST call
//
function restCall(httpMethod, url, cFunction, bodyText=null) {
  contentType = 'text/plain';
  if (httpMethod == 'POST') {
    contentType = 'application/json';
  }
  fetch (url, {
    method: httpMethod,
    headers: {
      'Content-Type': contentType
    },
    body: bodyText,
  })
  .then (response => {
    // Check Response Status
    if (!response.ok) {
      throw new Error('Error response: ' + response.status + ' ' + response.statusText);
    }
    return response;
  })
  .then (response => {
    // process JSON response
    const contentType = response.headers.get('content-type');
    if (!contentType || !contentType.includes('application/json')) {
      throw new TypeError("No JSON returned!");
    }
    return response.json();
  })
  .then (jsonData => {
    // Send JSON to callback function if present
    if (cFunction != undefined) {
      displayDebug(JSON.stringify(jsonData));
      cFunction(jsonData);
    }
  })
  .catch((err) => {
    console.log(err.message);
  });
}


//
// Handling displaying the current status
//
function statusLoaded (jsonResponse) {
  console.log("statusLoaded");
  drumId = jsonResponse.drumId;
  //light_on = jsonResponse.lightOn;
  light_color = jsonResponse.color;
  light_brightness = jsonResponse.lightBrightness;
  threshold_setpoint = jsonResponse.threshold;
  delay_value = jsonResponse.delayValue; 
  triggerMode = jsonResponse.triggerMode;
  
  document.getElementById('light_color').value = light_color;

  if (light_on) {
    document.getElementById('light_state').innerHTML = 'ON';
    document.getElementById('light_button').value = 'Turn Light OFF';
  }
  else {
    document.getElementById('light_state').innerHTML = 'OFF';
    document.getElementById('light_button').value = 'Turn Light ON';
  }

  console.log("DrumID" + drumId + " " + light_color);
}


//
// Turn the Light on or off
//
function changeLight() {
  if (light_on) {
    // Light is on -> turn it off
    restCall('DELETE', '/light', statusLoaded);
  }
  else {
    // Light is off -> turn it on
    restCall('PUT', '/light', statusLoaded);
  }
}

//
// Set the color of the light
//
function setLightColor() {
  var postObj = new Object();
  postObj.color = document.getElementById('light_color').value;
  //restCall('POST', '/light', statusLoaded, JSON.stringify(postObj));
  connection.send(JSON.stringify(postObj));
}

//
// Save the values to EEPROM
//
function saveValuesToEEPROM() {
  var postObj = new Object();
  postObj.saveValues = 1;
  connection.send(JSON.stringify(postObj));
}

//
// actions to perform when the page is loaded
//
function doOnLoad() {
  restCall('GET', '/light', statusLoaded);
}

function sendThreshold() {  
  var threshold = document.getElementById('threshold').value;  
  console.log('Threshold Set Point: ' + threshold); 
  document.getElementById('threshold_setpoint').innerHTML = threshold;
  var postObj = new Object();
  postObj.threshold = threshold;
  connection.send(JSON.stringify(postObj)); 
}

  
</SCRIPT>
</HEAD>
<BODY style='max-width: 960px; margin: auto;' onload='doOnLoad();'>
<CENTER><H1>Redline Drum Management Page</H1></CENTER>
<BR>
<BR>
Light is currently <span id='light_state'></span><BR>
Threshold is currently set to <span id='threshold_setpoint'></span><BR>
<HR style='margin-top: 20px; margin-bottom: 10px;'>
<form>
<DIV style='overflow: hidden; margin-top: 10px; margin-bottom: 10px;'>
  <DIV style='text-align: center; float: left;'>
    <label for='light_button'>Change Light:</label><BR>
    <input type='button' id='light_button' name='light_state' style='width: 160px; height: 40px; margin-bottom: 10px;' onClick='changeLight();'><BR>
  </DIV>
  <DIV style='text-align: center; overflow: hidden;'>
    <label for='light_color'>New Light Color:</label><BR>
    <input type='color' id='light_color' name='light_color' style='width: 120px; height: 40px; margin-bottom: 10px;'><BR>
    <input type='button' id='set_light_color' name='set_light_color' style='width: 120px; height: 40px;' value='Set Color' onClick='setLightColor();'><BR>
  </DIV>
  <DIV>
  Threshold Control:<br><br>
  Threshold: <input id="threshold" type="range" min="0" max="255" step="1" oninput="sendThreshold();" ><br>
  </DIV>
</DIV>
</form>
<HR style='margin-top: 10px; margin-bottom: 10px;'>
<DIV id='debug' style='font-family: monospace; color:blue; outline-style: solid; outline-color:blue; outline-width: 2px; visibility: hidden; padding-top: 10px; padding-bottom: 10px; margin-top: 10px; margin-bottom: 10px;'></DIV><BR>
<DIV id='errors' style='color:red; outline-style: solid; outline-color:red; outline-width: 2px; visibility: hidden; padding-top: 10px; padding-bottom: 10px; margin-top: 10px; margin-bottom: 10px;'></DIV><BR>

</BODY>

</HTML>
)====";


/*************************************************
 * Setup
 *************************************************/
void setup() {
  Serial.begin(74880);

  strip.begin(); // Initialize NeoPixel strip object (REQUIRED)
  strip.show();  // Initialize all pixels to 'off'
  ticker.attach(0.6, tick); // start ticker to slow blink LED strip during Setup


  //
  // Set up the Wifi Connection
  //
  WiFi.hostname(devicename);
  WiFi.mode(WIFI_STA);      // explicitly set mode, esp defaults to STA+AP
  WiFiManager wm;
  // wm.resetSettings();    // reset settings - for testing

  // Set static IP to see if it fixes my problem - joe
  IPAddress _ip = IPAddress(192, 168, 1, 13);
  IPAddress _gw = IPAddress(192, 168, 1, 1);
  IPAddress _sn = IPAddress(255, 255, 255, 0);
  wm.setSTAStaticIPConfig(_ip, _gw, _sn);
  
  wm.setAPCallback(configModeCallback); //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  //if it does not connect it starts an access point with the specified name here  "AutoConnectAP"
  if (!wm.autoConnect()) {
    //Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(1000);
  }
  Serial.println("connected");

  // start webSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  //
  // Set up the Multicast DNS
  //
  MDNS.begin(devicename);


  //
  // Set up OTA
  //
  // ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(devicename);
  ArduinoOTA.setPassword(devicepassword);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    //Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    //Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      //Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      //Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      //Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      //Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      //Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();


  //
  // Setup Web Server
  //
  server.on("/", handleRoot);
  server.on("/light", handleLight);
  server.on("/brightness", handleBrightness);
  server.onNotFound(handleNotFound);
  server.begin();
  //Serial.println("HTTP server started");

  //
  // Read saved values from the EEPROM
  //
  EEPROM.begin(sizeof(myDrumLight));
  char* myDrumLightBytes = reinterpret_cast<char*>(&myDrumLight);
  const uint32_t myDrumLightSize = sizeof(myDrumLight);
  
  EEPROM.begin(myDrumLightSize);  //Initialize EEPROM

  for(int index = 0; index < myDrumLightSize; index++){
    myDrumLightBytes[index] = EEPROM.read(index);
  }
  memcpy(&myDrumLight, myDrumLightBytes, sizeof(drumLight));
  
  //
  // Done with Setup
  //
  ticker.detach();          // Stop blinking the LED strip
  colorSet(strip.Color(  0, 255,   0)); // Use Green to indicate the setup is done.

  delay(2000);
  colorSet(strip.Color(0,0,0));

  MDNS.update();
}


/*************************************************
 * Loop
 *************************************************/
void loop() {
  // Handle any requests
  ArduinoOTA.handle();
  webSocket.loop();
  server.handleClient();
  handleSensorReading();
  //MDNS.update();

}

void handleSensorReading() {    
  int sensorReading = analogRead(PIEZO_PIN);
  //Serial.printf("Sensor reading %i; Threshold %i\n", sensorReading, gThreshold);
  if ( sensorReading >= myDrumLight.threshold) {
    //Serial.printf("Turing light on for sensor reading %i against threshold %i\n", sensorReading, gThreshold);
    turnLightOn();
    delay(50);
  }
  else {
    //Serial.println("Turning light off");
    turnLightOff();
    delay(50);
  }
}

/******************************
 * Callback Utilities during setup
 ******************************/

/*
 * Blink the LED Strip.
 * If on  then turn of
 * If off then turn on
 */
void tick()
{
  //toggle state
  ledState = !ledState;
  if (ledState) {
    colorSet(strip.Color(255, 255, 255));
  }
  else {
    colorSet(strip.Color(  0,   0,   0));
  }
}

/*
 * gets called when WiFiManager enters configuration mode
 */
void configModeCallback (WiFiManager *myWiFiManager) {
  //Serial.println("Entered config mode");
  //Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  //Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

/*
 * Turn the Light on to the color specified
 */

void turnLightOn() {
  colorSet(myDrumLight.color);
}


/*
 * Turn the Light off
 */
void turnLightOff() {
  colorSet(strip.Color(  0,   0,   0));    // Black/off
}


void setBrightnessValue(uint8_t bright_value) {
  int mappedValue = map(bright_value%255, 0, 100, 1, 254);
  //gLightBrightness = mappedValue;
  myDrumLight.brightness = mappedValue;
  //Serial.printf("Setting brightness value to %i\n", gLightBrightness);
  strip.setBrightness(mappedValue);  //valid brightness values are 0<->255
  
  //Adding test websocket send code here
  char msg_buf[10];
  sprintf(msg_buf, "%d", mappedValue);
  Serial.printf("Sending to [%u]: %s\n", connectedClients[0], msg_buf);
  webSocket.sendTXT(connectedClients[0], msg_buf);
}


// Fill strip pixels at once. Pass in color
// (as a single 'packed' 32-bit value, which you can get by calling
// strip.Color(red, green, blue) as shown in the loop() function above)
void colorSet(uint32_t color) {
  for(int i=0; i<strip.numPixels(); i++) { // For each pixel in strip...
    strip.setPixelColor(i, color);         //  Set pixel's color (in RAM)
  }
  strip.show();                          //  Update strip to match
}

void saveValues() {
  Serial.println("Saving values to EEPROM");
    char* myDrumLightBytes = reinterpret_cast<char*>(&myDrumLight);
    const uint32_t myDrumLightSize = sizeof(myDrumLight);

  EEPROM.begin(myDrumLightSize);  //Initialize EEPROM

  for(int index = 0; index < myDrumLightSize; index++){
    EEPROM.write(index, myDrumLightBytes[index]);
  }
  
  EEPROM.commit();    //Store data to EEPROM
}


/******************************************
 * Web Server Functions
 ******************************************/

//
// Handle a request for the root page
//
void handleRoot() {
  Serial.println("Handling Root");
  server.send_P(200, "text/html", MAIN_PAGE, sizeof(MAIN_PAGE));
}

//
// Handle service for Light
//
void handleLight() {
  switch (server.method()) {
    case HTTP_POST:
      if (setLightColor()) {
        sendStatus();
      }
      break;

    case HTTP_GET:
      sendStatus();
      break;
    default:
      server.send(405, "text/plain", "Method Not Allowed");
      break;
  }
}


//
// Handle service for Brightness
//
void handleBrightness() {
  switch (server.method()) {
    case HTTP_POST:
      if (setBrightness()) {
        sendStatus();
      }
      break;
      
    case HTTP_GET:
      sendStatus();
      break;
    default:
      server.send(405, "text/plain", "Method Not Allowed");
      break;
  }
}

  
//
// Handle returning the status of the strip
//
void sendStatus() {
  DynamicJsonDocument jsonDoc(1024);

  jsonDoc["drumId"] = myDrumLight.drumId;
  String currColorStr = "";
  color2String(&currColorStr);
  jsonDoc["color"] = currColorStr;
  jsonDoc["lightBrightness"] = myDrumLight.brightness;
  jsonDoc["threshold"] = myDrumLight.threshold;
  jsonDoc["delayValue"] = myDrumLight.delayValue;
  jsonDoc["triggerMode"] = myDrumLight.triggerMode;

  String payload;
  serializeJson(jsonDoc, payload);
  server.send(200, "application/json", payload);

  webSocket.sendTXT(connectedClients[0], payload);
}

//
// Handle setting a new color for the strip
//
boolean setLightColor() {
  if ((!server.hasArg("plain")) || (server.arg("plain").length() == 0)) {
    server.send(400, "text/plain", "Bad Request - Missing Body");
    return false;
  }
  StaticJsonDocument<200> requestDoc;
  DeserializationError error = deserializeJson(requestDoc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Bad Request - Parsing JSON Body Failed");
    return false;
  }
  if (!requestDoc.containsKey("color")) {
    server.send(400, "text/plain", "Bad Request - Missing Color Argument");
    return false;
  }
  String colorStr = requestDoc["color"];
  myDrumLight.color = string2color(colorStr);
  sendStatus();
  return true;
}

//
// Handle setting a brightness 
//
boolean setBrightness() {
  if ((!server.hasArg("plain")) || (server.arg("plain").length() == 0)) {
    server.send(400, "text/plain", "Bad Request - Missing Body");
    return false;
  }
  StaticJsonDocument<200> requestDoc;
  DeserializationError error = deserializeJson(requestDoc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Bad Request - Parsing JSON Body Failed");
    return false;
  }
  if (!requestDoc.containsKey("brightnessValue")) {
    server.send(400, "text/plain", "Bad Request - Missing Brightness Argument");
    return false;
  }
  
  uint8_t brightnessValue = requestDoc["brightnessValue"];
  setBrightnessValue(brightnessValue);
  strip.show();
  sendStatus();
  return true;
}

//
// Display a Not Found page
//
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}


//
// Convert uint32_t color to web #RRGGBB string
//
void color2String (String* colorString) {
  uint32_t pixelColor = myDrumLight.color & 0xFFFFFF; // remove any extra settings - only want RGB
  colorString->concat("#000000" + String(pixelColor,HEX));
  colorString->setCharAt(colorString->length()-7, '#');
  colorString->remove(0,colorString->length()-7);
}

//
// Convert web #RRGGBB string to uint32_t color
//
uint32_t string2color(String colorStr) {
  if (colorStr.charAt(0) == '#') {
    colorStr.setCharAt(0, '0');
  }
  char color_c[10] = "";
  colorStr.toCharArray(color_c, 8);
  uint32_t color = strtol(color_c, NULL, 16);
  return color;
}
