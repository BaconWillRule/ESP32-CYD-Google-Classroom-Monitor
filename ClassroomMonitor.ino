
#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "YOUR_TEMPLATE_NAME"
#define BLYNK_AUTH_TOKEN "YOUR_AUTH_TOKEN"
#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <TFT_eSPI.h> 
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "time.h" // Native ESP32 Time
char ssid[] = "YOUR_WIFI_SSID"; 
char pass[] = "YOUR_WIFI_PASSWORD"; 
// --- TIME SETTINGS (UK/London) ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;
// --- 1. THE TOUCH SETUP ---
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
bool USE_SOFTWARE_SPI = true; 
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS); 
// --- 2. THE SCREEN SETUP ---
TFT_eSPI tft = TFT_eSPI(); 
// --- PALETTE ---
#define C_BG        0x0000 // Black
#define C_RED       0xF800 // Pure Red
#define C_WHITE     0xFFFF
#define C_GREY      0x4208 // Dark Grey
#define C_YELLOW    0xFFE0
#define C_CYAN      0x07FF
#define C_GREEN     0x07E0 
// Custom Gradient Shades for Bleed
#define C_RED_1     0xF800 
#define C_RED_2     0xB000
#define C_RED_3     0x7800
#define C_RED_4     0x4000
// --- VARIABLES ---
int missing = 0; int assigned = 0; int done = 0;
String taskName = "Syncing..."; String listData = ""; 
String classData = ""; // V5: "Math:2|English:0"
int page = 0; // 0:Dash, 1:List, 2:Schedule, 3:Breakdown, 4:Standby
unsigned long lastTap = 0;
bool updateNeeded = true;
// --- 3. SUBJECT CONFIGURATION ---
struct SubjectTarget {
  String displayName; // "Biology"
  String searchKey;   // "BIO" (Finds "13B/BIO")
};
// DEFINE MONITORED SUBJECTS HERE
SubjectTarget monitoredSubjects[] = {
  {"Biology",   "BIO"},
  {"Geography", "GEO"},
  {"CompSci",   "CS1"} // Matches [13A/CS1]
};
int monitoredCount = 3;
// Matrix Rain Variables
int drops[10]; 
unsigned long lastFrame = 0;
// --- SMART SCHEDULE DATA ---
struct Lesson {
  String name;
  String teacher;
  String room;
  int startH;
  int startM;
};
// 5 Days, Max 6 Lessons per day
Lesson timetable[5][6]; 
BLYNK_WRITE(V0) { missing = param.asInt(); updateNeeded = true; }
BLYNK_WRITE(V1) { assigned = param.asInt(); updateNeeded = true; }
BLYNK_WRITE(V2) { done = param.asInt(); updateNeeded = true; }
BLYNK_WRITE(V3) { taskName = param.asStr(); updateNeeded = true; }
BLYNK_WRITE(V4) { listData = param.asStr(); updateNeeded = true; }
BLYNK_WRITE(V5) { 
  classData = param.asStr(); 
  updateNeeded = true; 
  Serial.print("RAW CLASS DATA: ");
  Serial.println(classData); // DEBUG
} 
// --- BIT BANG SPI FUNCTIONS ---
void softwareSpiInit() {
  pinMode(XPT2046_CLK, OUTPUT);
  pinMode(XPT2046_MOSI, OUTPUT);
  pinMode(XPT2046_CS, OUTPUT);
  pinMode(XPT2046_MISO, INPUT);
  
  digitalWrite(XPT2046_CS, HIGH);
  digitalWrite(XPT2046_CLK, LOW);
  digitalWrite(XPT2046_MOSI, LOW);
}
uint8_t softwareSpiTransfer(uint8_t data) {
  uint8_t result = 0;
  for (int i = 0; i < 8; i++) {
    digitalWrite(XPT2046_MOSI, (data & 0x80) ? HIGH : LOW);
    data <<= 1;
    digitalWrite(XPT2046_CLK, HIGH); 
    result <<= 1;
    if (digitalRead(XPT2046_MISO)) result |= 1;
    digitalWrite(XPT2046_CLK, LOW);
  }
  return result;
}
int readTouchChannel(uint8_t cmd) {
  digitalWrite(XPT2046_CS, LOW);
  softwareSpiTransfer(cmd);
  uint8_t h = softwareSpiTransfer(0x00);
  uint8_t l = softwareSpiTransfer(0x00);
  digitalWrite(XPT2046_CS, HIGH);
  return ((h << 8) | l) >> 3;
}
// --- HELPER: Get Aggregated Missing Count ---
int getMissingForSubject(String searchKey) {
  if (classData.length() == 0) return 0;
  int totalMissing = 0;
  
  String remaining = classData;
  while(remaining.length() > 0) {
    int split = remaining.indexOf('|');
    String item = (split == -1) ? remaining : remaining.substring(0, split);
    remaining = (split == -1) ? "" : remaining.substring(split + 1);
    
    int sep = item.indexOf(':');
    if(sep != -1) {
      String cName = item.substring(0, sep);
      int cCount = item.substring(sep+1).toInt();
      
      // If the class name contains our search key (e.g. "13A/CS1" contains "CS1")
      if (cName.indexOf(searchKey) != -1) {
        totalMissing += cCount;
      }
    }
  }
  return totalMissing;
}
// --- HELPER: Get Short Code ---
String getShortCode(String name) {
  if (name == "Biology") return "BIO";
  if (name == "Geography") return "GEO";
  if (name == "CompSci") return "CS";
  if (name == "Indep. Learning") return "IL";
  if (name == "Core RE") return "RE";
  if (name == "Assembly") return "ASM";
  if (name == "FREE" || name == "Finished / Free Period") return "";
  return name.substring(0, 3); // Fallback
}
// --- PAGE 2: WEEKLY GRID ---
void drawWeeklyGrid() {
  tft.fillScreen(C_BG);
  
  // Grid Config
  int colW = 64; // 320 / 5
  int rowH = 35; // 240 / 7 (approx)
  int startY = 30;
  
  // Draw Headers
  tft.fillRect(0, 0, 320, 25, C_WHITE);
  tft.setTextFont(2);
  tft.setTextColor(C_BG, C_WHITE);
  tft.setTextDatum(MC_DATUM);
  
  const char* days[] = {"MON", "TUE", "WED", "THU", "FRI"};
  for(int d=0; d<5; d++) {
    tft.drawString(days[d], (d * colW) + (colW/2), 12);
  }
  // Draw Grid
  for(int d=0; d<5; d++) {
    for(int l=0; l<6; l++) {
      Lesson lesson = timetable[d][l];
      String code = getShortCode(lesson.name);
      
      int x = d * colW;
      int y = startY + (l * rowH);
      
      // Determine Color
      uint16_t cellColor = C_BG;
      uint16_t textColor = C_WHITE;
      
      if (code == "") {
        // FREE PERIOD
        textColor = C_GREY;
      } else {
        // MAP TIMETABLE NAME TO SEARCH KEY
        String key = "";
        if (lesson.name == "Biology") key = "BIO";
        else if (lesson.name == "Geography") key = "GEO";
        else if (lesson.name == "CompSci") key = "CS1";
        
        int mCount = 0;
        if (key != "") mCount = getMissingForSubject(key);
        
        if (mCount > 0) {
          cellColor = C_RED;
          textColor = C_WHITE;
        } else {
          cellColor = C_BG; // Default Black
          textColor = C_CYAN;
        }
      }
      // Fill Cell
      tft.fillRect(x, y, colW, rowH, cellColor);
      tft.drawRect(x, y, colW, rowH, C_GREY); // Grid Lines
      
      // Draw Text
      tft.setTextColor(textColor, cellColor);
      tft.setTextFont(2);
      tft.drawString(code, x + (colW/2), y + (rowH/2));
    }
  }
}
// --- PAGE 3: BREAKDOWN  ---
void drawBreakdown() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 35, C_RED); 
  tft.setTextFont(4);
  tft.setTextColor(C_WHITE, C_RED); 
  tft.setTextDatum(ML_DATUM);
  tft.drawString("BREAKDOWN", 10, 18); 
  
  // CHECK FOR NO DATA
  if (classData.length() == 0) {
    tft.setTextFont(4);
    tft.setTextColor(C_GREY, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("NO DATA RECEIVED", 160, 100);
    tft.setTextFont(2);
    tft.drawString("Check Google Script & Blynk V5", 160, 130);
    return;
  }
  
  int y = 50;
  
  // ITERATE MONITORED SUBJECTS ONLY
  for (int i = 0; i < monitoredCount; i++) {
    SubjectTarget s = monitoredSubjects[i];
    int totalMissing = getMissingForSubject(s.searchKey);
    
    // CALCULATE INTEGRITY
    // Each missing task = 20% Damage
    int damage = totalMissing * 20;
    int integrity = 100 - damage;
    if (integrity < 0) integrity = 0;
    
    // DETERMINE STATUS & COLOR
    uint16_t barColor = C_GREEN;
    String status = "STABLE";
    
    if (integrity < 100 && integrity >= 40) {
      barColor = C_YELLOW;
      status = "CAUTION";
    } else if (integrity < 40) {
      barColor = C_RED;
      status = "CRITICAL";
    }
    
    // Draw Label (Subject Name)
    tft.setTextFont(2);
    tft.setTextColor(C_WHITE, C_BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(s.displayName, 10, y+10);
    
    // Draw Health Bar Background
    tft.fillRect(100, y, 200, 20, C_GREY);
    
    // Draw Health Bar Foreground
    int barW = integrity * 2; // 100% * 2 = 200px width
    if (barW > 0) tft.fillRect(100, y, barW, 20, barColor);
    
    // Draw Text Overlay (e.g. "100% [STABLE]")
    tft.setTextFont(2);
    tft.setTextColor(C_WHITE, C_BG); // Reset to safe default
    String statusText = String(integrity) + "% [" + status + "]";
    tft.drawString(statusText, 200, y+10); 
    
    y += 40; 
  }
}
// --- SETUP SCHEDULE ---
void setupSchedule() {
  // MONDAY
  timetable[0][0] = {"Biology", "Miss Berlin", "SC9", 8, 30};
  timetable[0][1] = {"Indep. Learning", "Miss Hampster", "Hall", 9, 25};
  timetable[0][2] = {"Geography", "Miss Soggy", "H10", 10, 55};
  timetable[0][3] = {"Geography", "Miss Soggy", "H10", 11, 50};
  timetable[0][4] = {"Indep. Learning", "Miss Hampster", "Hall", 13, 25};
  timetable[0][5] = {"Indep. Learning", "Miss Hampster", "Hall", 14, 20};
  // TUESDAY
  timetable[1][0] = {"FREE", "", "", 8, 30};
  timetable[1][1] = {"Assembly", "", "Main Hall", 9, 25};
  timetable[1][2] = {"CompSci", "Mr Kraków", "IT3", 10, 55};
  timetable[1][3] = {"Indep. Learning", "Miss Hampster", "Hall", 11, 50};
  timetable[1][4] = {"Biology", "Miss Berlin", "SC9", 13, 25};
  timetable[1][5] = {"Biology", "Miss Berlin", "SC9", 14, 20};
  // WEDNESDAY
  timetable[2][0] = {"Geography", "Mr Sheffield", "H10", 8, 30};
  timetable[2][1] = {"Indep. Learning", "Miss Hampster", "Hall", 9, 25};
  timetable[2][2] = {"Core RE", "Mr Mag", "H8", 10, 55};
  timetable[2][3] = {"Indep. Learning", "Miss Hampster", "Hall", 11, 50};
  timetable[2][4] = {"FREE", "", "", 13, 25};
  timetable[2][5] = {"FREE", "", "", 14, 20};
  // THURSDAY
  timetable[3][0] = {"CompSci", "Mr Kraków", "IT3", 8, 30};
  timetable[3][1] = {"CompSci", "Mr Kraków", "IT3", 9, 25};
  timetable[3][2] = {"Biology", "Miss Bagel", "SC10", 10, 55};
  timetable[3][3] = {"Biology", "Miss Bagel", "SC10", 11, 50};
  timetable[3][4] = {"Geography", "Mr Sheffield", "SEM3", 13, 25};
  timetable[3][5] = {"Geography", "Miss Soggy", "SEM3", 14, 20};
  // FRIDAY
  timetable[4][0] = {"Indep. Learning", "Miss Hampster", "Hall", 8, 30};
  timetable[4][1] = {"Indep. Learning", "Miss Hampster", "Hall", 9, 25};
  timetable[4][2] = {"CompSci", "Mr Kraków", "IT3", 10, 55};
  timetable[4][3] = {"CompSci", "Mr Kraków", "IT3", 11, 50};
  timetable[4][4] = {"FREE", "", "", 13, 25};
  timetable[4][5] = {"FREE", "", "", 14, 20};
}
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n--- BOOTING SMART MONITOR ---");
  
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  if (USE_SOFTWARE_SPI) {
    softwareSpiInit();
  } else {
    touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSpi);
    ts.setRotation(1);
  }
  tft.init();
  tft.setRotation(1); 
  tft.invertDisplay(1); 
  tft.fillScreen(C_BG);
  
  for(int i=0; i<10; i++) drops[i] = random(0, 240);
  
  setupSchedule(); 
  tft.setTextFont(4);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SYSTEM BOOT...", 160, 120);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  // INIT TIME
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  Blynk.syncAll();
}
void loop() {
  Blynk.run();
  // --- CHILL MODE ANIMATION ---
  if (page == 0 && missing == 0) {
    unsigned long now = millis();
    if (now - lastFrame > 50) { 
      drawMatrixRain();
      lastFrame = now;
    }
  }
  // --- TOUCH POLLING ---
  int x = 0, y = 0, z = 0;
  bool touched = false;
  if (USE_SOFTWARE_SPI) {
    z = readTouchChannel(0xB1); 
    if (z > 200 && z < 3800) {
      x = readTouchChannel(0xD1); 
      y = readTouchChannel(0x91); 
      touched = true;
    }
  } else {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      x = p.x; y = p.y; z = p.z;
      touched = true;
    }
  }
  if (touched && z > 200 && z < 3800) {
    unsigned long now = millis();
    if (now - lastTap > 500) { 
      page++;
      if (page > 4) page = 0; 
      updateNeeded = true;
      lastTap = now;
    }
  }
  if (updateNeeded) {
    drawPage();
    updateNeeded = false;
  }
}
void drawPage() {
  if (page == 4) { // Standby
    digitalWrite(21, LOW); 
    tft.fillScreen(C_BG);
    return; 
  } else {
    digitalWrite(21, HIGH); 
  }
  
  if (page == 0) drawDashboard();
  if (page == 1) drawList();
  if (page == 2) drawWeeklyGrid();
  if (page == 3) drawBreakdown();
}
// --- VISUAL HELPERS ---
void drawRedBleed() {
  tft.drawRoundRect(0, 0, 320, 240, 10, C_RED_1);
  tft.drawRoundRect(1, 1, 318, 238, 9, C_RED_1);
  tft.drawRoundRect(2, 2, 316, 236, 8, C_RED_2);
  tft.drawRoundRect(3, 3, 314, 234, 7, C_RED_2);
  tft.drawRoundRect(4, 4, 312, 232, 6, C_RED_3);
  tft.drawRoundRect(5, 5, 310, 230, 5, C_RED_3);
  tft.drawRoundRect(6, 6, 308, 228, 4, C_RED_4);
}
void drawMatrixRain() {
  tft.setTextFont(1);
  tft.setTextColor(C_GREEN, C_BG);
  for (int i = 0; i < 10; i++) {
    int x = i * 32 + 10;
    tft.drawChar(' ', x, drops[i], 1);
    drops[i] += 10;
    if (drops[i] > 240) drops[i] = random(0, -100); 
    char c = random(33, 126);
    tft.drawChar(c, x, drops[i], 1);
  }
  tft.setTextFont(4);
  tft.setTextColor(C_GREEN, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ALL SYSTEMS CLEAR", 160, 120);
}
// --- PAGE 0: DASHBOARD ---
void drawDashboard() {
  tft.fillScreen(C_BG);
  
  if (missing == 0) {
    tft.setTextFont(4);
    tft.setTextColor(C_GREEN, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("ALL SYSTEMS CLEAR", 160, 120);
    return;
  }
  
  drawRedBleed();
  tft.setTextFont(4);
  tft.setTextColor(C_RED, C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("CRITICAL ALERT", 160, 15);
  int yNum = 100; 
  tft.setTextFont(4); 
  tft.setTextSize(2); 
  
  tft.setTextColor((missing>0 ? C_RED : C_WHITE), C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(String(missing), 60, yNum);
  
  tft.setTextColor(C_WHITE, C_BG);
  tft.drawString(String(assigned), 160, yNum);
  
  tft.setTextColor(C_CYAN, C_BG);
  tft.drawString(String(done), 260, yNum);
  
  tft.setTextSize(1);
  tft.setTextFont(2);
  tft.setTextColor(C_WHITE, C_BG); 
  tft.drawString("MISSING", 60, 135);
  tft.drawString("ASSIGNED", 160, 135);
  tft.drawString("DONE", 260, 135);
  if (missing > 0) {
    tft.setTextFont(2);
    tft.setTextColor(C_WHITE, C_BG);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("PRIORITY TASK:", 160, 175); 
    
    tft.setTextFont(4);
    tft.setTextColor(C_RED, C_BG);
    tft.setTextDatum(MC_DATUM); 
    String safe = taskName;
    while(tft.textWidth(safe) > 280) safe.remove(safe.length()-1);
    tft.drawString(safe, 160, 205); 
  }
}
// --- PAGE 1: LIST ---
void drawList() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 35, C_CYAN); 
  tft.setTextFont(4);
  tft.setTextColor(C_BG, C_CYAN); 
  tft.setTextDatum(ML_DATUM);
  tft.drawString("ACTIVE TASKS", 10, 18);
  
  String remaining = listData;
  int y = 50;
  for (int i = 0; i < 5; i++) { 
    if (remaining.length() == 0) break;
    int split = remaining.indexOf('~');
    String row = (split == -1) ? remaining : remaining.substring(0, split);
    remaining = (split == -1) ? "" : remaining.substring(split + 1);
    int p1 = row.indexOf('|'); int p2 = row.lastIndexOf('|');
    
    if (p1 != -1 && p2 != -1) {
      String t = row.substring(0, p1);
      String c = row.substring(p1+1, p2);
      String d = row.substring(p2+1);
      
      tft.setTextFont(2);
      tft.setTextColor(C_WHITE, C_BG);
      tft.setTextDatum(TL_DATUM);
      if(t.length() > 25) t = t.substring(0, 25) + "..";
      tft.drawString(t, 10, y); 
      tft.setTextColor(C_CYAN, C_BG);
      tft.drawString(c, 10, y+16); 
      tft.setTextColor(C_RED, C_BG);
      tft.setTextDatum(TR_DATUM);
      tft.drawString(d, 310, y+5); 
      tft.drawLine(10, y+34, 310, y+34, C_GREY);
      y += 38;
    }
  }
}
