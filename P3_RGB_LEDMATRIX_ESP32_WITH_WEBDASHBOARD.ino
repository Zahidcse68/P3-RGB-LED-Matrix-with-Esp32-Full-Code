#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include "time.h"
#include <HTTPClient.h> 
#include <ArduinoJson.h>
#include <Fonts\FreeSansBold9pt7b.h>
#include "clockFont.h"
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

// --- WIFI & LOCATION SETTINGS ---
const char* ssid = "iphone";
const char* password = "12345678";

const long gmtOffset_sec = 19800; // India Time
const int daylightOffset_sec = 0;
String latitude = "33.730726126801294"; //enter your latitude
String longitude = "75.14878599117765";//enter your longitude
String apiKey = "00f38e5701138c298c08770e4410c5b9"; //enter weather api here watch previous video to setup on youtube

// --- PIN DEFINITIONS (P3 64x64) ---
#define R1_PIN 19
#define G1_PIN 13
#define B1_PIN 18
#define R2_PIN 5
#define G2_PIN 12
#define B2_PIN 17
#define A_PIN 16
#define B_PIN 14
#define C_PIN 4
#define D_PIN 27
#define E_PIN 32  
#define LAT_PIN 26
#define OE_PIN 22  
#define CLK_PIN 23 
#define BUZZER_PIN 25 

// --- API URLS ---
const char* ntpServer = "pool.ntp.org";
String weatherServerName = "https://api.openweathermap.org/data/2.5/weather?lat=" + latitude + "&lon=" + longitude + "&appid=" + apiKey + "&units=metric";
String prayerServerName = "https://api.aladhan.com/v1/timings?latitude=" + latitude + "&longitude=" + longitude + "&method=1&school=1";

HUB75_I2S_CFG::i2s_pins _pins = { R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN };
MatrixPanel_I2S_DMA* dma_display = nullptr;

// --- WEB SERVER & PREFERENCES ---
AsyncWebServer server(80);
Preferences preferences;

// --- GLOBAL VARIABLES (State) ---
int displayMode = 0; // 0 = Mosque Mode, 1 = School Mode, 2 = Custom Event Mode
int matrixBrightness = 100;

// School Data
String schoolSubjects[5] = {"Math", "Science", "English", "History", "P.E."};
String schoolTitles[5] = {"Sir", "Mam", "Sir", "Mam", "Sir"}; 
String schoolTeachers[5] = {"Zahid", "Doe", "Bean", "Fox", "Coach"};
String schoolStart[5] = {"08:00", "09:00", "10:00", "11:00", "12:00"};
String schoolEnd[5] = {"08:50", "09:50", "10:50", "11:50", "12:50"};
String colorCurrentHex = "#00FF00"; 
String colorNextHex = "#00FFFF";    

// Mosque Data
bool weatherUpdated = false;
String weather = "LOAD", temperature = "--";
String hijriDate = "SYNCING...";
int prevMin = -1, prevHour = -1, prevSecond = -1;
char* daysList[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

String zikrList[] = {"SubhanAllah", "Alhamdulillah", "Allahu Akbar", "La ilaha illallah"};
int zikrIndex = 0;
int marqueeX = 64; 
unsigned long lastMarqueeUpdate = 0;

struct PrayerTiming { String name; String timeStr; int hour; int minute; };
PrayerTiming prayers[5] = { {"FAJR", "--:--", 0, 0}, {"ZUHR", "--:--", 0, 0}, {"ASR",  "--:--", 0, 0}, {"MGRB", "--:--", 0, 0}, {"ISHA", "--:--", 0, 0} };

String nextPrayerName = "LOAD";
String nextPrayerTime = "--:--";
bool isPrayerTime = false;
bool mosqueGridDrawn = false; 

// --- FULLY CUSTOMIZABLE EVENT DATA ---
String eLine1Txt = "HAPPY"; int eLine1X = 16; int eLine1Y = 12; String eLine1Col = "#87CEEB";
String eLine2Txt = "EVENT"; int eLine2X = 16; int eLine2Y = 26; String eLine2Col = "#800080";
String eLine3Txt = "TODAY!";  int eLine3X = 16; int eLine3Y = 40; String eLine3Col = "#FFFFFF";
String eMarqueeTxt = "Wishing you all the best today! Keep shining!   ";
int eMarqueeX = 64;
unsigned long lastEventMarqueeUpdate = 0;

// Non-Blocking Buzzer State Machine
bool playBirthdayTune = false;
int currentNote = 0;
unsigned long previousNoteTime = 0;
int melody[] = {262, 262, 294, 262, 349, 330, 262, 262, 294, 262, 392, 349, 262, 262, 523, 440, 349, 330, 294, 466, 466, 440, 349, 392, 349};
int noteDurations[] = {200, 200, 400, 400, 400, 800, 200, 200, 400, 400, 400, 800, 200, 200, 400, 400, 400, 400, 400, 200, 200, 400, 400, 400, 800};

// --- HELPER: HEX to RGB565 ---
uint16_t hexToRGB565(String hexStr) {
  if (hexStr.startsWith("#")) hexStr.remove(0, 1);
  long number = strtol(hexStr.c_str(), NULL, 16);
  uint8_t r = (number >> 16) & 0xFF;
  uint8_t g = (number >> 8) & 0xFF;
  uint8_t b = number & 0xFF;
  return dma_display->color565(r, g, b);
}

// --- HTML UI (PROGMEM) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart Matrix Panel</title>
<style>
  body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #121212; color: #ffffff; padding: 20px; max-width: 600px; margin: auto; }
  .card { background-color: #1e1e1e; padding: 20px; border-radius: 12px; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
  h2 { margin-top: 0; color: #4facfe; border-bottom: 1px solid #333; padding-bottom: 10px; }
  label { display: block; margin-top: 10px; font-weight: bold; color: #aaa; font-size: 14px; }
  input, select, button { width: 100%; padding: 10px; margin-top: 5px; border-radius: 6px; border: none; box-sizing: border-box; }
  input[type="text"], input[type="time"], input[type="number"], select { background-color: #2a2a2a; color: #fff; font-size: 16px; }
  input[type="color"] { padding: 0; height: 40px; cursor: pointer; }
  button { background: linear-gradient(to right, #4facfe 0%, #00f2fe 100%); color: white; font-weight: bold; cursor: pointer; margin-top: 15px; font-size: 16px; }
  button:hover { opacity: 0.9; }
  .row { display: flex; gap: 10px; margin-top: 5px; }
  .row input, .row select { flex: 1; }
  .title-select { flex: 0.4 !important; }
  .class-block { background: #252525; padding: 10px; border-radius: 8px; margin-top: 15px; border-left: 4px solid #4facfe; }
  .event-block { background: #252525; padding: 10px; border-radius: 8px; margin-top: 15px; border-left: 4px solid #800080; }
</style>
</head>
<body>

<div class="card">
  <h2>Display Mode</h2>
  <select id="modeSelect" onchange="updateMode()">
    <option value="0">🕌 Mosque Mode</option>
    <option value="1">🏫 School Mode</option>
    <option value="2">🎉 Custom Event Mode</option>
  </select>
</div>

<div class="card">
  <h2>Global Settings</h2>
  <label>Brightness: <span id="brightVal">100</span></label>
  <input type="range" id="brightness" min="0" max="255" value="100" oninput="document.getElementById('brightVal').innerText = this.value; updateSettings();">
  
  <div class="row">
    <div><label>Current Class Color</label><input type="color" id="colCurr" onchange="updateSettings()"></div>
    <div><label>Next Class Color</label><input type="color" id="colNext" onchange="updateSettings()"></div>
  </div>
</div>

<div class="card">
  <h2>Custom Event Layout</h2>
  
  <div class="event-block">
    <label>Line 1 (Text, X Pixel, Y Pixel, Color)</label>
    <div class="row">
      <input type="text" id="el1t" placeholder="Text" style="flex: 2;">
      <input type="number" id="el1x" placeholder="X">
      <input type="number" id="el1y" placeholder="Y">
      <input type="color" id="el1c">
    </div>
  </div>

  <div class="event-block">
    <label>Line 2 (Text, X Pixel, Y Pixel, Color)</label>
    <div class="row">
      <input type="text" id="el2t" placeholder="Text" style="flex: 2;">
      <input type="number" id="el2x" placeholder="X">
      <input type="number" id="el2y" placeholder="Y">
      <input type="color" id="el2c">
    </div>
  </div>

  <div class="event-block">
    <label>Line 3 (Text, X Pixel, Y Pixel, Color)</label>
    <div class="row">
      <input type="text" id="el3t" placeholder="Text" style="flex: 2;">
      <input type="number" id="el3x" placeholder="X">
      <input type="number" id="el3y" placeholder="Y">
      <input type="color" id="el3c">
    </div>
  </div>

  <div class="event-block">
    <label>Scrolling Marquee Message (Bottom)</label>
    <input type="text" id="emq" placeholder="Enter scrolling blessing or message...">
  </div>

  <button onclick="saveEvent()">Save Event Layout</button>
</div>

<div class="card">
  <h2>School Timetable</h2>
  <div id="classesContainer"></div>
  <button onclick="saveTimetable()">Save Timetable</button>
</div>

<script>
  const container = document.getElementById('classesContainer');
  for(let i=0; i<5; i++) {
    container.innerHTML += `
      <div class="class-block">
        <label>Period ${i+1}</label>
        <div class="row">
          <input type="text" id="subj${i}" placeholder="Subject">
        </div>
        <div class="row">
          <select id="title${i}" class="title-select">
            <option value="Sir">Sir</option>
            <option value="Mam">Mam</option>
          </select>
          <input type="text" id="teach${i}" placeholder="First Name (e.g. Zahid)">
        </div>
        <div class="row">
          <input type="time" id="start${i}">
          <input type="time" id="end${i}">
        </div>
      </div>`;
  }

  fetch('/data').then(res => res.json()).then(data => {
    document.getElementById('modeSelect').value = data.mode;
    document.getElementById('brightness').value = data.brightness;
    document.getElementById('brightVal').innerText = data.brightness;
    document.getElementById('colCurr').value = data.cCurr;
    document.getElementById('colNext').value = data.cNext;
    
    // Event Data
    document.getElementById('el1t').value = data.eL1T; document.getElementById('el1x').value = data.eL1X; document.getElementById('el1y').value = data.eL1Y; document.getElementById('el1c').value = data.eL1C;
    document.getElementById('el2t').value = data.eL2T; document.getElementById('el2x').value = data.eL2X; document.getElementById('el2y').value = data.eL2Y; document.getElementById('el2c').value = data.eL2C;
    document.getElementById('el3t').value = data.eL3T; document.getElementById('el3x').value = data.eL3X; document.getElementById('el3y').value = data.eL3Y; document.getElementById('el3c').value = data.eL3C;
    document.getElementById('emq').value = data.eMQ;

    // Timetable Data
    for(let i=0; i<5; i++) {
      document.getElementById(`subj${i}`).value = data.subjects[i];
      document.getElementById(`title${i}`).value = data.titles[i];
      document.getElementById(`teach${i}`).value = data.teachers[i];
      document.getElementById(`start${i}`).value = data.starts[i];
      document.getElementById(`end${i}`).value = data.ends[i];
    }
  });

  function updateMode() {
    let mode = document.getElementById('modeSelect').value;
    fetch(`/setMode?m=${mode}`);
  }

  function updateSettings() {
    let b = document.getElementById('brightness').value;
    let cc = document.getElementById('colCurr').value.replace('#', '');
    let cn = document.getElementById('colNext').value.replace('#', '');
    fetch(`/setSettings?b=${b}&cc=${cc}&cn=${cn}`);
  }

  function saveEvent() {
    let params = new URLSearchParams();
    params.append('t1', document.getElementById('el1t').value); params.append('x1', document.getElementById('el1x').value); params.append('y1', document.getElementById('el1y').value); params.append('c1', document.getElementById('el1c').value.replace('#', ''));
    params.append('t2', document.getElementById('el2t').value); params.append('x2', document.getElementById('el2x').value); params.append('y2', document.getElementById('el2y').value); params.append('c2', document.getElementById('el2c').value.replace('#', ''));
    params.append('t3', document.getElementById('el3t').value); params.append('x3', document.getElementById('el3x').value); params.append('y3', document.getElementById('el3y').value); params.append('c3', document.getElementById('el3c').value.replace('#', ''));
    params.append('mq', document.getElementById('emq').value);
    
    fetch(`/setEvent?${params.toString()}`).then(() => alert("Event Customization Saved!"));
  }

  function saveTimetable() {
    let params = new URLSearchParams();
    for(let i=0; i<5; i++) {
      params.append(`s${i}`, document.getElementById(`subj${i}`).value);
      params.append(`tl${i}`, document.getElementById(`title${i}`).value);
      params.append(`t${i}`, document.getElementById(`teach${i}`).value);
      params.append(`st${i}`, document.getElementById(`start${i}`).value);
      params.append(`en${i}`, document.getElementById(`end${i}`).value);
    }
    fetch(`/setClasses?${params.toString()}`).then(() => alert("Timetable Saved Successfully!"));
  }
</script>
</body>
</html>
)rawliteral";

// --- MULTICOLOR GENERATOR ---
uint16_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) return dma_display->color565(255 - WheelPos * 3, 0, WheelPos * 3);
  if(WheelPos < 170) { WheelPos -= 85; return dma_display->color565(0, WheelPos * 3, 255 - WheelPos * 3); }
  WheelPos -= 170; return dma_display->color565(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void loadPreferences() {
  preferences.begin("matrix", false);
  displayMode = preferences.getInt("mode", 0);
  matrixBrightness = preferences.getInt("bright", 100);
  colorCurrentHex = preferences.getString("cCurr", "#00FF00");
  colorNextHex = preferences.getString("cNext", "#00FFFF");

  // Load Event Params
  eLine1Txt = preferences.getString("eL1T", eLine1Txt); eLine1X = preferences.getInt("eL1X", eLine1X); eLine1Y = preferences.getInt("eL1Y", eLine1Y); eLine1Col = preferences.getString("eL1C", eLine1Col);
  eLine2Txt = preferences.getString("eL2T", eLine2Txt); eLine2X = preferences.getInt("eL2X", eLine2X); eLine2Y = preferences.getInt("eL2Y", eLine2Y); eLine2Col = preferences.getString("eL2C", eLine2Col);
  eLine3Txt = preferences.getString("eL3T", eLine3Txt); eLine3X = preferences.getInt("eL3X", eLine3X); eLine3Y = preferences.getInt("eL3Y", eLine3Y); eLine3Col = preferences.getString("eL3C", eLine3Col);
  eMarqueeTxt = preferences.getString("eMQ", eMarqueeTxt);

  // Load School Params
  for(int i=0; i<5; i++) {
    schoolSubjects[i] = preferences.getString(("subj"+String(i)).c_str(), schoolSubjects[i]);
    schoolTitles[i] = preferences.getString(("titl"+String(i)).c_str(), schoolTitles[i]);
    schoolTeachers[i] = preferences.getString(("teach"+String(i)).c_str(), schoolTeachers[i]);
    schoolStart[i] = preferences.getString(("st"+String(i)).c_str(), schoolStart[i]);
    schoolEnd[i] = preferences.getString(("en"+String(i)).c_str(), schoolEnd[i]);
  }
  preferences.end();
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"mode\":" + String(displayMode) + ",";
    json += "\"brightness\":" + String(matrixBrightness) + ",";
    json += "\"cCurr\":\"" + colorCurrentHex + "\",";
    json += "\"cNext\":\"" + colorNextHex + "\",";
    
    json += "\"eL1T\":\"" + eLine1Txt + "\",\"eL1X\":" + String(eLine1X) + ",\"eL1Y\":" + String(eLine1Y) + ",\"eL1C\":\"" + eLine1Col + "\",";
    json += "\"eL2T\":\"" + eLine2Txt + "\",\"eL2X\":" + String(eLine2X) + ",\"eL2Y\":" + String(eLine2Y) + ",\"eL2C\":\"" + eLine2Col + "\",";
    json += "\"eL3T\":\"" + eLine3Txt + "\",\"eL3X\":" + String(eLine3X) + ",\"eL3Y\":" + String(eLine3Y) + ",\"eL3C\":\"" + eLine3Col + "\",";
    json += "\"eMQ\":\"" + eMarqueeTxt + "\",";

    json += "\"subjects\":[";
    for(int i=0; i<5; i++) json += "\"" + schoolSubjects[i] + "\"" + (i<4?",":"");
    json += "],\"titles\":[";
    for(int i=0; i<5; i++) json += "\"" + schoolTitles[i] + "\"" + (i<4?",":"");
    json += "],\"teachers\":[";
    for(int i=0; i<5; i++) json += "\"" + schoolTeachers[i] + "\"" + (i<4?",":"");
    json += "],\"starts\":[";
    for(int i=0; i<5; i++) json += "\"" + schoolStart[i] + "\"" + (i<4?",":"");
    json += "],\"ends\":[";
    for(int i=0; i<5; i++) json += "\"" + schoolEnd[i] + "\"" + (i<4?",":"");
    json += "]}";
    request->send(200, "application/json", json);
  });

  server.on("/setMode", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("m")) {
      int newMode = request->getParam("m")->value().toInt();
      
      // Trigger buzzer ONLY when switching TO Custom Event Mode
      if (newMode == 2 && displayMode != 2) {
         playBirthdayTune = true;
         currentNote = 0;
         previousNoteTime = millis();
         tone(BUZZER_PIN, melody[0], noteDurations[0]);
      } else if (newMode != 2) {
         playBirthdayTune = false;
         noTone(BUZZER_PIN);
      }

      displayMode = newMode;
      preferences.begin("matrix", false);
      preferences.putInt("mode", displayMode);
      preferences.end();
      dma_display->clearScreen(); 
      mosqueGridDrawn = false; 
      prevSecond = -1; 
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/setSettings", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("b")) matrixBrightness = request->getParam("b")->value().toInt();
    if(request->hasParam("cc")) colorCurrentHex = "#" + request->getParam("cc")->value();
    if(request->hasParam("cn")) colorNextHex = "#" + request->getParam("cn")->value();
    
    dma_display->setBrightness8(matrixBrightness);
    preferences.begin("matrix", false);
    preferences.putInt("bright", matrixBrightness);
    preferences.putString("cCurr", colorCurrentHex);
    preferences.putString("cNext", colorNextHex);
    preferences.end();
    request->send(200, "text/plain", "OK");
  });

  server.on("/setEvent", HTTP_GET, [](AsyncWebServerRequest *request){
    preferences.begin("matrix", false);
    if(request->hasParam("t1")) eLine1Txt = request->getParam("t1")->value();
    if(request->hasParam("x1")) eLine1X = request->getParam("x1")->value().toInt();
    if(request->hasParam("y1")) eLine1Y = request->getParam("y1")->value().toInt();
    if(request->hasParam("c1")) eLine1Col = "#" + request->getParam("c1")->value();
    
    if(request->hasParam("t2")) eLine2Txt = request->getParam("t2")->value();
    if(request->hasParam("x2")) eLine2X = request->getParam("x2")->value().toInt();
    if(request->hasParam("y2")) eLine2Y = request->getParam("y2")->value().toInt();
    if(request->hasParam("c2")) eLine2Col = "#" + request->getParam("c2")->value();

    if(request->hasParam("t3")) eLine3Txt = request->getParam("t3")->value();
    if(request->hasParam("x3")) eLine3X = request->getParam("x3")->value().toInt();
    if(request->hasParam("y3")) eLine3Y = request->getParam("y3")->value().toInt();
    if(request->hasParam("c3")) eLine3Col = "#" + request->getParam("c3")->value();

    if(request->hasParam("mq")) eMarqueeTxt = request->getParam("mq")->value();

    preferences.putString("eL1T", eLine1Txt); preferences.putInt("eL1X", eLine1X); preferences.putInt("eL1Y", eLine1Y); preferences.putString("eL1C", eLine1Col);
    preferences.putString("eL2T", eLine2Txt); preferences.putInt("eL2X", eLine2X); preferences.putInt("eL2Y", eLine2Y); preferences.putString("eL2C", eLine2Col);
    preferences.putString("eL3T", eLine3Txt); preferences.putInt("eL3X", eLine3X); preferences.putInt("eL3Y", eLine3Y); preferences.putString("eL3C", eLine3Col);
    preferences.putString("eMQ", eMarqueeTxt);
    preferences.end();
    
    dma_display->clearScreen();
    request->send(200, "text/plain", "OK");
  });

  server.on("/setClasses", HTTP_GET, [](AsyncWebServerRequest *request){
    preferences.begin("matrix", false);
    for(int i=0; i<5; i++) {
      if(request->hasParam("s"+String(i))) schoolSubjects[i] = request->getParam("s"+String(i))->value();
      if(request->hasParam("tl"+String(i))) schoolTitles[i] = request->getParam("tl"+String(i))->value();
      if(request->hasParam("t"+String(i))) schoolTeachers[i] = request->getParam("t"+String(i))->value();
      if(request->hasParam("st"+String(i))) schoolStart[i] = request->getParam("st"+String(i))->value();
      if(request->hasParam("en"+String(i))) schoolEnd[i] = request->getParam("en"+String(i))->value();
      
      preferences.putString(("subj"+String(i)).c_str(), schoolSubjects[i]);
      preferences.putString(("titl"+String(i)).c_str(), schoolTitles[i]);
      preferences.putString(("teach"+String(i)).c_str(), schoolTeachers[i]);
      preferences.putString(("st"+String(i)).c_str(), schoolStart[i]);
      preferences.putString(("en"+String(i)).c_str(), schoolEnd[i]);
    }
    preferences.end();
    dma_display->clearScreen();
    mosqueGridDrawn = false;
    prevSecond = -1;
    request->send(200, "text/plain", "OK");
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);

  loadPreferences();

  HUB75_I2S_CFG mxconfig(64, 64, 1, _pins);
  mxconfig.latch_blanking = 4;
  mxconfig.clkphase = false;   
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();             
  dma_display->setBrightness8(matrixBrightness);  
  dma_display->setTextWrap(false);  
  dma_display->clearScreen();

  dma_display->setTextColor(dma_display->color444(15, 15, 0));
  dma_display->setCursor(0, 20);
  dma_display->println("Connecting WiFi");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    dma_display->print(".");
    Serial.print(".");
  }
  
  dma_display->clearScreen();
  dma_display->setCursor(0, 10);
  dma_display->setTextColor(dma_display->color565(0, 255, 0));
  dma_display->println("WiFi Connected!");
  dma_display->println("Panel IP:");
  dma_display->setTextColor(dma_display->color565(0, 255, 255));
  dma_display->println(WiFi.localIP()); 
  Serial.println("\nIP Address for Web Panel: ");
  Serial.println(WiFi.localIP());       
  
  delay(4000); 
  dma_display->clearScreen();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  setupWebServer();
  
  fetchWeather();
  fetchPrayers();
}

void loop() {
  delay(30); 
  
  // Non-Blocking Buzzer Handler
  if (playBirthdayTune) {
    if (millis() - previousNoteTime > noteDurations[currentNote] * 1.3) {
      currentNote++;
      if (currentNote < 25) {
        tone(BUZZER_PIN, melody[currentNote], noteDurations[currentNote]);
        previousNoteTime = millis();
      } else {
        playBirthdayTune = false;
        noTone(BUZZER_PIN);
      }
    }
  }

  // Display Mode Handler
  if (displayMode == 0) {
    printMosqueMode();
  } else if (displayMode == 1) {
    printSchoolMode();
  } else {
    printEventMode(); // Now Fully Custom!
  }
}

// --- SECURE WEATHER FETCH ---
void fetchWeather() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure *client = new WiFiClientSecure;
    if(client) {
      client->setInsecure(); 
      HTTPClient http;
      http.setTimeout(8000); 
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.begin(*client, weatherServerName);
      if (http.GET() == 200) {
        DynamicJsonDocument doc(1024); 
        if (!deserializeJson(doc, http.getStream())) {
          weather = doc["weather"][0]["main"].as<String>();  
          weather.toUpperCase();
          temperature = String(int(doc["main"]["temp"].as<double>()));
          weatherUpdated = true;
        }
      }
      http.end();
      delete client;
    }
  }
}

// --- MEMORY-FILTERED PRAYER FETCH ---
void fetchPrayers() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure *client = new WiFiClientSecure;
    if(client) {
      client->setInsecure(); 
      HTTPClient http;
      http.setTimeout(15000); 
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.begin(*client, prayerServerName);
      if (http.GET() == 200) { 
        StaticJsonDocument<256> filter;
        filter["data"]["timings"] = true;
        filter["data"]["date"]["hijri"]["day"] = true;
        filter["data"]["date"]["hijri"]["month"]["en"] = true;
        DynamicJsonDocument doc(1024); 
        if (!deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) {
          JsonObject timings = doc["data"]["timings"];
          setPrayerTime(0, timings["Fajr"].as<String>());
          setPrayerTime(1, timings["Dhuhr"].as<String>());
          setPrayerTime(2, timings["Asr"].as<String>());
          setPrayerTime(3, timings["Maghrib"].as<String>());
          setPrayerTime(4, timings["Isha"].as<String>());
          String hDay = doc["data"]["date"]["hijri"]["day"].as<String>();
          String hMonth = doc["data"]["date"]["hijri"]["month"]["en"].as<String>();
          if(hDay != "null" && hMonth != "null" && hDay.length() > 0) {
             hijriDate = hDay + " " + hMonth;
             hijriDate.toUpperCase();
          } else { hijriDate = "SYNC ERR"; }
          struct tm timeinfo;
          if (getLocalTime(&timeinfo)) calculateNextPrayer(timeinfo.tm_hour, timeinfo.tm_min);
        }
      }
      http.end();
      client->stop(); delete client; 
    }
  }
}

void setPrayerTime(int index, String timeStr) {
  if (timeStr == "null" || timeStr.length() < 5) return; 
  prayers[index].timeStr = timeStr.substring(0, 5); 
  prayers[index].hour = timeStr.substring(0, 2).toInt();
  prayers[index].minute = timeStr.substring(3, 5).toInt();
}

void calculateNextPrayer(int currHour, int currMin) {
  int currentMins = currHour * 60 + currMin;
  isPrayerTime = false;
  nextPrayerName = prayers[0].name;
  nextPrayerTime = prayers[0].timeStr;
  for (int i = 0; i < 5; i++) {
    int prayerMins = prayers[i].hour * 60 + prayers[i].minute;
    if (currentMins >= prayerMins && currentMins < (prayerMins + 15)) {
      isPrayerTime = true; nextPrayerName = prayers[i].name; nextPrayerTime = prayers[i].timeStr; return;
    }
    if (currentMins < prayerMins) {
      nextPrayerName = prayers[i].name; nextPrayerTime = prayers[i].timeStr; return;
    }
  }
}

// ==========================================
// 1. ORIGINAL MOSQUE MODE
// ==========================================
void printMosqueMode() {
  struct tm timeinfo;
  
  if (!getLocalTime(&timeinfo)) {
    if(!mosqueGridDrawn) {
      dma_display->clearScreen();
      dma_display->setCursor(0, 20);
      dma_display->setTextColor(dma_display->color565(255, 0, 0));
      dma_display->print("WAITING FOR");
      dma_display->setCursor(0, 30);
      dma_display->print("INTERNET TIME");
      mosqueGridDrawn = true;
    }
    return;
  }

  if (!mosqueGridDrawn) {
    dma_display->clearScreen();
    uint16_t lineColor = dma_display->color565(40, 40, 40); 
    dma_display->drawFastHLine(0, 7, 64, lineColor);
    dma_display->drawFastHLine(0, 24, 64, lineColor);
    dma_display->drawFastHLine(0, 34, 64, lineColor);
    dma_display->drawFastHLine(0, 44, 64, lineColor);
    dma_display->drawFastHLine(0, 54, 64, lineColor);
    mosqueGridDrawn = true;
    prevMin = -1; 
  }

  uint16_t blank = dma_display->color444(0, 0, 0); 
  uint16_t colHijri   = dma_display->color565(0, 255, 255);   
  uint16_t colTime    = dma_display->color565(0, 255, 0);     
  uint16_t colWeather = dma_display->color565(0, 100, 255);   
  uint16_t colPrayer  = dma_display->color565(255, 0, 0);     
  uint16_t colDate    = dma_display->color565(255, 255, 0);   

  if (timeinfo.tm_min == 0 && timeinfo.tm_sec == 0 && prevSecond != timeinfo.tm_sec) {
     fetchWeather(); fetchPrayers();
  }

  if (prevMin != timeinfo.tm_min) {
    prevMin = timeinfo.tm_min;
    calculateNextPrayer(timeinfo.tm_hour, timeinfo.tm_min);

    dma_display->fillRect(0, 8, 48, 15, blank);
    dma_display->setFont(&FreeSansBold9pt7b);
    dma_display->setTextColor(colTime);
    dma_display->setCursor(1, 22);
    dma_display->print(&timeinfo, "%I:%M");
  }

  if (prevSecond != timeinfo.tm_sec || weatherUpdated) {
    dma_display->fillRect(0, 0, 64, 7, blank);
    dma_display->setFont();
    dma_display->setTextColor(colHijri);
    dma_display->setCursor(2, 0);
    dma_display->print(hijriDate);

    dma_display->setFont(&TTFont);
    dma_display->fillRect(49, 9, 15, 14, blank);
    dma_display->setTextColor(colTime);
    dma_display->setCursor(50, 14);
    if (timeinfo.tm_hour < 12) dma_display->print("AM"); else dma_display->print("PM");
    dma_display->setFont();
    dma_display->setCursor(51, 16);
    dma_display->print(&timeinfo, "%S");

    dma_display->fillRect(0, 25, 64, 8, blank);
    dma_display->setFont(&TTFont);
    dma_display->setTextColor(colWeather);
    dma_display->setCursor(0, 31);
    dma_display->print(weather);
    dma_display->setCursor(45, 31);
    dma_display->print(temperature);
    dma_display->print("C");
    weatherUpdated = false;

    dma_display->fillRect(0, 35, 64, 8, blank);
    dma_display->setFont(); 
    dma_display->setCursor(2, 36);
    dma_display->setTextColor(colPrayer);
    if (isPrayerTime) {
      dma_display->print("NOW: "); dma_display->print(nextPrayerName);
    } else {
      dma_display->print(nextPrayerName); dma_display->print(" "); dma_display->print(nextPrayerTime);
    }

    dma_display->fillRect(0, 45, 64, 8, blank);
    dma_display->setFont();
    dma_display->setTextColor(colDate);
    dma_display->setCursor(2, 46);
    dma_display->print(daysList[timeinfo.tm_wday]);
    dma_display->setCursor(48, 46);
    dma_display->print(&timeinfo, "%d"); 

    prevSecond = timeinfo.tm_sec;
  }

  if (millis() - lastMarqueeUpdate > 40) { 
    marqueeX--;
    int textLengthPixelBoundary = (zikrList[zikrIndex].length() * 6); 
    if (marqueeX < -textLengthPixelBoundary) { marqueeX = 64; zikrIndex = (zikrIndex + 1) % 4; }
    lastMarqueeUpdate = millis();
  }

  dma_display->fillRect(0, 55, 64, 8, blank);
  dma_display->setFont();
  int cursorX = marqueeX;
  for (int i = 0; i < zikrList[zikrIndex].length(); i++) {
    char c = zikrList[zikrIndex][i];
    uint8_t colorIndex = (cursorX * 6) % 256; 
    uint16_t spatialColor = Wheel(colorIndex);
    if (cursorX >= -6 && cursorX < 64) {
      dma_display->setTextColor(spatialColor);
      dma_display->setCursor(cursorX, 56);
      dma_display->print(c);
    }
    cursorX += 6; 
  }
}

// ==========================================
// 2. SCHOOL MODE 
// ==========================================
void printSchoolMode() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  uint16_t blank = dma_display->color444(0, 0, 0); 
  uint16_t colTime = dma_display->color565(255, 255, 255); 
  uint16_t colCurrent = hexToRGB565(colorCurrentHex);      
  uint16_t colNext = hexToRGB565(colorNextHex);            

  if (prevSecond != timeinfo.tm_sec) {
    dma_display->fillRect(0, 0, 64, 64, blank); 
    dma_display->setFont();

    dma_display->setTextColor(colTime);
    dma_display->setCursor(6, 2);
    dma_display->print(&timeinfo, "%I:%M %p");
    dma_display->drawFastHLine(0, 11, 64, dma_display->color565(40, 40, 40));

    int currentMins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int activeClassIndex = -1;
    int nextClassIndex = -1;

    for(int i = 0; i < 5; i++) {
      int startMins = schoolStart[i].substring(0, 2).toInt() * 60 + schoolStart[i].substring(3, 5).toInt();
      int endMins = schoolEnd[i].substring(0, 2).toInt() * 60 + schoolEnd[i].substring(3, 5).toInt();
      
      if (currentMins >= startMins && currentMins < endMins) activeClassIndex = i;
      if (currentMins < startMins && nextClassIndex == -1) nextClassIndex = i; 
    }

    if (activeClassIndex != -1) {
      dma_display->setTextColor(colCurrent);
      dma_display->setCursor(0, 16);
      dma_display->print("NOW: ");
      dma_display->print(schoolSubjects[activeClassIndex]);
      
      dma_display->setCursor(0, 26);
      dma_display->print(schoolTitles[activeClassIndex]);
      dma_display->print(" ");
      dma_display->print(schoolTeachers[activeClassIndex]);
    } else {
      dma_display->setTextColor(dma_display->color565(100, 100, 100));
      dma_display->setCursor(0, 20);
      dma_display->print("-- NO CLASS --");
    }

    dma_display->drawFastHLine(0, 36, 64, dma_display->color565(40, 40, 40));

    if (nextClassIndex != -1) {
      dma_display->setTextColor(colNext);
      dma_display->setCursor(0, 42);
      dma_display->print("NXT: ");
      dma_display->print(schoolSubjects[nextClassIndex]);

      dma_display->setCursor(0, 52);
      dma_display->print(schoolTitles[nextClassIndex]);
      dma_display->print(" ");
      dma_display->print(schoolTeachers[nextClassIndex]);
    } else {
      dma_display->setTextColor(dma_display->color565(100, 100, 100));
      dma_display->setCursor(14, 46);
      dma_display->print("DONE FOR");
      dma_display->setCursor(18, 56);
      dma_display->print("THE DAY");
    }

    prevSecond = timeinfo.tm_sec;
  }
}

// ==========================================
// 3. FULLY CUSTOM EVENT MODE
// ==========================================
void printEventMode() {
  uint16_t blank = dma_display->color444(0, 0, 0); 
  dma_display->fillRect(0, 0, 64, 52, blank); // Clear upper section leaving marquee space
  dma_display->setFont();

  // Dynamic Multicolor Running Border
  int borderShift = (millis() / 30) % 256;
  for (int i = 0; i < 64; i++) {
     uint16_t borderColor = Wheel((i * 4 + borderShift) % 256);
     dma_display->drawPixel(i, 0, borderColor); // Top
     dma_display->drawPixel(i, 63, borderColor); // Bottom
     dma_display->drawPixel(0, i, borderColor); // Left
     dma_display->drawPixel(63, i, borderColor); // Right
  }

  // Draw Line 1
  dma_display->setTextColor(hexToRGB565(eLine1Col));
  dma_display->setCursor(eLine1X, eLine1Y);
  dma_display->print(eLine1Txt);

  // Draw Line 2
  dma_display->setTextColor(hexToRGB565(eLine2Col));
  dma_display->setCursor(eLine2X, eLine2Y);
  dma_display->print(eLine2Txt);

  // Draw Line 3
  dma_display->setTextColor(hexToRGB565(eLine3Col));
  dma_display->setCursor(eLine3X, eLine3Y);
  dma_display->print(eLine3Txt);

  // Marquee Engine (Scrolls continuously at bottom)
  if (millis() - lastEventMarqueeUpdate > 40) { 
    eMarqueeX--;
    int textLengthPixelBoundary = (eMarqueeTxt.length() * 6); 
    if (eMarqueeX < -textLengthPixelBoundary) { 
      eMarqueeX = 64; 
    }
    lastEventMarqueeUpdate = millis();
  }

  dma_display->fillRect(1, 54, 62, 9, blank); // Clear inner marquee track
  int cursorX = eMarqueeX;
  for (int i = 0; i < eMarqueeTxt.length(); i++) {
    char c = eMarqueeTxt[i];
    uint8_t colorIndex = (cursorX * 6) % 256; 
    uint16_t spatialColor = Wheel(colorIndex);
    
    // Confine to inside the border (1 to 62)
    if (cursorX >= 1 && cursorX < 63) {
      dma_display->setTextColor(spatialColor);
      dma_display->setCursor(cursorX, 55);
      dma_display->print(c);
    }
    cursorX += 6; 
  }
}