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
#include <LuaWrapper.h>


#define PIXEL_PIN    4 //

#define PIXEL_COUNT 100  // Number of NeoPixels

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

// State of the light and it's color
uint8_t gLightBrightness = 100;
int gColor = 0;

// global variables to hold the animation
//uint8_t gFrameIndex = 0;
//uint32_t gDelayTimeLeft = 0;
//uint8_t gNoOfFrames = 0;
//DynamicJsonDocument gRequestDoc(8500);


// For Web Server
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
uint8_t connectedClient = 0;

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
        connectedClient = num;
        // send message to client
        webSocket.sendTXT(num, "Connected");
      }
      break;
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\n", num, payload);

      if (payload[0] == '#')
      {
        gColor = string2color((const char *)payload);
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

var light_on = false;
var light_color = '#000000';
var light_brightness = 100;

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
    displayError(err.message);
  });
}


//
// Handling displaying the current status
//
function statusLoaded (jsonResponse) {
  light_on = jsonResponse.lightOn;
  light_color = jsonResponse.color;
  light_brightness = jsonResponse.brightnessValue;
  document.getElementById('light_color').value = light_color;

  if (light_on) {
    document.getElementById('light_state').innerHTML = 'ON';
    document.getElementById('light_button').value = 'Turn Light OFF';
    document.getElementById('state').style.color = light_color;
  }
  else {
    document.getElementById('light_state').innerHTML = 'OFF';
    document.getElementById('light_button').value = 'Turn Light ON';
    document.getElementById('state').style.color = '#000000';

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
// Increase the light brightness
//
function increaseBrightness() {
  restCall('PUT', '/brightness?up', statusLoaded);
}

//
// Decrease the light brightness
//
function decreaseBrightness() {
  restCall('PUT', '/brightness?down', statusLoaded);
}

//
// Set the color of the light
//
function setLightColor() {
  var postObj = new Object();
  postObj.color = document.getElementById('light_color').value;
  restCall('POST', '/light', statusLoaded, JSON.stringify(postObj));
}


//
// actions to perform when the page is loaded
//
function doOnLoad() {
  restCall('GET', '/light', statusLoaded);
}

function sendRGB() {  
    var r = parseInt(document.getElementById('r').value).toString(16);  
    var g = parseInt(document.getElementById('g').value).toString(16);  
    var b = parseInt(document.getElementById('b').value).toString(16);  
    if(r.length < 2) { r = '0' + r; }   
    if(g.length < 2) { g = '0' + g; }   
    if(b.length < 2) { b = '0' + b; }   
    var rgb = '#'+r+g+b;   

  console.log('RGB: ' + rgb); 
  connection.send(rgb); 
}

  
</SCRIPT>
</HEAD>
<BODY style='max-width: 960px; margin: auto;' onload='doOnLoad();'>
<CENTER><H1>Redline Drum Management Page</H1></CENTER>
<BR>
<BR>
Light is currently <span id='light_state'></span><BR>
Brightness is currently set to <span id='brightness_state'></span><BR>
<HR style='margin-top: 20px; margin-bottom: 10px;'>
<form>
<DIV style='overflow: hidden; margin-top: 10px; margin-bottom: 10px;'>
  <DIV style='text-align: center; float: left;'>
    <label for='light_button'>Change Light:</label><BR>
    <input type='button' id='light_button' name='light_state' style='width: 160px; height: 40px; margin-bottom: 10px;' onClick='changeLight();'><BR>
\  </DIV>
  <DIV style='text-align: center; overflow: hidden;'>
    <label for='light_color'>New Light Color:</label><BR>
    <input type='color' id='light_color' name='light_color' style='width: 120px; height: 40px; margin-bottom: 10px;'><BR>
    <input type='button' id='set_light_color' name='set_light_color' style='width: 120px; height: 40px;' value='Set Color' onClick='setLightColor();'><BR>
  </DIV>
  <DIV>
  LED Control:<br><br>
  Red: <input id="r" type="range" min="0" max="255" step="1" oninput="sendRGB();" ><br>
  Green: <input id="g" type="range" min="0" max="255" step="1" oninput="sendRGB();" ><br>
  Blue: <input id="b" type="range" min="0" max="255" step="1" oninput="sendRGB();" ><br>
  Brightness:  <input id="brightnessSlide" type="range" min="0" max="100" step="1"><BR>
  </DIV>
</DIV>
</form>
<HR style='margin-top: 10px; margin-bottom: 10px;'>
<DIV id='debug' style='font-family: monospace; color:blue; outline-style: solid; outline-color:blue; outline-width: 2px; visibility: hidden; padding-top: 10px; padding-bottom: 10px; margin-top: 10px; margin-bottom: 10px;'></DIV><BR>
<DIV id='errors' style='color:red; outline-style: solid; outline-color:red; outline-width: 2px; visibility: hidden; padding-top: 10px; padding-bottom: 10px; margin-top: 10px; margin-bottom: 10px;'></DIV><BR>
</BODY>
<SCRIPT>


</SCRIPT>
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
  IPAddress _ip = IPAddress(192, 168, 1, 14);
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
  // Done with Setup
  //
  ticker.detach();          // Stop blinking the LED strip
  colorSet(strip.Color(  0, 255,   0)); // Use Green to indicate the setup is done.
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  delay(2000);
  colorSet(strip.Color(0,0,0));
}


/*************************************************
 * Loop
 *************************************************/
void loop() {
  // Handle any requests
  ArduinoOTA.handle();
  webSocket.loop();
  server.handleClient();
  //MDNS.update();

}


/******************************
 * Callback Utilities during setup
 ******************************/

/*
 * Blink the LED Strip.
 * If on  then turn off
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


void setBrightnessValue(uint8_t bright_value) {
  int mappedValue = map(bright_value%255, 0, 100, 1, 254);
  gLightBrightness = mappedValue;
  Serial.printf("Setting brightness value to %i\n", gLightBrightness);
  strip.setBrightness(mappedValue);  //valid brightness values are 0<->255
  
  //Adding test websocket send code here
  char msg_buf[10];
  sprintf(msg_buf, "%d", mappedValue);
  Serial.printf("Sending to [%u]: %s\n", connectedClient, msg_buf);
  webSocket.sendTXT(connectedClient, msg_buf);
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

  // Send back current state of Light
  jsonDoc["lightBrightness"] = gLightBrightness;

  // Send back current state of Color
  String currColorStr = "";
  color2String(&currColorStr);
  jsonDoc["color"] = currColorStr;

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
  gColor = string2color(colorStr);
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
  uint32_t pixelColor = gColor & 0xFFFFFF; // remove any extra settings - only want RGB
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
