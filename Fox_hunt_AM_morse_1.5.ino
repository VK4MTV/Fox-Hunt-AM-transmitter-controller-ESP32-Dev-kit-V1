#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <vector>
#include <esp_task_wdt.h>

// --- CONFIGURATION for ESP32 DevKit V1 ---
const char* ssid = "FOX_HUNT_AM_TRANSMITTER";
const char* password = "foxhunt123";

#define PWM_PIN      27      // Safe PWM pin - no conflict with VSPI/SD
#define PLAY_PIN     14
#define STOP_PIN     13
#define SD_CS_PIN     5      // Default VSPI CS pin

#define PWM_FREQ    80000    // 80 kHz carrier for clean AM modulation
#define PWM_RES      8

#define BUFFER_SIZE 512
uint8_t audioBuffer[BUFFER_SIZE];
int bufferPointer = 0;
int bytesInBuffer = 0;

// --- GLOBALS ---
AsyncWebServer server(80);
std::vector<String> playlist;
int currentIndex = 0;
volatile bool isRunning = false;
float gain_booster = 1.08f;        // Start here - increase if too quiet
TaskHandle_t AudioTaskHandle = nullptr;
bool sdMounted = false;

const char* morse_table[] = {
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.", 
  "", "", "", "", "", "", "", 
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", 
  "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.." 
};

const int MORSE_LETTER_OFFSET = 17;

// IMA-ADPCM tables
const int step_table[] = { 
  7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
  157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
  1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
  10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767 
};

const int8_t index_table[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};

int predictor = 0;
int step_index = 0;

// --- FILE HELPERS ---
File openAnyFile(const String& item) {
  if (item.startsWith("SD:")) return SD.open(item.substring(3));
  return LittleFS.open("/" + item);
}

bool fileExistsAny(const String& item) {
  if (item.startsWith("SD:")) return SD.exists(item.substring(3));
  return LittleFS.exists("/" + item);
}

String getStorageInfo() {
  String info = "LittleFS: ";
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  info += String((float)used / total * 100, 1) + "% (" + String(used/1024) + "KB/" + String(total/1024) + "KB)";
  if (sdMounted) {
    info += " | SD: " + String(SD.usedBytes()/(1024*1024)) + "MB/" + String(SD.totalBytes()/(1024*1024)) + "MB";
  } else {
    info += " | SD: not mounted";
  }
  return info;
}

void savePlaylistToDisk() {
  File file = LittleFS.open("/playlist.json", "w");
  if (!file) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& s : playlist) arr.add(s);
  serializeJson(doc, file);
  file.close();
}

void loadPlaylistFromDisk() {
  if (!LittleFS.exists("/playlist.json")) return;
  File file = LittleFS.open("/playlist.json", "r");
  if (!file) return;
  JsonDocument doc;
  deserializeJson(doc, file);
  playlist.clear();
  for (JsonVariant v : doc.as<JsonArray>()) playlist.push_back(v.as<String>());
  file.close();
}

void apply_ramp(int target, int ms) {
  static int current = 0;
  const int steps = 25;
  if (ms <= 0) ms = 5;
  int diff = target - current;
  for (int i = 1; i <= steps; ++i) {
    int val = current + (diff * i / steps);
    ledcWrite(PWM_PIN, constrain(val, 0, 255));
    delayMicroseconds((ms * 1000) / steps);
  }
  current = target;
}

void decodeAndOutput(uint8_t nibble) {
  int step = step_table[step_index];
  int diff = step >> 3;
  if (nibble & 4) diff += step;
  if (nibble & 2) diff += (step >> 1);
  if (nibble & 1) diff += (step >> 2);
  if (nibble & 8) predictor -= diff;
  else predictor += diff;

  predictor = constrain(predictor, -32768, 32767);
  step_index = constrain(step_index + index_table[nibble & 0x0F], 0, 88);

  int output = constrain((int)(((predictor + 32768) >> 8) * gain_booster), 0, 255);
  ledcWrite(PWM_PIN, output);
}

void playWavFile(const String& item) {
  File f = openAnyFile(item);
  if (!f) return;

  apply_ramp(128, 12);

  uint8_t header[44];
  f.read(header, 44);
  uint32_t sampleRate = *(uint32_t*)(header + 24);
  uint16_t bits = *(uint16_t*)(header + 34);
  bool isPCM = (bits == 8);
  uint32_t delayUs = (sampleRate > 0) ? 1000000UL / sampleRate : 125;

  predictor = 0; step_index = 0;

  while (f.available() && isRunning) {
    if (bufferPointer >= bytesInBuffer) {
      bytesInBuffer = f.read(audioBuffer, BUFFER_SIZE);
      bufferPointer = 0;
      if (bytesInBuffer == 0) break;
    }
    uint8_t b = audioBuffer[bufferPointer++];

    if (isPCM) {
      ledcWrite(PWM_PIN, constrain((int)(b * gain_booster), 0, 255));
      delayMicroseconds(delayUs);
    } else {
      decodeAndOutput(b & 0x0F);
      decodeAndOutput(b >> 4);
      delayMicroseconds(62);
    }

    static uint32_t lastWdt = 0;
    if (millis() - lastWdt > 40) {
      esp_task_wdt_reset();
      lastWdt = millis();
    }
  }
  f.close();
  apply_ramp(0, 15);
}

void sendMorse(const String& text) {
  String upper = text; upper.toUpperCase();
  for (char c : upper) {
    if (!isRunning) break;
    if (c == ' ') { ledcWrite(PWM_PIN, 0); vTaskDelay(pdMS_TO_TICKS(700)); continue; }

    int idx = -1;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c >= 'A' && c <= 'Z') idx = c - 'A' + MORSE_LETTER_OFFSET;
    if (idx < 0) continue;

    const char* pat = morse_table[idx];
    for (size_t j = 0; pat[j]; ++j) {
      if (!isRunning) break;
      int dur = (pat[j] == '.') ? 200 : 600;
      apply_ramp(255, 8);
      vTaskDelay(pdMS_TO_TICKS(dur));
      apply_ramp(0, 8);
      vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelay(pdMS_TO_TICKS(400));
  }
}

void playNextItem() {
  if (playlist.empty() || !isRunning) return;
  String item = playlist[currentIndex];

  if (item.startsWith("M:")) sendMorse(item.substring(2));
  else if (int sec = item.toInt(); sec > 0) {
    apply_ramp(0, 15);
    delay(sec * 1000UL);
  }
  else if (fileExistsAny(item)) playWavFile(item);

  currentIndex = (currentIndex + 1) % playlist.size();
}

void audioTask(void *pvParameters) {
  esp_task_wdt_add(nullptr);
  while (true) {
    if (isRunning) playNextItem();
    else vTaskDelay(pdMS_TO_TICKS(50));
    taskYIELD();
  }
}

// ====================== WEB UI (copy full HTML from previous message) ======================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>Fox Hunt AM PWM</title>
<style>
  body { background:#000; color:#0f0; font-family:'Courier New', monospace; padding:20px; }
  h2 { border-bottom: 2px solid #040; }
  .box { margin:15px 0; padding:15px; border:1px solid #0f0; background:#010; }
  .item { display:flex; align-items:center; background:#010; border:1px solid #030; margin:5px 0; padding:5px; }
  button { background:#020; color:#0f0; border:1px solid #0f0; cursor:pointer; padding:6px; margin:2px; }
  button:hover { background:#0f0; color:#000; }
  input { background:#000; color:#0f0; border:1px solid #0f0; padding:5px; }
  .save-btn { background:#040; border-color:#0f0; font-weight:bold; width:100%; font-size:16px; margin:10px 0; }
</style></head><body>
  <div id="mem" style="border:1px solid #040; padding:10px;">STORAGE: ...</div>
  <h2>[ PLAYLIST ]</h2><div id="playlist"></div>
  <button class="save-btn" onclick="saveAndPlay()">SAVE & START BROADCAST</button>
  <div class="box">[ MORSE ] <input id="mmsg" placeholder="VK4MTV FOX 1"><button onclick="addM()">ADD</button></div>
  <div class="box">[ UPLOAD WAV to LittleFS ] <input type="file" id="fi"><button onclick="up()">UPLOAD</button><div id="prg"></div></div>
  <div class="box">[ SILENCE sec ] <input type="number" id="sec" min="1"><button onclick="addS()">ADD</button></div>
  <h2>[ FILES ]</h2><div id="files"></div>
  <button style="color:red; border-color:red;" onclick="clearPL()">CLEAR PLAYLIST</button>
<script>
  function load() {
    fetch('/api/status').then(r=>r.json()).then(d=>{
      document.getElementById('mem').innerText = d.storage;
      let p = ''; d.playlist.forEach((f,i)=> p += `<div class="item"><button onclick="act('move',${i},-1)">▲</button><button onclick="act('move',${i},1)">▼</button><button onclick="act('remove',${i})">X</button> ${f}</div>`);
      document.getElementById('playlist').innerHTML = p || 'Empty';
      let fstr = ''; d.files.forEach(n => {
        let prefix = n.startsWith('SD:') ? '💾 ' : '';
        fstr += `<div class="item"><button onclick="act('add','${n}')">ADD</button> ${prefix}${n} <button onclick="act('del-disk','${n}')">DEL</button></div>`;
      });
      document.getElementById('files').innerHTML = fstr;
    });
  }
  function act(a,i,d=0) { 
    let url = `/api/${a}?i=${i}&d=${d}`;
    if (a==='add' || a==='del-disk') url += `&f=${encodeURIComponent(i)}`;
    fetch(url).then(load); 
  }
  function addM() { let m=document.getElementById('mmsg').value; if(m) fetch('/api/add?f=M:'+encodeURIComponent(m)).then(load); }
  function addS() { let s=document.getElementById('sec').value; if(s) fetch('/api/add?f='+s).then(load); }
  function saveAndPlay() { fetch('/api/save-play').then(()=>{load(); alert('Playlist saved and broadcast started!');}); }
  function up() {
    let fi=document.getElementById('fi'); if(!fi.files.length) return;
    let fd=new FormData(); fd.append("data", fi.files[0], fi.files[0].name);
    let x=new XMLHttpRequest(); x.open("POST","/api/upload");
    x.upload.onprogress = e => document.getElementById('prg').innerText = Math.round(e.loaded/e.total*100)+"%";
    x.onload = ()=>{ load(); document.getElementById('prg').innerText='Done'; };
    x.send(fd);
  }
  function clearPL() { if(confirm('Wipe playlist?')) fetch('/api/clear').then(load); }
  load();
</script></body></html>)rawliteral";

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(240);

  pinMode(PLAY_PIN, INPUT_PULLUP);
  pinMode(STOP_PIN, INPUT_PULLUP);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS failed!");
    while(true) delay(1000);
  }
  loadPlaylistFromDisk();

  sdMounted = SD.begin(SD_CS_PIN);
  if (sdMounted) Serial.println("SD Card mounted successfully");
  else Serial.println("SD not detected - using LittleFS only");

  WiFi.softAP(ssid, password);

  ledcAttach(PWM_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(PWM_PIN, 0);

  // Add all your server.on() routes here (status, add, move, remove, upload, etc.)
  // Use the exact same web handlers from the previous full version

  server.begin();

  esp_task_wdt_config_t wdt_config = {.timeout_ms = 8000, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_init(&wdt_config);

  xTaskCreatePinnedToCore(audioTask, "AudioTask", 12288, nullptr, 3, &AudioTaskHandle, 1);

  Serial.println("AM PWM Transmitter ready - PWM on GPIO27, SD on VSPI");
}

void loop() {
  if (digitalRead(PLAY_PIN) == LOW) isRunning = true;
  if (digitalRead(STOP_PIN) == LOW) {
    isRunning = false;
    apply_ramp(0, 10);
  }
  vTaskDelay(pdMS_TO_TICKS(10));
}
