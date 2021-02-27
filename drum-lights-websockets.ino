// How to use this code:
// To initaize, you need to use a differetn program, DrumLightEEPROMwrite.  That program
// populates the variables in EEPROM that are then read in by this program and used to 
// in setup for this program.  The values that are used can be overwritten by this program, 
// so they don't have to be correct.  In fact, you may be able to use this without the other 
// program, but I'm not sure what will happen if the EEPROM is either empty or full of 
// other stuff so better to be safe.
// Next, load this code, and search WiFi for the name that was initialized by the write
// program, something like 'Bass1'.  Connect to that endpoint, and set your browser to 
// 192.168.4.1.  The AP will require a password, which is set in the 'devicepassword' 
// variable below.  This page does contain a reference to WiFiManger which is commented out
// because it does not work properly on my home network.  Your results may vary.  If you want 
// to have the drums on the same network (and perhaps talking to one another), uncomment the 
// WiFiManger code and use that instead.
// The interrupt button is meant to 'change modes' with the idea being that the board will 
// initialize in 'setup mode' which contains OTA, web server, etc.  Clicking the button will 
// then put the loop into 'off' mode, and clicking it once again would put it in whatever
// mode was set in EERPOM.  The intention was to create an interface in the HTML page to allow
// swithing that last mode via the webpage, but I didn't need to use that functionality for my 
// last project so I never got around to finishing that interface.  I did, however, include a 
// couple more modes that was planned to use one drum to trigger the lights on others, which 
// is the 'broadcasting' and 'remote trigger' modes.  broadcasting would be the drum that is hit
// and sending out the hit, and the 'remote trigger' drums would listen for that hit and 
// trigger the lights.  That functionality was only bench tested but could be cool.  Broadcast
// logic assumes network IP is 192.168.1.XXX
// There are two .h files that I used to describe different data structures; drums.h and 
// modes.h.  Adding additional drums would need to happen both in this file as well as in the 
// HTML interface in the dropdown if you want to use that. 
// The REST interface has a ton of replication and if I had time, I'd clean that up and combine
// the different calls.  From an efficiency standpoint I don't think it matters.
// 
// This code is totally horrible.  Don't use it.  I use it as a playground and have
// all kinds of really screwed up methods of doing things, mostly sticking to whatever
// approach was being done in the code I copied for that section.  It does very little 
// error checking, if any.  This makes Frankenstien's Monster look like a CG cover.
// YOU'VE BEEN WARNED
// Light up the strip of 100 WS2811s around a board
// Webpage also available to control board

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
#define PIXEL_COUNT 13  // Number of NeoPixels - is overridden by EEPROM value

// Device Info
const char* devicename = "DrumTest";  // This is overridden by EEPROM value
const char* devicepassword = "pcepmbdl";

// Declare NeoPixel strip object:
// Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
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

// For drum-to-drum broadcast
WiFiUDP broadcastUdp;
unsigned int broadcastPort = 6789;
IPAddress broadcastIp(192,168,1,255);
char broadcastBuffer[UDP_TX_PACKET_MAX_SIZE];


// Main Page 
// This is the HTML page that is loaded into PROGMEM and will be served up when a client connects.
// It contains javascript functions that call rest APIs on the device when values are changed.

static const char MAIN_PAGE[] PROGMEM = R"====(
<HTML>
<HEAD>
<meta name="viewport" content="width=device-width, initial-scale=1" /> 
<link rel="icon" href="data:,">
<SCRIPT>

var drumId = 0;
var light_color = '#000000';
var light_brightness = 100;
var threshold_setpoint = 100;
var delay_value = 20;
var triggerMode = 0;

var light_on = false;
  
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
       console.log(JSON.stringify(jsonData));
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
function statusLoaded(jsonResponse) {
  console.log(jsonResponse);
}

//
// Handling displaying the current status
//
function initialStatusLoaded(jsonResponse) {
  console.log(jsonResponse);
  var obj = jsonResponse;
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
  document.getElementById('delayValue_label').innerHTML = delay_value;
}
//
// Send the Delay Value
//
function sendDelayValue() {
  var postObj = new Object();
  postObj.delayValue = document.getElementById('delayValue').value;
  restCall('POST', '/delayValue', statusLoaded, JSON.stringify(postObj));  

}

//
// Send the Drum Id
//
function sendDrumId() {
  var postObj = new Object();
  postObj.drumId = document.getElementById('drumIdCombo').value;
  restCall('POST', '/drumID', statusLoaded, JSON.stringify(postObj));  

}

//
// Send the color of the light
//
function sendLightColor() {
  var color = document.getElementById('light_color').value;  
  document.getElementById('color_label').innerHTML = color;
  var postObj = new Object();
  postObj.color = color;
  restCall('POST', '/light', statusLoaded, JSON.stringify(postObj));  
}

//
// Save the values to EEPROM
//
function saveValuesToEEPROM() {
  var postObj = new Object();
  postObj.saveValues = 1;
  connection.send(JSON.stringify(postObj));
  restCall('POST', '/eeprom', statusLoaded, JSON.stringify(postObj));  
}


function sendBrightness() {  
  var brightness = document.getElementById('brightness').value;  
  document.getElementById('brightness_label').innerHTML = brightness;
  var postObj = new Object();
  postObj.brightness = brightness;
//  connection.send(JSON.stringify(postObj)); 
  restCall('POST', '/brightness', statusLoaded, JSON.stringify(postObj));

}

function sendThreshold() {  
  var threshold = document.getElementById('threshold').value;  
  document.getElementById('threshold_setpoint').innerHTML = threshold;
  var postObj = new Object();
  postObj.threshold = threshold;
  //connection.send(JSON.stringify(postObj)); 
  restCall('POST', '/threshold', statusLoaded, JSON.stringify(postObj));
}

//
// actions to perform when the page is loaded
//
function doOnLoad() {
  restCall('GET', '/light', initialStatusLoaded);
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
    Brightness: <input id="brightness" type="range" min="0" max="100" step="1" oninput="sendBrightness();" ><BR>
  </DIV>
  <DIV>
    Threshold: <input id="threshold" type="range" min="0" max="255" step="1" oninput="sendThreshold();" ><br>
  </DIV>
  <DIV>
    <input type='button' id='eeprom_button' value="Save to EEPROM"; style='width: 160px; height: 40px; margin-bottom: 10px;' onClick='saveValuesToEEPROM();'><BR>
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

  //Set the device name to the one stored in EEPROM
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

  // Set static IP to get around WM not working for me
//  IPAddress _ip = IPAddress(192, 168, 1, 13);
//  IPAddress _gw = IPAddress(192, 168, 1, 1);
//  IPAddress _sn = IPAddress(255, 255, 255, 0);
//  wm.setSTAStaticIPConfig(_ip, _gw, _sn);
//  
//  wm.setAPCallback(configModeCallback); //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
//  //if it does not connect it starts an access point with the specified name here  "AutoConnectAP"
//  if (!wm.autoConnect(devicename)) {
//    //Serial.println("failed to connect and hit timeout");
//    //reset and try again, or maybe put it to deep sleep
////    ESP.restart();
////    delay(1000);
//  }
//  Serial.println("connected");

    //Since the WM code doesn't work on my network, I simply setup an AP
    WiFi.softAP(devicename, devicepassword);


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
  // In the future, I'd combine these.
  server.on("/", handleRoot);
  server.on("/light", handleLight);
  server.on("/brightness", handleBrightness);
  server.on("/threshold", handleThreshold);
  server.on("/eeprom", handleEEPROM);
  server.on("/drumID", handleDrumID);
  server.on("/delayValue", handleDelayValue);
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
        server.handleClient();
        handleSensorReading();
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
        server.handleClient();
        handleSensorReading();
  }


}

void handleSensorReading() {    
  int sensorReading = analogRead(PIEZO_PIN);
  //Serial.printf("Sensor reading %i; Threshold %i\n", sensorReading, myDrumLight.threshold);
  if ( sensorReading >= myDrumLight.threshold) {
    //Serial.printf("Turing light on for sensor reading %i against threshold %i\n", sensorReading, gThreshold);
    turnLightOn();
    delay(myDrumLight.delayValue);
  }
  else {
    //Serial.println("Turning light off");
    turnLightOff();
    delay(myDrumLight.delayValue);
  }
}
void handleBroadcastMode() {    
  int sensorReading = analogRead(PIEZO_PIN);
  if ( sensorReading >= myDrumLight.threshold) {
    turnLightOn();
    broadcastUdp.beginPacket(broadcastIp, broadcastPort);
    broadcastUdp.print((byte)1);
    broadcastUdp.endPacket();
    delay(myDrumLight.delayValue);
  }
  else {
    //Serial.println("Turning light off");
    turnLightOff();
    delay(myDrumLight.delayValue);
  }
}
void handleRemoteTriggerMode() {   
    broadcastUdp.read(broadcastBuffer, UDP_TX_PACKET_MAX_SIZE);
    if(broadcastBuffer[0]) {
      turnLightOn();
      delay(myDrumLight.delayValue);
    }
    else {
      turnLightOff();
      delay(myDrumLight.delayValue);
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
  myDrumLight.brightness = mappedValue;
  Serial.printf("Setting brightness value to %i, %i\n", myDrumLight.brightness, bright_value);
  strip.setBrightness(mappedValue);  //valid brightness values are 0<->255
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
  Serial.println("Handling Light");
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
// Handle service for Threshold
//
void handleThreshold() {
  if ((!server.hasArg("plain")) || (server.arg("plain").length() == 0)) {
    server.send(400, "text/plain", "Bad Request - Missing Body");
  }
  StaticJsonDocument<200> requestDoc;
  DeserializationError error = deserializeJson(requestDoc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Bad Request - Parsing JSON Body Failed");
  }
  switch (server.method()) {
    case HTTP_POST:
      if (requestDoc.containsKey("threshold")) {
        myDrumLight.threshold = requestDoc["threshold"];
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
// Handle service for EEPROM
//
void handleEEPROM() {
  if ((!server.hasArg("plain")) || (server.arg("plain").length() == 0)) {
    server.send(400, "text/plain", "Bad Request - Missing Body");
  }
  StaticJsonDocument<200> requestDoc;
  DeserializationError error = deserializeJson(requestDoc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Bad Request - Parsing JSON Body Failed");
  }
  switch (server.method()) {
    case HTTP_POST:
      if (requestDoc.containsKey("saveValues")) {
            saveValues();
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
// Handle service for DrumID
//
void handleDrumID() {
  if ((!server.hasArg("plain")) || (server.arg("plain").length() == 0)) {
    server.send(400, "text/plain", "Bad Request - Missing Body");
  }
  StaticJsonDocument<200> requestDoc;
  DeserializationError error = deserializeJson(requestDoc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Bad Request - Parsing JSON Body Failed");
  }
  switch (server.method()) {
    case HTTP_POST:
     if (requestDoc.containsKey("drumId")) {
       myDrumLight.drumId = requestDoc["drumId"];
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
// Handle service for DrumID
//
void handleDelayValue() {
  if ((!server.hasArg("plain")) || (server.arg("plain").length() == 0)) {
    server.send(400, "text/plain", "Bad Request - Missing Body");
  }
  StaticJsonDocument<200> requestDoc;
  DeserializationError error = deserializeJson(requestDoc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Bad Request - Parsing JSON Body Failed");
  }
  switch (server.method()) {
    case HTTP_POST:
      if (requestDoc.containsKey("saveValues")) {
            saveValues();
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
  return true;
}

//
// Handle setting a brightness 
//
boolean setBrightness() {
  Serial.println("Changing Brightness");
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
  if (!requestDoc.containsKey("brightness")) {
    server.send(400, "text/plain", "Bad Request - Missing Brightness Argument");
    return false;
  }
  
  uint8_t brightnessValue = requestDoc["brightness"];
  setBrightnessValue(brightnessValue);
  strip.show();
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
