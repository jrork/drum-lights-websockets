// Lily's Board LED control
// Light up the strip of 100 WS2811s around a board
// Webpage also available to control board
// Rest interface that supports simple commands, JSON animation, and loading
// of LUA scripts also available
#include <WebSocketsServer_Generic.h>

#include <Adafruit_NeoPixel.h>  // For controling the Light Strip
#include <ESP8266WiFi.h>        // For running the Web Server
#include <ESP8266WebServer.h>   // For running the Web Server
#include <ESP8266mDNS.h>        // For running OTA and Web Server
#include <WiFiManager.h>        // For managing the Wifi Connection by TZAPU
#include <WiFiUdp.h>            // For running OTA
#include <ArduinoOTA.h>         // For running OTA
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <EEPROM.h>
#include "drums.h"
#include "modes.h"

#define PIXEL_PIN    D6 //
#define PIEZO_PIN  A0  // Piezo attached to Analog A0 on Wemos or Gemma D2 (A1)
#define BUTTON_PIN D3 
#define PIXEL_COUNT 13  // Number of NeoPixels

// Device Info
const char* devicename = "DrumTest";
const char* devicepassword = "onairadmin";

// Declare NeoPixel strip object:
// Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
//Adafruit_NeoPixel strip;
Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, NEO_BRG + NEO_KHZ400); //<--- use this one

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

struct drumLight {
  drumID drumId;
  uint32_t color;
  uint8_t brightness;
  uint32_t threshold;
  uint8_t delayValue;
  modeID triggerMode;
  uint8_t pixelCount;
};

drumLight myDrumLight;
volatile modeID gTriggerMode = setupMode;
volatile uint32_t interruptMills = millis() ;
uint8_t interruptDebounce = 150;
 
// For Web Server
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
//uint8_t connectedClient = 0;
uint8_t connectedClients[20];
uint8_t connectedClientCount = 0;

WiFiUDP broadcastUdp;
unsigned int broadcastPort = 6789;
IPAddress broadcastIp(192,168,1,255);
char broadcastBuffer[UDP_TX_PACKET_MAX_SIZE];

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
        Serial.printf("connectedClientCount is: %i, %i\n", connectedClients[connectedClientCount], connectedClientCount);
        // send message to client
        //webSocket.sendTXT(connectedClients[connectedClientCount], "Connected");
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
   
        if (requestDoc.containsKey("drumId")) {
          myDrumLight.drumId = requestDoc["drumId"];
        }    
        if (requestDoc.containsKey("color")) {
          String colorStr = requestDoc["color"];
          myDrumLight.color = string2color(colorStr);
        }
        if (requestDoc.containsKey("threshold")) {
          myDrumLight.threshold = requestDoc["threshold"];
          //need to add bounds and error checking here
        }
        if (requestDoc.containsKey("brightness")) {
          myDrumLight.brightness = requestDoc["brightness"];
          setBrightnessValue(myDrumLight.brightness);
          //need to add bounds and error checking here
        }
        if (requestDoc.containsKey("delayValue")) {
          myDrumLight.drumId = requestDoc["delayValue"];
        }
        if (requestDoc.containsKey("triggerMode")) {
          myDrumLight.drumId = requestDoc["triggerMode"];
        }
        if (requestDoc.containsKey("getStatus")) {
            sendStatus();
        }
        if (requestDoc.containsKey("saveValues")) {
            saveValues();
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
connection.onmessage = function (e) {  
  statusLoaded(e.data);
  };

//
// Handling displaying the current status
//
function statusLoaded(jsonResponse) {
  console.log(jsonResponse);
  var obj = JSON.parse(jsonResponse);
  drumId = obj.drumId;
  light_color = obj.color;
  light_brightness = obj.lightBrightness;
  threshold_setpoint = obj.threshold;
  delay_value = obj.delayValue; 
  triggerMode = obj.triggerMode;
  
  document.getElementById('drumIdCombo').value = drumId;
  document.getElementById('light_color').value = light_color;
  document.getElementById('brightness').value = light_brightness;
  document.getElementById('brightness_label').innerHTML = light_brightness;
  document.getElementById('threshold').value = threshold_setpoint;     
  document.getElementById('threshold_setpoint').innerHTML = threshold_setpoint;
  document.getElementById('color_label').innerHTML = light_color;
}


//
// Send the Drum Id
//
function sendDrumId() {
  var postObj = new Object();
  postObj.drumId = document.getElementById('drumIdCombo').value;
  connection.send(JSON.stringify(postObj));
}

//
// Send the color of the light
//
function sendLightColor() {
  var postObj = new Object();
  postObj.color = document.getElementById('light_color').value;
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
  var postObj = new Object();
  postObj.getStatus = 1;
  setTimeout(() => {connection.send(JSON.stringify(postObj));}, 1000)
}

function sendBrightness() {  
  var brightness = document.getElementById('brightness').value;  
  document.getElementById('brightness_label').innerHTML = brightness;
  var postObj = new Object();
  postObj.brightness = brightness;
  connection.send(JSON.stringify(postObj)); 
}

function sendThreshold() {  
  var threshold = document.getElementById('threshold').value;  
  document.getElementById('threshold_setpoint').innerHTML = threshold;
  var postObj = new Object();
  postObj.threshold = threshold;
  connection.send(JSON.stringify(postObj)); 
}

  
</SCRIPT>
</HEAD>
<BODY style='max-width: 960px; margin: auto;' onload='doOnLoad();'>

Threshold is currently set to <span id='threshold_setpoint'></span><BR>
Color is currently set to <span id='color_label'></span><BR>
Brightness is currently set to <span id='brightness_label'></span><BR>
<form>
<select id="drumIdCombo", onchange="sendDrumId();">
  <option value="0">Bass 1</option>
  <option value="1">Bass 2</option>
  <option value="2">Bass 3</option>
  <option value="3">Bass 4</option>
  <option value="4">Bass 5</option>
  <option value="5">Bass 6</option>
  <option value="6">Bass 7</option>
  <option value="7">Bass 8</option>
  <option value="8">Snare 1</option>
  <option value="9">Snare 2</option>
  <option value="10">Snare 3</option>
  <option value="11">Snare 4</option>
  <option value="12">Snare 5</option>
  <option value="13">Snare 6</option>
  <option value="14">Snare 7</option>
  <option value="15">Snare 8</option>
  <option value="16">Snare 9</option>
  <option value="17">Snare 10</option>
  <option value="18">Kit Snare</option>
  <option value="19">Kit High Tom</option>
  <option value="20">Kit Low Tom</option>
  <option value="21">Kit Floor Tom</option>
  <option value="22">Kit Kick</option>
</select>
<DIV style='overflow: hidden; margin-top: 10px; margin-bottom: 10px;'>
  <DIV>
    <input type='color' id='light_color' name='light_color' style='width: 120px; height: 40px; margin-bottom: 10px;' oninput="sendLightColor();"><BR>
  </DIV>
  <DIV>
    Brightness: <input id="brightness" type="range" min="0" max="255" step="1" oninput="sendBrightness();" ><BR>
  </DIV>
  <DIV>
    Threshold: <input id="threshold" type="range" min="0" max="255" step="1" oninput="sendThreshold();" ><br>
  </DIV>
  <DIV>
    <input type='button' id='eeprom_button' value="Save"; style='width: 160px; height: 40px; margin-bottom: 10px;' onClick='saveValuesToEEPROM();'><BR>
  </DIV>
</DIV>
</form>
</BODY>

</HTML>
)====";


/*************************************************
 * Setup
 *************************************************/
void setup() {
  Serial.begin(74880);
  
  //
  // Read saved values from the EEPROM
  //
//  EEPROM.begin(sizeof(myDrumLight));
//  char* myDrumLightBytes = reinterpret_cast<char*>(&myDrumLight);
//  const uint32_t myDrumLightSize = sizeof(myDrumLight);
//  
//  EEPROM.begin(myDrumLightSize);  //Initialize EEPROM
  char* myDrumLightBytes = reinterpret_cast<char*>(&myDrumLight);
  const uint32_t myDrumLightSize = sizeof(myDrumLight);
  EEPROM.begin(myDrumLightSize);  
  for(int index = 0; index < myDrumLightSize; index++){
    myDrumLightBytes[index] = EEPROM.read(index);
  }
  memcpy(&myDrumLight, myDrumLightBytes, sizeof(drumLight));

  Serial.printf("Drum id after EEPROM read: %s\n", DrumText[myDrumLight.drumId]);
  Serial.printf("Color value after EEPROM read: %d\n", myDrumLight.color);
  Serial.printf("Brightness value after EEPROM read: %i\n", myDrumLight.brightness);
  Serial.printf("Threshold value after EEPROM read: %i\n", myDrumLight.threshold);
  Serial.printf("Delay time value after EEPROM read: %i\n", myDrumLight.delayValue);
  Serial.printf("Trigger Mode value after EEPROM read: %i\n", myDrumLight.triggerMode);
  Serial.printf("Trigger Mode value after EEPROM read: %s\n", ModeText[myDrumLight.triggerMode]);
  Serial.printf("Pixel count value after EEPROM read: %i\n", myDrumLight.pixelCount);
  Serial.printf("MAC: ");
  Serial.println(WiFi.macAddress());
  
  devicename = DrumText[myDrumLight.drumId];
  strip.updateLength(myDrumLight.pixelCount);
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

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleInterrupt, FALLING);

  //
  // Done with Setup
  //
  ticker.detach();          // Stop blinking the LED strip
  colorSet(strip.Color(0, 255,0)); // Use Green to indicate the setup is done.

  delay(2000);
  colorSet(strip.Color(0,0,0));

  MDNS.update();
}


/*************************************************
 * Loop
 *************************************************/
void loop() {
  //Serial.println(ModeText[gTriggerMode]);
  switch(gTriggerMode){
    case(offMode):
    //Serial.print("Doing nothing\n");
      break;
   case(lightOnHitMode):
      handleSensorReading();
      break;
   case(setupMode):
        // Handle any requests
        ArduinoOTA.handle();
        webSocket.loop();
        server.handleClient();
        handleSensorReading();
        //MDNS.update();
      break;
   case(broadcast):
      handleBroadcastMode();
      break;   
   case(remoteTrigger):
      handleRemoteTriggerMode();
      break;
   default:
        // Handle any requests
        ArduinoOTA.handle();
        webSocket.loop();
        server.handleClient();
        handleSensorReading();
        //MDNS.update();
  }


}

void handleSensorReading() {    
  int sensorReading = analogRead(PIEZO_PIN);
  //Serial.printf("Sensor reading %i; Threshold %i\n", sensorReading, myDrumLight.threshold);
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
void handleBroadcastMode() {    
  int sensorReading = analogRead(PIEZO_PIN);
  if ( sensorReading >= myDrumLight.threshold) {
    turnLightOn();
    broadcastUdp.beginPacket(broadcastIp, broadcastPort);
    broadcastUdp.print((byte)1);
    broadcastUdp.endPacket();
    delay(50);
  }
  else {
    //Serial.println("Turning light off");
    turnLightOff();
    delay(50);
  }
}
void handleRemoteTriggerMode() {   
    broadcastUdp.read(broadcastBuffer, UDP_TX_PACKET_MAX_SIZE);
    if(broadcastBuffer[0]) {
      turnLightOn();
      delay(50);
    }
    else {
      turnLightOff();
      delay(50);
    }

}

ICACHE_RAM_ATTR void handleInterrupt() {
  if ((millis() - interruptMills) >= interruptDebounce) {
    if (gTriggerMode != myDrumLight.triggerMode) {
        gTriggerMode = myDrumLight.triggerMode;
      }
      else {
        gTriggerMode = offMode;
      }
      interruptMills = millis();
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
  //char msg_buf[100];
  //sprintf(msg_buf, "New Brightness value %d", mappedValue);
  //Serial.printf("Sending to [%u]: %s\n", connectedClients[connectedClientCount], msg_buf);
  //sendStatus();
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
  //sendStatus();
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
  //sendStatus();
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
