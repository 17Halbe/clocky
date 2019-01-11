#define FASTLED_ESP8266_NODEMCU_PIN_ORDER
#define FASTLED_ALLOW_INTERRUPTS 0

#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <ESP8266WiFi.h>        // Include the Wi-Fi library
#include <ESP8266WiFiMulti.h>   // Include WIFI Multi library
#include <time.h>
#include <FS.h>                 // Include the SPIFFS library
#include <ESP8266mDNS.h>
#include <PolledTimeout.h>
#include <ESP8266HTTPUpdateServer.h>
#include "DHTesp.h"

/*
   Global defines and vars
*/
#define DATA_PIN    2        // data pin : D2 (pas 4 grace au define ligne 1)
#define NUM_LEDS    30       // number of LEDs in the strip
#define BRIGHTNESS  255


#define pinDHT22            5                                   // DHT22 Gpio pin
#define TIMEZONE_OFFSET     1                                   // CET

#define SERVICE_PORT        80                                  // HTTP port

const char*                   AP_ssid     = "AP_Name";  // The SSID (name) of the Wi-Fi network we want to connect to
const char*                   AP_password = "Password";     // The password of the Wi-Fi network
const char*                   own_ssid     = "ClockY";         // The SSID (name) of the Wi-Fi network you want to create
const char*                   own_password = "StephanS";     // The password of the Wi-Fi network

char*                         pcHostDomain            = 0;        // Negociated host domain
bool                          bHostDomainConfirmed    = false;    // Flags the confirmation of the host domain
MDNSResponder::hMDNSService   hMDNSService            = 0;        // The handle of the clock service in the MDNS responder
File                          fsUploadFile;                       // a File object to temporarily store the received file

DHTesp dht;
ESP8266HTTPUpdateServer httpUpdater;

// TCP server at port 'SERVICE_PORT' will respond to HTTP requests
ESP8266WebServer                    server(SERVICE_PORT);
ESP8266WiFiMulti wifiMulti;           // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
CRGB LEDs[NUM_LEDS];                                            // this creates an LED array to hold the values for each led in your strip

const char*                   mdnsName                = "clocky";

CRGB colorCRGB = CRGB::Red;           // Change this if you want another default color, for example CRGB::Blue
CHSV colorCHSV = CHSV(95, 255, 255);  // Green
CRGB colorOFF  = CRGB::Black;//(20,20,20);      // Color of the segments that are 'disabled'. You can also set it to CRGB::Black
volatile bool blinkDots = false;        // Set this to true if you want the dots to blink in clock mode, set it to false to disable
volatile byte colorMODE = 0;           // 0=CRGB, 1=CHSV, 2=Constant Color Changing pattern
volatile byte mode = 1;                // 0=Clock, 1=Temperature, 2=Humidity, 3=Scoreboard, 4=Time counter
volatile int scoreLeft = 0;
volatile int scoreRight = 0;
volatile long timerValue = 0;
volatile int timerRunning = 0;
os_timer_t t30sec;
os_timer_t t1sec;
os_timer_t t50milli;
bool refreshDisplayFlag = false;
/*
   handleHTTPClient
*/

/*
   setup
*/
void setup(void) {
  Serial.begin(115200);
  FastLED.delay(3000);
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(LEDs, NUM_LEDS);
  FastLED.setCorrection( TypicalLEDStrip );
  FastLED.setBrightness( BRIGHTNESS );
  FastLED.clear();
  displaySegments(0, 1); 
  displaySegments(7, 15);
  displaySegments(16, 1);    
  displaySegments(23, 15);
  FastLED.show();
  os_timer_setfn(&t1sec, timer1Callback, NULL);
  os_timer_setfn(&t30sec, timer30Callback, NULL);
  os_timer_setfn(&t50milli, timer50Callback, NULL);

  startDHT();
  
  startSPIFFS(); 
  loadConfig();
  
  if (startWiFi() == 0) {  // Connect to WiFi network
    // Sync clock
    startNTP();
  }
  // Setup MDNS responder
  startMDNS();
  

  // Start TCP (HTTP) server
  startOTA();
  //Serial.println("OTA started");

  startServer();
  //Serial.println("TCP server started");
  os_timer_arm(&t30sec, 15000, true);
  os_timer_arm(&t1sec, 1000, true);
  os_timer_arm(&t50milli, 150, true);
  
}
void loadConfig() {
  File fs_file = SPIFFS.open("/setup.cfg", "r");            // Open the file for writing in SPIFFS (create if it doesn't exist)
  if(fs_file) {
    int r = fs_file.readStringUntil('\n').toInt();
    int g = fs_file.readStringUntil('\n').toInt();
    int b = fs_file.readStringUntil('\n').toInt();
    colorCRGB = CRGB(r,g,b);
    mode = (byte) fs_file.readStringUntil('\n').toInt();
    FastLED.setBrightness(fs_file.readStringUntil('\n').toInt());
    colorMODE = fs_file.readStringUntil('\n').toInt();
    blinkDots = (bool) fs_file.readStringUntil('\n').toInt();
    //Serial.println("Config Loaded: ");
    //Serial.println("RGB: " + String(r) + "," + String(g) + "," + String(b));
    //Serial.println("Mode: " + String(mode));
    //Serial.println("Brightness: " + String(FastLED.getBrightness()));
    //Serial.println("ColorMode: " + String(colorMODE));
    //Serial.println("Dots " + String(blinkDots));
  } else { 
    //Serial.println("No config File found");
  }
  fs_file.close();                               // Close the file again
}
void startDHT() {
  dht.setup(pinDHT22, DHTesp::DHT22); // Connect DHT sensor to GPIO 5
}
byte startWiFi() {

  // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
  WiFi.softAP(own_ssid, own_password);             // Start the access point
  //Serial.print("Access Point \"");
  //Serial.print(own_ssid);
  //Serial.println("\" started\r\n");
  if (SPIFFS.exists("/ssid.info")){  // If an accesspoint file exists, get the ssid and password
    File file = SPIFFS.open("/ssid.info", "r");                    // Open the file
    
    String ap_string = file.readStringUntil('\n');
    char ap_name[sizeof(ap_string)];
    ap_string.toCharArray(ap_name,sizeof(ap_string)); //read the ssid name
    
    String pass_string = file.readStringUntil('\n');
    char pass[sizeof(pass_string)];
    pass_string.toCharArray(pass,sizeof(pass_string));    //read the ssid password  
    file.close();     
    wifiMulti.addAP(ap_name,pass);         // add Wi-Fi networks you want to connect to  
    //Serial.print("Connecting to the stronger of " + ap_string + " " + AP_ssid);
    
  }             
  wifiMulti.addAP(AP_ssid,AP_password);

  //Serial.println("Connecting");
  while (wifiMulti.run() != WL_CONNECTED && WiFi.softAPgetStationNum() < 1) {  // Wait for the Wi-Fi to connect
    delay(250);
    switch(wifiMulti.run()) {
      case WL_IDLE_STATUS:
        //Serial.println("Idling..");

        break;
      case WL_NO_SSID_AVAIL:
        //Serial.println("No AP found..");
        break;
      case WL_SCAN_COMPLETED:
        //Serial.println("Scan completed..");
        break;
      case WL_CONNECTED:
        //Serial.println("Connected..");
        break;
      case WL_CONNECT_FAILED:
        //Serial.println("Connection failed..");
        break;
      case WL_CONNECTION_LOST:
        //Serial.println("Connection lost..");
        break;
      case WL_DISCONNECTED:
        //Serial.println("disconnected..");
        break;
    }
  }
  //Serial.println("\r\n");
  if(WiFi.softAPgetStationNum() == 0) {      // If the ESP is connected to an AP
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID());             // Tell us what network we're connected to
    Serial.print("IP address:\t");
    Serial.print(WiFi.localIP());            // Send the IP address of the ESP8266 to the computer
    WiFi.softAPdisconnect(true);
  } else {                                   // If a station is connected to the ESP SoftAP
    //Serial.print("Station connected to ESP8266 AP");
    return 1;
  }
  //Serial.println("\r\n");
  return 0;
}

void startNTP() {
  configTime((TIMEZONE_OFFSET * 3600), (0 * 3600), "pool.ntp.org", "time.nist.gov", "time.windows.com");

  //Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);   // Secs since 01.01.1970 (when uninitalized starts with (8 * 3600 = 28800)
  while (now < 8 * 3600 * 2) {  // Wait for realistic value
    delay(500);
    //Serial.print(".");
    now = time(nullptr);
  }
  checkDST();                   // Update DST
  //Serial.println("");
  //Serial.printf("Current time: %s\n", getTimeString());
}

void startMDNS() {
  MDNS.setProbeResultCallback(MDNSProbeResultCallback, 0);
  // Init the (currently empty) host domain string with 'mdnsName'
  if ((!MDNSResponder::indexDomain(pcHostDomain, 0, mdnsName)) ||
      (!MDNS.begin(pcHostDomain))) {
    //Serial.println("Error setting up MDNS responder!");
    while (1) { // STOP
      delay(1000);
    }
  }
  MDNS.addService("http", "tcp", 80);

  //Serial.println("MDNS responder started");
}

void startSPIFFS() { // Start the SPIFFS and list all contents
  SPIFFS.begin();                             // Start the SPI Flash File System (SPIFFS)
  //Serial.println("SPIFFS started. Contents:");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {                      // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      //Serial.printf("\tFS File: %s\r\n", fileName.c_str());
    }
    //Serial.printf("\n");
  }
  // check for config files
  //REMOVE BEFORE SHIPPING TODO
  //SPIFFS.remove("/upload.html");
  //SPIFFS.remove("/success.html");
  SPIFFS.remove("/index.html");
  //SPIFFS.remove("/success_ssid.html");
  
  
  if (!SPIFFS.exists("/upload.html")) { factoryReset("/upload_orig", "/upload.html"); }
  if (!SPIFFS.exists("/success.html")) { factoryReset("/success_orig", "/success.html"); }
  if (!SPIFFS.exists("/index.html")) { factoryReset("/index_orig", "/index.html"); }
  if (!SPIFFS.exists("/success_ssid.html")) { factoryReset("/success_ssid_orig", "/success_ssid.html"); }
}

void factoryReset(String source, String dest) {
  //Serial.println("No " + dest + " found, recovering from " + source);
  File destF = SPIFFS.open(dest,"w");
  if(!destF){
      //Serial.println("There was an error opening the " + dest + " file for writing\n");
      exit;
  } 
  File sourceF = SPIFFS.open(source, "r");
  if(!sourceF){
    //Serial.println("Failed to open file " + source + " for reading\n");
    exit;
  }
  while(sourceF.available()){
    destF.print((char)sourceF.read());
  }
  destF.close();
  sourceF.close();
}

void startServer() {
  // Setup Webserver
  server.on("/upload", HTTP_GET, []() {                 // if the client requests the upload page
    if (!handleFileRead("/upload.html"))                // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });

  server.on("/upload", HTTP_POST,                       // if the client posts to the upload page
    [](){ server.send(200); },                          // Send status 200 (OK) to tell the client we are ready to receive
    handleFileUpload                                    // Receive and save the file
  );

  server.on("/", HTTP_GET, []() {                       // if the client requests the root page
    if (!handleFileRead("/index.html"))                // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
    });
  //server.on("/reboot", reboot); //ESP.reset();
  server.on("/read", handleRead);
  server.on("/submit", handleSubmit);
  
  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri()))                  // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });  
  server.begin();                           // Actually start the server
  //Serial.println("HTTP server started");
}

void startOTA() {
  httpUpdater.setup(&server);
}

void timer1Callback(void *pArg) {
  refreshTimer();
}
void timer30Callback(void *pArg) {
  refreshDisplayFlag = true; 
  
  checkDST();
}
void timer50Callback(void *pArg) {
  updateHue();
}
/*
   loop
*/
void loop(void) {
  // Allow MDNS processing
  MDNS.update();
  server.handleClient(); 
  if (refreshDisplayFlag) {
    refreshDisplayFlag = false;
    refreshDisplay(); 
  }
}


void updateHue() {
  if (colorMODE != 2)
    return;
    
  colorCHSV.sat = 255;
  colorCHSV.val = 255;
  if (colorCHSV.hue >= 255){
    colorCHSV.hue = 0;
  } else {
    colorCHSV.hue++;
  }
  refreshDisplay();
}

void refreshDisplay() {
  ////Serial.println("refreshing Display. Mode " + String(mode));
  switch (mode) {
    case 0:
      FastLED.clear();
      FastLED.show();
      break;
    case 1:
      displayClock();
      break;
    case 2:
      displayTemperature();
      break;
    case 3:
      displayHumidity();
      break;
    case 4:
      displayScoreboard();
      break;      
    case 5:
      refreshTimer();
      // Time counter has it's own timer
      break;
    case 6:
      refreshTimer();
      // Countdown integrated in case 5
      break;
    default:   
      break; 
  }
}

void refreshTimer() {
  if (mode == 1 && blinkDots) {    
    displayDots(3);
  } else if (mode == 5 && timerRunning == 1) {
    int elapsed = (time(nullptr) - timerValue);
    elapsed = elapsed > 9999 ? 9999 : elapsed;
    int m1 = (elapsed / 60) / 10 ;
    int m2 = (elapsed / 60) % 10 ;
    int s1 = (elapsed % 60) / 10;
    int s2 = (elapsed % 60) % 10;
  
    displaySegments(0, s2); 
    displaySegments(7, s1);
    displaySegments(16, m2);    
    displaySegments(23, m1);  
    displayDots(0);  
  } else if (mode == 6) {
    int remaining = (timerValue - time(nullptr));
    if (remaining > 9999) {
      //Serial.println("More than 9999 seconds remaining. Displaying Clock");
      time_t midnight = remaining;
      struct tm * timeinfo;
      timeinfo = gmtime(&midnight);                         // Also as localtime
      byte m = timeinfo->tm_min;                       // Minutes 0 to 59
      byte h = timeinfo->tm_hour;                       // Seconds 0 to 61 (2 leap sec)
      int hl = (h / 10) == 0 ? 13 : (h / 10);
      int hr = h % 10;
      int ml = m / 10;
      int mr = m % 10;
      displaySegments(0, mr);    
      displaySegments(7, ml);
      displaySegments(16, hr);    
      displaySegments(23, hl);  
      displayDots(0);  
    } else {
      if (remaining % 2 == 0 && remaining < 1) {
        FastLED.clear();
      } else {
        remaining = abs(remaining);
        //Serial.println("Less than 9999 seconds remaining.Displaying raw seconds: " + String(remaining));
        int m1 = (remaining / 1000);
        int m2 = ((remaining - m1 * 1000) / 100);
        int s1 = (remaining - m1 * 1000 - m2 * 100) / 10;
        int s2 = (remaining - m1 * 1000 - m2 * 100 - s1 * 10);
        //Serial.println(String(m1) + String(m2) + String(s1) + String(s2));
        displaySegments(0, s2); 
        displaySegments(7, s1);
        displaySegments(16, m2);    
        displaySegments(23, m1);  
        displayDots(1); 
      } 
    }
  }
  FastLED.show();
}

void displayClock() {  
  ////Serial.println("Refreshing Clock. Time " + String(getTimeString()));

  time_t now = time(nullptr);                      // Update time to now
  struct tm * timeinfo;
  //time(&now);
  timeinfo = gmtime(&now);                         // Also as localtime
  byte m = timeinfo->tm_min;                       // Minutes 0 to 59
  byte h = timeinfo->tm_hour;                       
  int hl = (h / 10) == 0 ? 13 : (h / 10);
  int hr = h % 10;
  int ml = m / 10;
  int mr = m % 10;

  displaySegments(0, mr);    
  displaySegments(7, ml);
  displaySegments(16, hr);    
  displaySegments(23, hl);  
  if (!blinkDots) {displayDots(0);}
  FastLED.show();
}

void displayTemperature() {
  float tmp = readDHT(false);

  if (isnan(tmp)) {
    //Serial.println("Failed to read from DHT sensor!");
  } else {
    int tmp1 = tmp / 10;
    int tmp2 = ((int)tmp) % 10;
    displaySegments(23, tmp1);    
    displaySegments(16, tmp2);
    displaySegments(7,  10);    
    displaySegments(0, 11);
    displayDots(1);  
    FastLED.show();    
  }  
}

void displayHumidity() {
  float hum = readDHT(true);
  if (isnan(hum)) {
    //Serial.println("Failed to read from DHT sensor!");
  } else {
    int hum1 = hum / 10;
    int hum2 = ((int)hum) % 10;
    displaySegments(23, hum1);    
    displaySegments(16, hum2);
    displaySegments(7,  10);    
    displaySegments(0,  12);
    displayDots(1);  
    FastLED.show();    
  }  
}

void displayScoreboard() {
  int s1 = scoreLeft % 10;
  int s2 = scoreLeft / 10;
  int s3 = scoreRight % 10;
  int s4 = scoreRight / 10;
  displaySegments(0, s3);    
  displaySegments(7, s4);
  displaySegments(16, s1);    
  displaySegments(23, s2);
  
  displayDots(2);  
  FastLED.show();  
}

void displayDots(int dotMode) {
  // dotMode: 0=Both on, 1=Both Off, 2=Bottom On, 3=Blink
  switch (dotMode) {
    case 0:
      LEDs[14] = colorMODE == 0 ? colorCRGB : colorCHSV;
      LEDs[15] = colorMODE == 0 ? colorCRGB : colorCHSV; 
      break;
    case 1:
      LEDs[14] = colorOFF;
      LEDs[15] = colorOFF; 
      break;
    case 2:
      LEDs[14] = colorOFF;
      LEDs[15] = colorMODE == 0 ? colorCRGB : colorCHSV; 
      break;
    case 3:
      LEDs[14] = (LEDs[14] == colorOFF) ? (colorMODE == 0 ? colorCRGB : colorCHSV) : colorOFF;
      LEDs[15] = (LEDs[15] == colorOFF) ? (colorMODE == 0 ? colorCRGB : colorCHSV) : colorOFF;
      FastLED.show();  
      break;
    default:
      break;    
  }
}

void displaySegments(int startindex, int number) {
  // Order: 0b0 mid top-left bottom-left bottom bottom-right top-right   top 
  byte numbers[] = {
    0b00111111, // 0    
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111, // 9   
    0b01100011, // º              10
    0b00111001, // C(elcius)      11
    0b01011100, // º lower        12
    0b00000000, // Empty          13
    0b01110001, // F(ahrenheit)   14
    0b01110110, // H              15
  };
  
  for (int i = 0; i < 7; i++) {
    LEDs[i + startindex] = ((numbers[number] & 1 << i) == 1 << i) ? (colorMODE == 0 ? colorCRGB : colorCHSV) : colorOFF;
  } 
}

float readDHT(bool GetHum) {
  static float temp = 99;               //static means keeping the value in between function calls
  static float hum = 0;
  static long lastCheck = 0;
  Serial.println("Reading DHT. Last Check: " + String(lastCheck) + " Now: " + String(millis()) + " Diff to now: " + String(millis() - lastCheck));
  if ( millis() - lastCheck > 2000 ) { // DHT22 does support a request only every 2 seconds. The second one is 
    lastCheck = millis();          // save the new latest data request time 
    if (dht.getStatusString() == "OK") { 
      hum = dht.getHumidity();
      temp= dht.getTemperature();
      //Serial.println("New Reading: Temp: " + String(temp) + " Hum: " + String(hum));
    } else {
      //Serial.println("Couldn't get reading from DHT22");
      return 0;
    }
  }
  return GetHum ? hum : temp;           // If humidity was requested, return it, otherwise temperature
}

void checkDST() {
  static byte dst= 0;
  time_t now = time(nullptr);
  struct tm * timeinfo;
  timeinfo = gmtime(&now);                         // Also as localtime
  byte m = timeinfo->tm_min;                       // Minutes 0 to 59
  byte s = timeinfo->tm_sec;                       // Minutes 0 to 59
  if (m == 0 && s == 0 ) { 
    byte _hour = timeinfo->tm_hour;                        // Hours 0 to 23
    byte _day = timeinfo->tm_mday;                        // Day of month 1 to 31
    byte _month = timeinfo->tm_mon;                        // Months since Jan. 0 to 11
    _month += 1;                                      // Month now 1 to 12
    int _year = timeinfo->tm_year + 1900;                 // Year with 4 digits
    
    //Serial.println("\nChecking for summertime.. \nIt is ");
    if (_month < 3 || _month > 10) { 
      //Serial.println("Wintertime");        // no summertime in Jan, Feb, Nov, Dez
    } else if (_month > 3 && _month < 10) {
      dst = 1; //Serial.println("Summertime");        // summertime in Apr, Mai, Jun, Jul, Aug, Sep
    } else if (_month == 3 && (_hour + 24 * _day) >= (1 + TIMEZONE_OFFSET + 24 * (31 - (5 * _year / 4 + 4) % 7)) || _month == 10 && (_hour + 24 * _day) < (1 + TIMEZONE_OFFSET + 24 * (31 - (5 * _year / 4 + 1) % 7))) {
      dst = 1; //Serial.println("Summertime");        // check more thouroughly during march and october
    } else {
      //Serial.println("Wintertime");        // earlier in march or later in october
    }
    configTime(TIMEZONE_OFFSET * 3600, dst * 3600, "0.europe.pool.ntp.org", "pool.ntp.org", "time.nist.gov");    // setup ntp update for Local Time
  }
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  //Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed verion
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    //Serial.println(String("\tSent file: ") + path);
    return true;
  }
  //Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

void handleFileUpload(){ // upload a new file to the SPIFFS
  HTTPUpload& upload = server.upload();
  String path;
  if(upload.status == UPLOAD_FILE_START){
    path = upload.filename;
    if(!path.startsWith("/")) path = "/"+path;
    if(!path.endsWith(".gz")) {                          // The file server always prefers a compressed version of a file 
      String pathWithGz = path+".gz";                    // So if an uploaded file is not compressed, the existing compressed
      if(SPIFFS.exists(pathWithGz))                      // version of that file must be deleted (if it exists)
         SPIFFS.remove(pathWithGz);
    }
    //Serial.print("handleFileUpload Name: "); //Serial.println(path);
    fsUploadFile = SPIFFS.open(path, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
    path = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile) {                                    // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      //Serial.print("handleFileUpload Size: "); //Serial.println(upload.totalSize);
      server.sendHeader("Location","/success.html");      // Redirect the client to the success page
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

String getContentType(String filename){
  if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

const char* getTimeString(void) {

  static char   acTimeString[32];
  time_t now = time(nullptr);
  ctime_r(&now, acTimeString);
  size_t    stLength;
  while (((stLength = strlen(acTimeString))) &&
         ('\n' == acTimeString[stLength - 1])) {
    acTimeString[stLength - 1] = 0; // Remove trailing line break...
  }
  return acTimeString;
}

bool setStationHostname(const char* p_pcHostname) {

  if (p_pcHostname) {
    WiFi.hostname(p_pcHostname);
    //Serial.printf("setDeviceHostname: Station hostname is set to '%s'\n", p_pcHostname);
  }
  return true;
}

/*
   MDNSProbeResultCallback

   Probe result callback for the host domain.
   If the domain is free, the host domain is set and the clock service is
   added.
   If the domain is already used, a new name is created and the probing is
   restarted via p_pMDNSResponder->setHostname().

*/
bool MDNSProbeResultCallback(MDNSResponder* p_pMDNSResponder,
                             const char* p_pcDomainName,
                             const MDNSResponder::hMDNSService p_hService,
                             bool p_bProbeResult,
                             void* p_pUserdata) {
  //Serial.println("MDNSProbeResultCallback");
  (void) p_pUserdata;

  if ((p_pMDNSResponder) &&
      (0 == p_hService)) {  // Called for host domain
    //Serial.printf("MDNSProbeResultCallback: Host domain '%s.local' is %s\n", p_pcDomainName, (p_bProbeResult ? "free" : "already USED!"));
    if (true == p_bProbeResult) {
      // Set station hostname
      setStationHostname(pcHostDomain);

      if (!bHostDomainConfirmed) {
        // Hostname free -> setup clock service
        bHostDomainConfirmed = true;
      } else {
        // Change hostname, use '-' as divider between base name and index
        if (MDNSResponder::indexDomain(pcHostDomain, "-", 0)) {
          p_pMDNSResponder->setHostname(pcHostDomain);
        } else {
          //Serial.println("MDNSProbeResultCallback: FAILED to update hostname!");
        }
      }
    }
  }
  return true;
}

void handleSubmit() {
  //Serial.println("Handling submit request");
  for (int i = 0; i < server.args(); i++) {
    //Serial.println(server.argName(i) + ": " + server.arg(i));
  }
  
  if (server.hasArg("SSID") && server.hasArg("PASSWORD")) {
    //Serial.println("Got new SSID Info: " + server.arg("SSID"));
    if (saveToFile("ssid.info",server.arg("SSID") + '\n' + server.arg("PASSWORD"))) {
      server.sendHeader("Location","/success_ssid.html");      // Redirect the client to the success page
      server.send(303);
    }
    else { //failed to save
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
  else if (server.hasArg("action")) {
    if (server.arg("action") == "reboot" ) {
      server.send (200, "text/plain", "rebooting...");
      delay(5000);
      WiFi.softAPdisconnect(true);
      if (startWiFi() == 0) {  // Connect to WiFi network
        // Sync clock
        startNTP();
      }    
    }
  }
  else if (server.hasArg("color")) {
    char color[7];
    server.arg("color").toCharArray(color,7);
    long rgb = strtoll( (const char *)&color, NULL, 16);
    // Split them up into r, g, b values
    long r = rgb >> 16;
    long g = rgb >> 8 & 0xFF;
    long b = rgb & 0xFF;
    colorCRGB = CRGB(r,g,b);
    //Serial.println("RGB: " + String((long)r) + " " + String((long)g) + " " + String((long)b));
    //Serial.println("Color: " + server.arg("color") + " Char: " + String(color));
    saveConfig();
    server.send(200, "text/plane", "#" + server.arg("color")); //Send web page
  }
  else if (server.hasArg("mode")) {
    mode = server.arg("mode").toInt();
    switch (mode) {
      case 5:
        timerValue = time(nullptr);
        timerRunning = 1;
        break;
    }
    saveConfig();
    server.send(200, "text/plane", modeToString()); //Send web page
  }
  else if (server.hasArg("Brightness")) {
    FastLED.setBrightness(server.arg("Brightness").toInt());
    saveConfig();
    server.send(200, "text/plane", server.arg("Brightness")); //Send web page
  }
  else if (server.hasArg("blink")) {
    blinkDots = (server.arg("blink") == "true");  
    saveConfig();    
    server.send(200, "text/plane", (blinkDots ? "Blinky" : "Solid")); //Send web page
  }
  else if (server.hasArg("rainbow")) {
    colorMODE = (server.arg("rainbow") == "true") ? 2 : 0;  
    saveConfig();    
    server.send(200, "text/plane", (colorMODE == 0) ? "Boring!" : "Over the Rainbow!"); //Send web page
  }
  else if (server.hasArg("countdown") && server.hasArg("c_mode")) {
    //Serial.println("Starting Countdown: " + server.arg("countdown") + " mode: " + server.arg("c_mode"));
    int hr = (server.arg("countdown")).substring(0,2).toInt();
    int mn = (server.arg("countdown")).substring(3,5).toInt();
    time_t now = time(nullptr);                      // Update time to now
    if (server.arg("c_mode") == "abs") {
      struct tm * timeinfo;
      timeinfo = gmtime(&now);                         // Also as localtime
      timeinfo->tm_hour = hr;
      timeinfo->tm_min = mn;
      timerValue = mktime(timeinfo);
    }
    else if (server.arg("c_mode") == "rel") {
      timerValue = now + hr * 3600 + mn * 60;
    }
    //Serial.println("Timer value: " + String(timerValue) + " Difference: " + String(timerValue - now));
    //Serial.println("hr: " + String(hr) + " mn: " + String(mn));
    server.send(200, "text/plane", "Starting...");
  }
  else if (server.hasArg("score") && server.hasArg("player")) {
    //Serial.println("Starting Scoreboard: " + server.arg("player") + ":" + server.arg("score"));
    server.arg("player") == "left" ? scoreLeft = server.arg("score").toInt() : scoreRight = server.arg("score").toInt();
    server.send(200, "text/plane", scoreLeft > scoreRight ? "Left" : scoreLeft < scoreRight ? "Right" : "Even");
  }
  else if (server.hasArg("reset")) { 
    factoryReset("/upload_orig", "/upload.html");
    factoryReset("/success_orig", "/success.html");
    factoryReset("/index_orig", "/index.html"); 
    factoryReset("/success_ssid_orig", "/success_ssid.html");
    SPIFFS.remove("/setup.cfg");
    server.send(200, "text/plane", "Done...");
    colorCRGB = CRGB::Red;
    colorMODE = 0;           // 0=CRGB, 1=CHSV, 2=Constant Color Changing pattern
    mode = 1;                // 0=Clock, 1=Temperature, 2=Humidity, 3=Scoreboard, 4=Time counter
    scoreLeft = 0;
    scoreRight = 0;
    timerValue = 0;
    timerRunning = 0;
    blinkDots = false;        // Set this to true if you want the dots to blink in clock mode, set it to false to disable
  }
  refreshDisplay();
}
bool saveConfig() {
  if (!saveToFile("/setup.cfg", String(colorCRGB.r) + '\n' + String(colorCRGB.g) + '\n' + String(colorCRGB.b) + '\n' 
      + String(mode) + '\n' 
      + String(FastLED.getBrightness()) + '\n'
      + String(colorMODE) + '\n'
      + String(blinkDots))
      ) {
    //Serial.println("Couldn't save config to file");
  }
}
String modeToString() {
  switch (mode) {
    case 0:
      return "Off";
      break;
    case 1:
      return "Clock";
      break;
    case 2:
      return "Temperature";
      break;
    case 3:
      return "Humidity";
      break;
    case 4:
      return "Scoreboard";
      break;
    case 5:
      return "Stopwatch";
      break;
    case 6:
      return "Countdown";
      break;
    }
}

bool saveToFile(String path, String content) {
  if(!path.startsWith("/")) path = "/"+path;
  //TODO REMOVE BEFORE SHIPPING
  //if(path.endsWith("_orig")) return false; 
  //Serial.print("saving Filename: "); //Serial.println(path);
  //Serial.println("Content: " + content);
  File fs_file = SPIFFS.open(path, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
  path = String();
  if(fs_file) {
    fs_file.print(content); // Write the received bytes to the file
    fs_file.close();                               // Close the file again
    //Serial.print("File Size: "); //Serial.println(fs_file.size());
    return true;
  } else {
    return false;
  }  
}

void handleRead() {
  String timeString = String(getTimeString());
  String init = "";
  String countdown = "";
  if (server.hasArg("init")) {
    long HexRGB = ((long)colorCRGB.r << 16) | ((long)colorCRGB.g << 8 ) | (long)colorCRGB.b; // get value and convert.
    init = "\",\"status\":\"" + modeToString() +
      "\",\"color\":\"#" + String(HexRGB, HEX) +
      "\",\"brightness\":\"" + String(FastLED.getBrightness()) + 
      "\",\"dots\":\"" + String(blinkDots) +
      "\",\"rainbow\":\"" + String(colorMODE != 0);
  }
  if (mode == 5) {
    countdown = "\",\"countdown\":\"" + ((timerValue - time(nullptr)) < 0 ? "Not set" : String(timerValue - time(nullptr)));
  }
  String json = "{\"datetime\":\"" + timeString + 
    "\",\"temperature\":\"" + String(readDHT(false)) + 
    "\",\"humidity\":\"" + String(readDHT(true)) +  
    init +
    countdown +
    "\"}";
  server.send (200, "application/json", json);
  //Serial.println("Time request handled: " + json);
 //server.send(200, "text/plane", getTimeString()); //Send time value only to client ajax request
} 
