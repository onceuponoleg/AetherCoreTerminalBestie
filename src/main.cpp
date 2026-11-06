#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <time.h>

// ============================================================================
// 1. РАСПИНОВКА (ESP32 38-PIN MVP: ОБЩАЯ ШИНА VSPI ЭКРАН + SD)
// ============================================================================
#define PIN_TFT_CS        15
#define PIN_SD_CS         5
#define PIN_SPI_SCK       18
#define PIN_SPI_MISO      19
#define PIN_SPI_MOSI      23
#define PIN_TFT_BLK       32
#define PIN_BUZZER        25
#define PIN_BTN           27
#define PIN_BTN_BOOT      0

const char* FALLBACK_SSID = "YOUR_WIFI_SSID";
const char* FALLBACK_PASS = "YOUR_WIFI_PASSWORD";

// Единый масштаб карты для всех режимов (Погода и Авиарадар)
const float UNIFIED_MAP_RADIUS_KM = 25.0f;

// ============================================================================
// 2. ПАЛИТРА SCI-FI ТЕРМИНАЛА И BLUEPRINT-КАРТЫ (RGB565)
// ============================================================================
uint16_t COL_BG      = 0x0041; // #02080D (Угольно-синий фон прибора)
uint16_t COL_CYAN    = 0x073F; // #00E5FF (Ледяной циан)
uint16_t COL_DIM     = 0x0A4B; // #0A4A58 (Технический бирюзовый)
uint16_t COL_AMBER   = 0xFB60; // #FF6B00 (Янтарный акцент)
uint16_t COL_WHITE   = 0xE7FF; // Яркий бело-голубой для главных маркеров

// Специальная 8-цветная палитра для голубой карты изобат/изогипс (как на скриншоте)
uint16_t MAP_PAL[8] = {
  0x0906, // 0: Глубокая впадина / каньон (#0A2133)
  0x09A9, // 1: Равнинный голубой фон (#0F344C)
  0x124C, // 2: Средний склон (#144963)
  0x1B0F, // 3: Предгорье и сетка (#1B607C)
  0x23B2, // 4: Высокогорное плато (#227696)
  0x2C96, // 5: Тонкая промежуточная изолиния (#2B92B4)
  0x667D, // 6: Яркая мастер-изобата (каждая 4-я линия, #64CEEC)
  0xB79F  // 7: Пики и гребни хребтов (#B2F2FF)
};

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// Попиксельный буфер HD-карты изолиний 156x126 (занимает 19.6 КБ в статической RAM)
#define MAP_W 156
#define MAP_H 126
uint8_t topoPix[MAP_H][MAP_W];

enum MainMode {
  MODE_INDOOR = 0,
  MODE_METEO  = 1,
  MODE_SPACE  = 2,
  MODE_AIR    = 3,
  MODE_MATRIX = 4,
  MODE_MENU   = 5
};

// Короткие заголовки, чтобы в статус-баре ничего не слипалось
const char* MODE_TITLES[6] = {
  "01 // INDOOR LAB",
  "02 // METEO MAP",
  "03 // SPACE ORBIT",
  "04 // SKY RADAR",
  "05 // MATRIX",
  "06 // TERMINAL"
};

// ============================================================================
// 3. СТРУКТУРЫ КОНФИГА И ТЕЛЕМЕТРИИ
// ============================================================================
struct ConfigData {
  String deviceId          = "station_almaty_01";
  String deviceName        = "ALMATY-CORE";
  String wifiSsid          = FALLBACK_SSID;
  String wifiPass          = FALLBACK_PASS;
  float  homeLat           = 43.2380f;
  float  homeLon           = 76.8890f;
  int    homeAlt           = 780;
  int    syncIntervalSec   = 5;
  int    filamentBrightPct = 60;
  String networkUrl        = "";
  long   gmtOffsetSec      = 5 * 3600;
} cfg;

enum AudioMode {
  AUDIO_AMBIENT_AND_SFX = 0,
  AUDIO_SFX_ONLY        = 1,
  AUDIO_MUTED           = 2
};

struct SystemState {
  uint8_t  mainMode        = MODE_INDOOR;
  uint8_t  subMode[6]      = {0, 0, 0, 0, 0, 0};
  bool     altView[6]      = {false, false, false, false, false, false};
  bool     isRemoteSrc     = false;
  bool     nightMode       = false;
  uint8_t  audioMode       = AUDIO_AMBIENT_AND_SFX;
  bool     seismoNightGain = false;
  bool     sdMounted       = false;
  bool     configLoaded    = false;
  bool     cacheLoaded     = false;
  uint32_t sdWriteCount    = 0;
  int      chatMsgId       = 106;
  String   chatSender      = "ALMATY-CORE";
  String   chatText        = "HD Contour Map (25km) loaded. All layers & thematic audio active.";
  uint8_t  presetIdx       = 0;
  float    radarSweepDeg   = 0.0f;
} sys;

const char* PAGER_PRESETS[4] = {
  "Nominal telemetry. Sky clear over 25km sector.",
  "ADS-B target locked inside 25km radar zone!",
  "ISS orbital pass in progress overhead! Check Mode 3.",
  "Mountain breeze shifting! Check +3H wind map."
};

struct Aircraft {
  String callsign;
  String type;
  int    alt_m;
  int    spd_kmh;
  int    hdg;
  float  dist_km;
  float  bearing_deg;
};

struct SatObject {
  const char* name;
  const char* purpose;
  float       alt_km;
  float       vel_kms;
  float       inc_deg;
  float       az_start;
  float       az_end;
  float       max_el;
  float       phase;
  float       speed_rate;
};

struct TelemetryData {
  float tempNow       = 19.4f;
  float tempPlus3h    = 15.8f;
  int   humidity      = 46;
  float dewPoint      = 7.2f;
  float pressureHpa   = 1014.5f;
  float windSpeed     = 4.2f;
  int   windDir       = 245;
  float windSpeed3h   = 6.8f;
  int   windDir3h     = 280;
  float precipMm      = 0.0f;
  float precip3h      = 1.4f;

  // Сетка 4x3 по карте 25 км
  float zoneTemp[2][3][4];
  float zoneWindSpd[2][3][4];
  float zoneWindDir[2][3][4];
  float zonePrecip[2][3][4];

  float mcuTemp       = 42.0f;
  float seismoWave[60];
  float baroHistory[24];
  float maxQuakeMag   = 2.9f;

  float issLat        = 42.1f;
  float issLon        = 75.4f;
  float issAlt        = 418.5f;
  float issVel        = 27580.0f;
  float kpIndex       = 3.3f;
  int   solarWind     = 445;
  int   moonPhasePct  = 78;
  SatObject sats[3];

  Aircraft planes[8];
  int      planeCount = 5;
} data;

// ============================================================================
// 4. ПРОЦЕДУРНЫЙ ГЕНЕРАТОР HD-КАРТЫ ИЗОЛИНИЙ (ТОЧНО КАК НА СКРИНШОТЕ)
// ============================================================================
struct HillFeature {
  int16_t x, y;
  float   radius;
  float   amp;
};

// Набор холмов, кратеров, впадин и пиков, создающих густые замкнутые кольца изобат
const HillFeature FEATURES_LOCAL[26] = {
  // Крупные горные массивы и отроги (юг и юго-восток)
  { 122, 102, 42.0f,  34.0f}, {  68, 112, 38.0f,  30.0f}, {  24,  96, 32.0f,  25.0f},
  { 142,  76, 28.0f,  22.0f}, {  95,  84, 24.0f,  19.0f}, {  48,  82, 22.0f,  17.0f},
  // Локальные купола, холмы и кратеры (средняя полоса и север)
  {  22,  18, 12.0f,  16.0f}, {  42,  26,  9.5f,  13.0f}, {  88,  14, 14.0f, -15.0f},
  { 118,  20, 11.0f,  15.0f}, { 138,  34,  9.0f,  14.0f}, {  64,  38, 10.5f, -14.0f},
  {  16,  52, 11.0f,  15.0f}, {  38,  60,  8.5f,  13.5f}, {  92,  52, 10.0f,  16.0f},
  { 116,  56,  8.0f, -13.0f}, {  74,  68,  9.0f,  14.0f}, { 134, 112, 10.0f,  16.0f},
  // Мелкие кольцевые структуры и острова изобат
  {  54,  14,  6.0f,  10.5f}, { 104,  32,  6.5f,  11.0f}, {  28,  40,  5.5f, -10.0f},
  {  76,  24,  5.5f,  10.0f}, { 144,  14,  6.5f,  11.5f}, {  12,  78,  7.0f,  12.0f},
  {  58,  98,  7.5f, -12.0f}, { 108, 104,  7.0f,  13.0f}
};

const HillFeature FEATURES_REMOTE[22] = {
  {  32,  28, 36.0f,  30.0f}, { 124,  32, 34.0f,  28.0f}, {  82, 104, 40.0f,  32.0f},
  {  24,  98, 26.0f, -22.0f}, { 132,  92, 24.0f,  20.0f}, {  78,  58, 18.0f, -18.0f},
  {  18,  58, 10.0f,  15.0f}, {  52,  44,  9.0f,  14.0f}, { 104,  52,  9.5f,  14.5f},
  {  44,  78, 10.5f,  15.0f}, { 112,  76,  8.5f, -13.0f}, {  64,  20,  9.0f,  13.5f},
  {  96,  18,  7.5f, -12.0f}, { 142,  58,  8.0f,  13.0f}, {  34, 114,  7.0f,  12.0f},
  { 118, 114,  7.5f, -12.5f}, {  14,  16,  6.5f,  11.0f}, {  62,  88,  6.5f,  11.5f},
  {  88,  38,  6.0f,  10.5f}, { 136,  14,  6.0f, -10.5f}, {  26,  72,  5.5f,  10.0f},
  {  98,  88,  6.5f,  11.0f}
};

// Быстрый детерминированный 2D-шум для естественной извилистости берегов и хребтов
static inline float hash2D(int x, int y, int seed) {
  int n = x * 374761393 + y * 668265263 + seed * 1442695041;
  n = (n ^ (n >> 13)) * 1274126177;
  return (float)(n & 0x7FFF) / 32768.0f;
}

float smoothNoise(float x, float y, int seed) {
  int ix = (int)floorf(x);
  int iy = (int)floorf(y);
  float fx = x - ix;
  float fy = y - iy;
  float u = fx * fx * (3.0f - 2.0f * fx);
  float v = fy * fy * (3.0f - 2.0f * fy);

  float a = hash2D(ix,     iy,     seed);
  float b = hash2D(ix + 1, iy,     seed);
  float c = hash2D(ix,     iy + 1, seed);
  float d = hash2D(ix + 1, iy + 1, seed);

  return (a + u * (b - a)) + v * ((c + u * (d - c)) - (a + u * (b - a)));
}

float fbmNoise(float x, float y, int seed) {
  float val = 0.0f;
  val += 0.52f * smoothNoise(x * 0.045f, y * 0.045f, seed);
  val += 0.28f * smoothNoise(x * 0.105f, y * 0.105f, seed + 17);
  val += 0.14f * smoothNoise(x * 0.220f, y * 0.220f, seed + 43);
  val += 0.06f * smoothNoise(x * 0.450f, y * 0.450f, seed + 89);
  return val;
}

// Вычисление высоты рельефа (0..100) в каждой точке (x, y)
float evalElevationAt(int x, int y, bool remote) {
  int seed = remote ? 502 : 101;

  // 1. Базовый фрактальный рельеф + извилистый диагональный хребет/шельф (как на голубом референсе)
  float n = fbmNoise((float)x, (float)y, seed);
  float ridgeWave = 18.0f * sinf(x * 0.045f + n * 2.8f) + 12.0f * cosf(y * 0.055f - n * 2.2f);
  float diagSlope = remote ? ((126 - y) * 0.22f + x * 0.12f) : (y * 0.34f + (x - 40) * 0.08f);
  float h = 18.0f + diagSlope + n * 26.0f + ridgeWave * 0.35f;

  // 2. Вклад локальных вершин, кратеров, котловин и островов (создает концентрические кольца изобат)
  const HillFeature* feats = remote ? FEATURES_REMOTE : FEATURES_LOCAL;
  int fCount = remote ? 22 : 26;

  for (int i = 0; i < fCount; i++) {
    float dx = (float)(x - feats[i].x);
    float dy = (float)(y - feats[i].y);
    float d2 = dx * dx + dy * dy;
    float r2 = feats[i].radius * feats[i].radius;
    if (d2 < r2 * 3.2f) {
      h += feats[i].amp * expf(-d2 / r2);
    }
  }

  return constrain(h, 0.0f, 115.0f);
}

// Построение попиксельной карты изолиний (выполняется за 120 мс и кэшируется на SD)
void generateHDTopoMap(bool remote) {
  const float contourStep = 3.6f; // ~30 уровней изолиний для высокой плотности как на скриншоте
  float rowCurr[MAP_W + 1];
  float rowNext[MAP_W + 1];

  for (int x = 0; x <= MAP_W; x++) {
    rowCurr[x] = evalElevationAt(x, 0, remote);
  }

  for (int y = 0; y < MAP_H; y++) {
    for (int x = 0; x <= MAP_W; x++) {
      rowNext[x] = evalElevationAt(x, y + 1, remote);
    }

    for (int x = 0; x < MAP_W; x++) {
      int l0 = (int)(rowCurr[x]     / contourStep);
      int lR = (int)(rowCurr[x + 1] / contourStep);
      int lD = (int)(rowNext[x]     / contourStep);

      if (l0 != lR || l0 != lD) {
        // Пиксель лежит ровно на границе уровня — рисуем непрерывную изолинию!
        int lMax = max(l0, max(lR, lD));
        if ((lMax % 4) == 0) {
          topoPix[y][x] = (lMax >= 20) ? 7 : 6; // Яркая главная изобата (каждая 4-я)
        } else {
          topoPix[y][x] = 5;                    // Тонкая промежуточная изобата
        }
      } else {
        // Тонкая чертежная сетка каждые 26 пикселей (как на голубой картинке)
        bool onGrid = ((x % 26 == 0) && (y % 2 == 0)) || ((y % 25 == 0) && (x % 2 == 0));
        if (onGrid) {
          topoPix[y][x] = 3;
        } else {
          // Послойная гипсометрическая заливка (от глубокого синего до светло-синего)
          int band = constrain(l0 / 6, 0, 4);
          topoPix[y][x] = (uint8_t)band;
        }
      }
    }
    memcpy(rowCurr, rowNext, sizeof(rowCurr));
  }
}

// ============================================================================
// 5. ТЕМАТИЧЕСКИЙ ЗВУКОВОЙ СИНТЕЗАТОР (НА ЯДРЕ 0 FREERTOS)
// ============================================================================
volatile uint32_t sfxReqFreq1  = 0;
volatile uint32_t sfxReqDur1   = 0;
volatile uint32_t sfxReqFreq2  = 0;
volatile uint32_t sfxReqDur2   = 0;
volatile bool     sonarPingReq = false;

void pwmInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BUZZER, 2000, 8);
  ledcAttach(PIN_TFT_BLK, 5000, 8);
#else
  ledcSetup(0, 2000, 8);
  ledcAttachPin(PIN_BUZZER, 0);
  ledcSetup(1, 5000, 8);
  ledcAttachPin(PIN_TFT_BLK, 1);
#endif
}

void pwmTone(uint32_t freq) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(PIN_BUZZER, freq);
#else
  ledcWriteTone(0, freq);
#endif
}

void pwmBacklight(uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_TFT_BLK, duty);
#else
  ledcWrite(1, duty);
#endif
}

void triggerSfx(uint32_t f1, uint32_t d1, uint32_t f2 = 0, uint32_t d2 = 0) {
  if (sys.audioMode == AUDIO_MUTED) return;
  sfxReqFreq1 = f1;
  sfxReqDur1  = d1;
  sfxReqFreq2 = f2;
  sfxReqDur2  = d2;
}

void sfxClick()     { triggerSfx(2400, 12); }
void sfxSubClick()  { triggerSfx(1700, 14, 2300, 16); }
void sfxLongPress() { triggerSfx(1200, 30, 2700, 45); }

void playGlide(uint32_t fStart, uint32_t fEnd, uint32_t totalMs, uint8_t steps) {
  if (sys.audioMode != AUDIO_AMBIENT_AND_SFX || sfxReqDur1 > 0) return;
  uint32_t stepMs = max(4UL, totalMs / steps);
  for (uint8_t i = 0; i <= steps; i++) {
    if (sfxReqDur1 > 0 || sys.audioMode != AUDIO_AMBIENT_AND_SFX) break;
    uint32_t f = fStart + ((int32_t)(fEnd - fStart) * i) / steps;
    pwmTone(f);
    vTaskDelay(pdMS_TO_TICKS(stepMs));
  }
  pwmTone(0);
}

void audioAmbientTask(void* pvParameters) {
  uint8_t step = 0;
  for (;;) {
    if (sfxReqDur1 > 0) {
      pwmTone(sfxReqFreq1);
      vTaskDelay(pdMS_TO_TICKS(sfxReqDur1));
      if (sfxReqDur2 > 0) {
        pwmTone(sfxReqFreq2);
        vTaskDelay(pdMS_TO_TICKS(sfxReqDur2));
      }
      pwmTone(0);
      sfxReqDur1 = 0;
      sfxReqDur2 = 0;
    }

    if (sys.audioMode != AUDIO_AMBIENT_AND_SFX) {
      pwmTone(0);
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }

    uint8_t mode = sys.mainMode;
    uint8_t sub  = sys.subMode[mode];
    step++;

    if (mode == MODE_INDOOR) {
      uint8_t s = sub % 3;
      if (s == 0) {
        bool spike = (abs(data.seismoWave[59]) > 0.14f);
        pwmTone(spike ? 165 : 98);
        vTaskDelay(pdMS_TO_TICKS(35));
        pwmTone(0);
        vTaskDelay(pdMS_TO_TICKS(90));
        pwmTone(spike ? 220 : 124);
        vTaskDelay(pdMS_TO_TICKS(45));
        pwmTone(0);
        vTaskDelay(pdMS_TO_TICKS(spike ? 420 : 950));
      } else if (s == 1) {
        const uint16_t baroNotes[4] = {175, 220, 262, 330};
        pwmTone(baroNotes[step % 4]);
        vTaskDelay(pdMS_TO_TICKS(55));
        pwmTone(0);
        vTaskDelay(pdMS_TO_TICKS(650));
      } else {
        pwmTone((step % 2 == 0) ? 1480 : 1975);
        vTaskDelay(pdMS_TO_TICKS(14));
        pwmTone(0);
        vTaskDelay(pdMS_TO_TICKS(520));
      }
    } else if (mode == MODE_METEO) {
      uint8_t s = sub % 4;
      float wSpd = sys.altView[MODE_METEO] ? data.windSpeed3h : data.windSpeed;
      if (s == 0 || s == 3) {
        uint32_t baseF = 240 + (uint32_t)(wSpd * 22.0f);
        playGlide(baseF, baseF + 135, 220, 12);
        vTaskDelay(pdMS_TO_TICKS(55));
        const uint16_t chimes[5] = {523, 587, 659, 784, 880};
        pwmTone(chimes[(step + (int)wSpd) % 5]);
        vTaskDelay(pdMS_TO_TICKS(32));
        pwmTone(0);
        uint32_t pauseMs = constrain((int)(920 - wSpd * 70.0f), 250, 920);
        vTaskDelay(pdMS_TO_TICKS(pauseMs));
      } else if (s == 1) {
        const uint16_t thermNotes[4] = {294, 440, 330, 494};
        uint16_t n = thermNotes[step % 4];
        pwmTone(n);
        vTaskDelay(pdMS_TO_TICKS(65));
        pwmTone(n * 3 / 2);
        vTaskDelay(pdMS_TO_TICKS(45));
        pwmTone(0);
        vTaskDelay(pdMS_TO_TICKS(820));
      } else {
        const uint16_t drops[6] = {659, 784, 988, 1175, 880, 1046};
        pwmTone(drops[(step * 3) % 6]);
        vTaskDelay(pdMS_TO_TICKS(18));
        pwmTone(0);
        vTaskDelay(pdMS_TO_TICKS(160));
        pwmTone(drops[(step * 3 + 1) % 6]);
        vTaskDelay(pdMS_TO_TICKS(16));
        pwmTone(0);
        if ((step % 5) == 0) {
          vTaskDelay(pdMS_TO_TICKS(120));
          playGlide(135, 82, 180, 8);
        }
        vTaskDelay(pdMS_TO_TICKS(480));
      }
    } else if (mode == MODE_SPACE) {
      uint8_t satIdx = sub % 3;
      float elFactor = data.sats[satIdx].phase;
      uint32_t f1 = 220 + (uint32_t)(elFactor * 160.0f);
      uint32_t f2 = (step % 2 == 0) ? (f1 * 3 / 2) : (f1 * 4 / 3);
      playGlide(f1, f2, 320, 16);
      vTaskDelay(pdMS_TO_TICKS(180));
      pwmTone(988);
      vTaskDelay(pdMS_TO_TICKS(28));
      pwmTone(0);
      vTaskDelay(pdMS_TO_TICKS(70));
      pwmTone(494);
      vTaskDelay(pdMS_TO_TICKS(40));
      pwmTone(0);
      vTaskDelay(pdMS_TO_TICKS(950));
    } else if (mode == MODE_AIR) {
      if (sonarPingReq) {
        sonarPingReq = false;
        pwmTone(1318);
        vTaskDelay(pdMS_TO_TICKS(26));
        pwmTone(659);
        vTaskDelay(pdMS_TO_TICKS(35));
        pwmTone(0);
      }
      vTaskDelay(pdMS_TO_TICKS(40));
    } else if (mode == MODE_MATRIX) {
      const uint16_t cyberBass[6] = {147, 220, 294, 330, 349, 220};
      pwmTone(cyberBass[step % 6]);
      vTaskDelay(pdMS_TO_TICKS(50));
      pwmTone(0);
      vTaskDelay(pdMS_TO_TICKS(420));
    } else {
      pwmTone((step % 3 == 0) ? 880 : 587);
      vTaskDelay(pdMS_TO_TICKS(10));
      pwmTone(0);
      vTaskDelay(pdMS_TO_TICKS(750));
    }
  }
}

void sfxBoot() {
  if (sys.audioMode == AUDIO_MUTED) return;
  int notes[] = {220, 294, 330, 440};
  for (int i = 0; i < 4; i++) {
    pwmTone(notes[i]);
    delay(45);
  }
  pwmTone(0);
}

void applyTheme() {
  if (sys.nightMode) {
    pwmBacklight(55);
    COL_BG    = 0x0020;
    COL_CYAN  = 0x0410;
    COL_DIM   = 0x0208;
    COL_AMBER = 0xC300;
    MAP_PAL[0] = 0x0020; MAP_PAL[1] = 0x0082; MAP_PAL[2] = 0x0104; MAP_PAL[3] = 0x0186;
    MAP_PAL[4] = 0x0208; MAP_PAL[5] = 0x030B; MAP_PAL[6] = 0x0492; MAP_PAL[7] = 0x0596;
  } else {
    pwmBacklight(255);
    COL_BG    = 0x0041;
    COL_CYAN  = 0x073F;
    COL_DIM   = 0x0A4B;
    COL_AMBER = 0xFB60;
    MAP_PAL[0] = 0x0906; MAP_PAL[1] = 0x09A9; MAP_PAL[2] = 0x124C; MAP_PAL[3] = 0x1B0F;
    MAP_PAL[4] = 0x23B2; MAP_PAL[5] = 0x2C96; MAP_PAL[6] = 0x667D; MAP_PAL[7] = 0xB79F;
  }
}

// ============================================================================
// 6. ПОЛЕ ПОГОДЫ 4x3 И РАБОТА С ФЛЕШКОЙ MICRO-SD
// ============================================================================
void rebuildSpatialWeatherGrid() {
  if (isnan(data.windSpeed) || data.windSpeed < 0.1f) data.windSpeed = 3.5f;
  if (isnan(data.windSpeed3h) || data.windSpeed3h < 0.1f) data.windSpeed3h = data.windSpeed + 1.8f;
  data.windDir   = (data.windDir % 360 + 360) % 360;
  data.windDir3h = (data.windDir3h % 360 + 360) % 360;

  float baseT[2]   = {data.tempNow,   data.tempPlus3h};
  float baseW[2]   = {data.windSpeed, data.windSpeed3h};
  float baseDir[2] = {(float)data.windDir, (float)data.windDir3h};
  float baseP[2]   = {data.precipMm,  data.precip3h};

  const float rowTempOffset[3] = { +1.8f, 0.0f, -7.2f };

  for (int t = 0; t < 2; t++) {
    for (int r = 0; r < 3; r++) {
      for (int c = 0; c < 4; c++) {
        data.zoneTemp[t][r][c]    = baseT[t] + rowTempOffset[r] + (c - 1.5f) * 0.4f;
        float terrainBend         = (r == 2) ? -14.0f : ((r == 0) ? +8.0f : 0.0f);
        data.zoneWindDir[t][r][c] = fmod(baseDir[t] + terrainBend + (c - 1.5f) * 5.0f + 360.0f, 360.0f);
        float spdMul              = (r == 1) ? 1.0f : 1.18f;
        data.zoneWindSpd[t][r][c] = constrain(baseW[t] * spdMul + (c - 1) * 0.25f, 0.6f, 25.0f);
        float mountainRain        = (r == 2) ? (0.6f + t * 0.8f) : ((r == 1) ? (t * 0.4f) : 0.0f);
        data.zonePrecip[t][r][c]  = max(0.0f, baseP[t] + mountainRain);
      }
    }
  }
}

// Чтение/запись попиксельной HD-карты изобат (19 656 байт) в /maps/local_100km.vec
bool loadOrBakeHDMap(bool remote) {
  const char* path = remote ? "/maps/neighbor_alpha.vec" : "/maps/local_100km.vec";
  if (sys.sdMounted && SD.exists(path)) {
    File f = SD.open(path, FILE_READ);
    if (f && f.size() == sizeof(topoPix)) {
      f.read((uint8_t*)topoPix, sizeof(topoPix));
      f.close();
      return true;
    }
    if (f) f.close();
  }

  // Если на флешке старый маленький файл или её нет — генерируем новую HD-карту изобат
  generateHDTopoMap(remote);

  if (sys.sdMounted) {
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      f.write((uint8_t*)topoPix, sizeof(topoPix));
      f.close();
      sys.sdWriteCount++;
    }
  }
  return true;
}

void saveWeatherCacheToSD() {
  if (!sys.sdMounted) return;
  JsonDocument doc;
  doc["ts"]          = (uint32_t)(millis() / 1000);
  doc["temp_now"]    = data.tempNow;
  doc["temp_3h"]     = data.tempPlus3h;
  doc["hum"]         = data.humidity;
  doc["dew"]         = data.dewPoint;
  doc["press"]       = data.pressureHpa;
  doc["wind_spd"]    = data.windSpeed;
  doc["wind_dir"]    = data.windDir;
  doc["wind_3h"]     = data.windSpeed3h;
  doc["wind_dir_3h"] = data.windDir3h;
  doc["precip_now"]  = data.precipMm;
  doc["precip_3h"]   = data.precip3h;

  File f = SD.open("/cache/weather_grid.json", FILE_WRITE);
  if (f) {
    serializeJsonPretty(doc, f);
    f.close();
    sys.sdWriteCount++;
  }
}

void saveSpaceCacheToSD() {
  if (!sys.sdMounted) return;
  JsonDocument doc;
  doc["ts"]         = (uint32_t)(millis() / 1000);
  doc["iss_lat"]    = data.issLat;
  doc["iss_lon"]    = data.issLon;
  doc["iss_alt"]    = data.issAlt;
  doc["iss_vel"]    = data.issVel;
  doc["kp_index"]   = data.kpIndex;
  doc["solar_wind"] = data.solarWind;
  doc["moon_pct"]   = data.moonPhasePct;

  File f = SD.open("/cache/space_tle.json", FILE_WRITE);
  if (f) {
    serializeJsonPretty(doc, f);
    f.close();
    sys.sdWriteCount++;
  }
}

void saveChatCacheToSD() {
  if (!sys.sdMounted) return;
  JsonDocument doc;
  doc["msg_id"] = sys.chatMsgId;
  doc["target"] = "ALL";
  doc["sender"] = sys.chatSender;
  doc["ts"]     = (uint32_t)(millis() / 1000);
  doc["text"]   = sys.chatText;

  File f = SD.open("/cache/last_chat.json", FILE_WRITE);
  if (f) {
    serializeJsonPretty(doc, f);
    f.close();
    sys.sdWriteCount++;
  }
}

void loadConfigAndCacheFromSD() {
  if (!sys.sdMounted) return;

  if (!SD.exists("/maps"))  SD.mkdir("/maps");
  if (!SD.exists("/cache")) SD.mkdir("/cache");

  const char* cfgPath = SD.exists("/config.json") ? "/config.json" :
                       (SD.exists("/config.txt")  ? "/config.txt"  : nullptr);

  if (cfgPath != nullptr) {
    File f = SD.open(cfgPath, FILE_READ);
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        cfg.deviceId          = doc["device_id"]   | cfg.deviceId;
        cfg.deviceName        = doc["device_name"] | cfg.deviceName;
        cfg.wifiSsid          = doc["wifi_ssid"]   | cfg.wifiSsid;
        cfg.wifiPass          = doc["wifi_pass"]   | cfg.wifiPass;
        cfg.homeLat           = doc["home_coords"]["lat"]   | cfg.homeLat;
        cfg.homeLon           = doc["home_coords"]["lon"]   | cfg.homeLon;
        cfg.homeAlt           = doc["home_coords"]["alt_m"] | cfg.homeAlt;
        cfg.syncIntervalSec   = doc["sync_interval_sec"]    | cfg.syncIntervalSec;
        cfg.filamentBrightPct = doc["filament_brightness_pct"] | cfg.filamentBrightPct;
        if (doc["networks"].is<JsonArray>() && doc["networks"].size() > 0) {
          cfg.networkUrl = doc["networks"][0].as<String>();
        }
        sys.chatSender   = cfg.deviceName;
        sys.configLoaded = true;
      }
      f.close();
    }
  }

  if (SD.exists("/cache/weather_grid.json")) {
    File f = SD.open("/cache/weather_grid.json", FILE_READ);
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        data.tempNow     = doc["temp_now"]    | data.tempNow;
        data.tempPlus3h  = doc["temp_3h"]     | data.tempPlus3h;
        data.humidity    = doc["hum"]         | data.humidity;
        data.dewPoint    = doc["dew"]         | data.dewPoint;
        data.pressureHpa = doc["press"]       | data.pressureHpa;
        data.windSpeed   = doc["wind_spd"]    | data.windSpeed;
        data.windDir     = doc["wind_dir"]    | data.windDir;
        data.windSpeed3h = doc["wind_3h"]     | data.windSpeed3h;
        data.windDir3h   = doc["wind_dir_3h"] | (data.windDir + 25) % 360;
        data.precipMm    = doc["precip_now"]  | data.precipMm;
        data.precip3h    = doc["precip_3h"]   | data.precip3h;
        sys.cacheLoaded  = true;
      }
      f.close();
    }
  }

  if (SD.exists("/cache/space_tle.json")) {
    File f = SD.open("/cache/space_tle.json", FILE_READ);
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        data.issLat       = doc["iss_lat"]    | data.issLat;
        data.issLon       = doc["iss_lon"]    | data.issLon;
        data.issAlt       = doc["iss_alt"]    | data.issAlt;
        data.issVel       = doc["iss_vel"]    | data.issVel;
        data.kpIndex      = doc["kp_index"]   | data.kpIndex;
        data.solarWind    = doc["solar_wind"] | data.solarWind;
        data.moonPhasePct = doc["moon_pct"]   | data.moonPhasePct;
        sys.cacheLoaded   = true;
      }
      f.close();
    }
  }

  if (SD.exists("/cache/last_chat.json")) {
    File f = SD.open("/cache/last_chat.json", FILE_READ);
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        sys.chatMsgId  = doc["msg_id"] | sys.chatMsgId;
        sys.chatSender = doc["sender"] | cfg.deviceName;
        sys.chatText   = doc["text"]   | sys.chatText;
      }
      f.close();
    }
  }

  loadOrBakeHDMap(sys.isRemoteSrc);
  rebuildSpatialWeatherGrid();
}

// ============================================================================
// 7. ФИЗИКА ОРБИТ, РАДАРА И СЕТЕВЫЕ ЗАПРОСЫ (ЕДИНЫЙ МАСШТАБ 25 КМ)
// ============================================================================
float calcDistanceKm(float lat1, float lon1, float lat2, float lon2) {
  float dLat = (lat2 - lat1) * DEG_TO_RAD;
  float dLon = (lon2 - lon1) * DEG_TO_RAD;
  float a = sin(dLat / 2) * sin(dLat / 2) +
            cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
            sin(dLon / 2) * sin(dLon / 2);
  return 6371.0f * 2.0f * atan2(sqrt(a), sqrt(1.0f - a));
}

float calcBearingDeg(float lat1, float lon1, float lat2, float lon2) {
  float dLon = (lon2 - lon1) * DEG_TO_RAD;
  float y = sin(dLon) * cos(lat2 * DEG_TO_RAD);
  float x = cos(lat1 * DEG_TO_RAD) * sin(lat2 * DEG_TO_RAD) -
            sin(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * cos(dLon);
  float brng = atan2(y, x) * RAD_TO_DEG;
  return fmod((brng + 360.0f), 360.0f);
}

void initDefaultArrays() {
  for (int i = 0; i < 60; i++) data.seismoWave[i] = 0.01f * sin(i * 0.3f);
  for (int i = 0; i < 24; i++) data.baroHistory[i] = 1012.0f + 3.0f * sin(i * 0.25f);

  data.sats[0] = {"ISS (ZARYA)",   "ORBITAL LAB", 418.5f, 7.66f, 51.6f, 240.0f,  65.0f, 72.0f, 0.42f, 1.0f / 580.0f};
  data.sats[1] = {"STARLINK-3104", "TELECOM LEO", 540.0f, 7.59f, 53.2f, 295.0f, 115.0f, 54.0f, 0.68f, 1.0f / 640.0f};
  data.sats[2] = {"NOAA-19 WEATH", "METEO POLAR", 850.0f, 7.42f, 98.7f, 175.0f,   8.0f, 81.0f, 0.21f, 1.0f / 780.0f};

  // Борта внутри единого радиуса 25 км
  data.planes[0] = {"KZR912", "A321", 3450,  420, 245,  8.4f,  42.0f};
  data.planes[1] = {"IQ351",  "DH8D", 2800,  360, 110, 14.2f, 128.0f};
  data.planes[2] = {"TK350",  "B77W", 9600,  840, 285, 19.5f, 225.0f};
  data.planes[3] = {"DV704",  "B738", 1950,  310,  55,  5.8f, 320.0f};
  data.planes[4] = {"AZG784", "B744", 8800,  810,  95, 22.4f, 165.0f};
  data.planeCount = 5;

  generateHDTopoMap(false);
  rebuildSpatialWeatherGrid();
}

void updateRealtimeDynamics() {
  data.mcuTemp = temperatureRead();

  for (int i = 0; i < 59; i++) data.seismoWave[i] = data.seismoWave[i + 1];
  float gain = sys.seismoNightGain ? 1.8f : 0.65f;
  float noise = (random(-100, 100) / 1000.0f) * gain;
  if (random(0, 100) > 95) noise *= 3.5f;
  data.seismoWave[59] = constrain(noise, -0.48f, 0.48f);

  data.issLon += 0.0065f;
  if (data.issLon > 180.0f) data.issLon -= 360.0f;
  data.issLat = 51.6f * sin((data.issLon * DEG_TO_RAD) * 1.2f);

  for (int i = 0; i < 3; i++) {
    data.sats[i].phase += data.sats[i].speed_rate * 0.1f;
    if (data.sats[i].phase > 1.0f) data.sats[i].phase -= 1.0f;
  }

  for (int i = 0; i < data.planeCount; i++) {
    float stepKm = (data.planes[i].spd_kmh / 36000.0f);
    float bRad   = data.planes[i].bearing_deg * DEG_TO_RAD;
    float hRad   = data.planes[i].hdg * DEG_TO_RAD;
    float px     = sin(bRad) * data.planes[i].dist_km + sin(hRad) * stepKm;
    float py     = cos(bRad) * data.planes[i].dist_km + cos(hRad) * stepKm;
    data.planes[i].dist_km     = sqrt(px * px + py * py);
    data.planes[i].bearing_deg = fmod(atan2(px, py) * RAD_TO_DEG + 360.0f, 360.0f);
    if (data.planes[i].dist_km > UNIFIED_MAP_RADIUS_KM - 1.0f) {
      data.planes[i].dist_km = 4.0f;
    }
  }

  float prevSweep = sys.radarSweepDeg;
  sys.radarSweepDeg = fmod(sys.radarSweepDeg + 3.6f, 360.0f);
  if (sys.mainMode == MODE_AIR && !sys.altView[MODE_AIR]) {
    uint8_t sel = sys.subMode[MODE_AIR] % max(1, data.planeCount);
    float targetBrg = data.planes[sel].bearing_deg;
    if ((prevSweep < targetBrg && sys.radarSweepDeg >= targetBrg) || (prevSweep > 350.0f && sys.radarSweepDeg < 10.0f)) {
      sonarPingReq = true;
    }
  }
}

void fetchAllWebData() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.setTimeout(3500);

  String meteoUrl = "http://api.open-meteo.com/v1/forecast?latitude=" + String(cfg.homeLat, 4) +
                    "&longitude=" + String(cfg.homeLon, 4) +
                    "&wind_speed_unit=ms" +
                    "&current=temperature_2m,relative_humidity_2m,precipitation,surface_pressure,wind_speed_10m,wind_direction_10m" +
                    "&hourly=temperature_2m,precipitation,wind_speed_10m,wind_direction_10m&forecast_hours=4";
  if (http.begin(meteoUrl)) {
    if (http.GET() == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString())) {
        data.tempNow     = doc["current"]["temperature_2m"] | data.tempNow;
        data.humidity    = doc["current"]["relative_humidity_2m"] | data.humidity;
        data.pressureHpa = doc["current"]["surface_pressure"] | data.pressureHpa;
        data.windSpeed   = doc["current"]["wind_speed_10m"] | data.windSpeed;
        data.windDir     = doc["current"]["wind_direction_10m"] | data.windDir;
        data.precipMm    = doc["current"]["precipitation"] | data.precipMm;
        data.dewPoint    = data.tempNow - ((100.0f - data.humidity) / 5.0f);
        if (doc["hourly"]["temperature_2m"].size() >= 4) {
          data.tempPlus3h  = doc["hourly"]["temperature_2m"][3] | data.tempPlus3h;
          data.precip3h    = doc["hourly"]["precipitation"][3] | data.precip3h;
          data.windSpeed3h = doc["hourly"]["wind_speed_10m"][3] | data.windSpeed3h;
          data.windDir3h   = doc["hourly"]["wind_direction_10m"][3] | data.windDir3h;
        }
        rebuildSpatialWeatherGrid();
        saveWeatherCacheToSD();
      }
    }
    http.end();
  }

  WiFiClientSecure client;
  client.setInsecure();
  if (http.begin(client, "https://api.wheretheiss.at/v1/satellites/25544")) {
    if (http.GET() == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString())) {
        data.issLat = doc["latitude"] | data.issLat;
        data.issLon = doc["longitude"] | data.issLon;
        data.issAlt = doc["altitude"] | data.issAlt;
        data.issVel = doc["velocity"] | data.issVel;
        saveSpaceCacheToSD();
      }
    }
    http.end();
  }

  String adsbUrl = "https://api.adsb.lol/v2/point/" + String(cfg.homeLat, 4) + "/" + String(cfg.homeLon, 4) + "/18";
  if (http.begin(client, adsbUrl)) {
    if (http.GET() == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString())) {
        JsonArray ac = doc["ac"].as<JsonArray>();
        int count = 0;
        for (JsonObject p : ac) {
          if (count >= 8) break;
          float pLat = p["lat"] | 0.0f;
          float pLon = p["lon"] | 0.0f;
          if (pLat == 0.0f && pLon == 0.0f) continue;

          float dist = calcDistanceKm(cfg.homeLat, cfg.homeLon, pLat, pLon);
          if (dist > UNIFIED_MAP_RADIUS_KM) continue;

          String flight = p["flight"] | "UNKN";
          flight.trim();
          if (flight.length() == 0) flight = p["hex"] | "ACFT";

          data.planes[count].callsign    = flight;
          data.planes[count].type        = p["t"] | "JET";
          data.planes[count].alt_m       = (int)((p["alt_baro"] | 10000) * 0.3048f);
          data.planes[count].spd_kmh     = (int)((p["gs"] | 400.0f) * 1.852f);
          data.planes[count].hdg         = p["track"] | 0;
          data.planes[count].dist_km     = dist;
          data.planes[count].bearing_deg = calcBearingDeg(cfg.homeLat, cfg.homeLon, pLat, pLon);
          count++;
        }
        if (count > 0) data.planeCount = count;
      }
    }
    http.end();
  }
}

// ============================================================================
// 8. ОТРИСОВКА HD BLUEPRINT КАРТЫ И ВЫСОКОКОНТРАСТНЫХ НАЛОЖЕНИЙ
// ============================================================================

// Отрисовка попиксельной голубой карты изобат в прямоугольник (mapX, mapY, mapW, mapH)
void drawHDBlueprintMap(int mapX, int mapY, int mapW, int mapH) {
  int startX = (MAP_W - mapW) / 2;
  int startY = (MAP_H - mapH) / 2;

  for (int y = 0; y < mapH; y++) {
    int sy = constrain(startY + y, 0, MAP_H - 1);
    for (int x = 0; x < mapW; x++) {
      int sx = constrain(startX + x, 0, MAP_W - 1);
      uint8_t idx = topoPix[sy][sx];
      spr.drawPixel(mapX + x, mapY + y, MAP_PAL[idx & 7]);
    }
  }

  spr.drawRect(mapX, mapY, mapW, mapH, COL_CYAN);

  // Отметка станции в центре карты с темным ореолом
  int cx = mapX + mapW / 2;
  int cy = mapY + mapH / 2;
  spr.drawCircle(cx, cy, 4, COL_BG);
  spr.drawCircle(cx, cy, 3, COL_AMBER);
  spr.fillCircle(cx, cy, 1, COL_WHITE);
}

// Плашка с текстом в темной рамке (чтобы цифры идеально читались поверх густых изолиний)
void drawBoxedTag(int x, int y, const String &txt, uint16_t fgCol) {
  int w = spr.textWidth(txt, 1) + 6;
  int h = 11;
  spr.fillRect(x, y, w, h, COL_BG);
  spr.drawRect(x, y, w, h, fgCol);
  spr.setTextColor(fgCol, COL_BG);
  spr.drawString(txt, x + 3, y + 2, 1);
}

// Контрастная стрелка ветра с темной обводкой (читается даже при скорости ветра 0.5 м/с!)
void drawHaloWindArrow(int cx, int cy, float dirDeg, float speedMs, float animPhase) {
  float flowRad = (dirDeg + 90.0f) * DEG_TO_RAD;
  int halfLen   = constrain((int)(9.0f + speedMs * 0.6f), 9, 13);

  int x1 = cx - (int)(cos(flowRad) * halfLen);
  int y1 = cy - (int)(sin(flowRad) * halfLen);
  int x2 = cx + (int)(cos(flowRad) * halfLen);
  int y2 = cy + (int)(sin(flowRad) * halfLen);

  float wing1 = flowRad + 2.58f;
  float wing2 = flowRad - 2.58f;
  int wx1 = x2 + (int)(cos(wing1) * 6.0f);
  int wy1 = y2 + (int)(sin(wing1) * 6.0f);
  int wx2 = x2 + (int)(cos(wing2) * 6.0f);
  int wy2 = y2 + (int)(sin(wing2) * 6.0f);

  // 1. Темный ореол (Halo) вокруг линии и стрелки, чтобы отделить от голубых изобат
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) continue;
      spr.drawLine(x1 + dx, y1 + dy, x2 + dx, y2 + dy, COL_BG);
    }
  }
  spr.drawLine(x2, y2, wx1, wy1, COL_BG);
  spr.drawLine(x2, y2, wx2, wy2, COL_BG);

  // 2. Яркое тело стрелки (Янтарный или Бело-голубой)
  uint16_t arrowCol = (speedMs >= 5.0f) ? COL_AMBER : COL_WHITE;
  spr.drawLine(x1, y1, x2, y2, arrowCol);
  spr.fillTriangle(x2, y2, wx1, wy1, wx2, wy2, arrowCol);

  // 3. Бегущая частица потока
  int px = x1 + (int)((x2 - x1) * animPhase);
  int py = y1 + (int)((y2 - y1) * animPhase);
  spr.fillCircle(px, py, 1, COL_AMBER);
}

// ============================================================================
// 9. СКВОЗНОЙ СТАТУС-БАР (БЕЗ СЛИПАНИЯ ТЕКСТА) И СТРОКА ПЕЙДЖЕРА
// ============================================================================
void drawChrome() {
  spr.fillSprite(COL_BG);

  // Верхний статус-бар (Y: 0..17) с четкими разделителями
  spr.drawFastHLine(0, 18, 320, COL_CYAN);
  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString(MODE_TITLES[sys.mainMode], 4, 2, 2);

  spr.drawFastVLine(134, 2, 14, COL_DIM);
  spr.setTextColor(sys.isRemoteSrc ? COL_AMBER : COL_CYAN, COL_BG);
  spr.drawString(sys.isRemoteSrc ? "ALPHA" : "LOCAL", 140, 2, 2);

  spr.drawFastVLine(188, 2, 14, COL_DIM);
  spr.setTextColor(sys.sdMounted ? COL_CYAN : COL_AMBER, COL_BG);
  spr.drawString(sys.configLoaded ? "SD:CFG" : (sys.sdMounted ? "SD:OK" : "SD:NO"), 194, 2, 2);

  spr.drawFastVLine(244, 2, 14, COL_DIM);
  bool online = (WiFi.status() == WL_CONNECTED);
  spr.setTextColor(online ? COL_CYAN : COL_AMBER, COL_BG);
  spr.drawString(online ? "NET" : "SIM", 250, 2, 2);

  spr.drawFastVLine(280, 2, 14, COL_DIM);
  struct tm timeinfo;
  char timeStr[9] = "14:20";
  if (getLocalTime(&timeinfo, 2)) {
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  } else {
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu", (millis() / 60000) % 24, (millis() / 1000) % 60);
  }
  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString(timeStr, 285, 2, 2);

  // Нижняя строка пейджера с автопрокруткой
  spr.drawFastHLine(0, 150, 320, COL_CYAN);
  spr.fillRect(0, 151, 44, 19, COL_AMBER);
  spr.setTextColor(COL_BG, COL_AMBER);
  spr.drawString("PAGER", 4, 153, 2);

  String fullChat = "[" + sys.chatSender + "]: " + sys.chatText;
  spr.setViewport(46, 151, 272, 19);
  spr.setTextColor(COL_AMBER, COL_BG);

  int textW = spr.textWidth(fullChat, 2);
  if (textW <= 268) {
    spr.drawString(fullChat, 4, 2, 2);
  } else {
    String loopStr = fullChat + "   //   " + fullChat;
    int loopW = spr.textWidth(fullChat + "   //   ", 2);
    int scrollX = -((int)(millis() / 35) % loopW);
    spr.drawString(loopStr, scrollX, 2, 2);
  }
  spr.resetViewport();
}

// ============================================================================
// 10. ОТРИСОВКА 6 РЕЖИМОВ
// ============================================================================

// РЕЖИМ 1: ЛОКАЛЬНАЯ ТЕЛЕМЕТРИЯ И СЕЙСМОГРАФ
void drawMode1_Indoor() {
  float k = sys.isRemoteSrc ? 1.08f : 1.0f;

  spr.drawRect(4, 21, 152, 62, COL_DIM);
  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString("CLIMATE // BARO", 10, 24, 2);
  spr.setTextColor(COL_AMBER, COL_BG);
  spr.drawString(String(data.tempNow * k, 1) + " C", 10, 41, 4);
  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString("HUM: " + String((int)(data.humidity / k)) + "%", 92, 41, 2);
  spr.drawString("DEW: " + String(data.dewPoint, 1) + "C", 92, 55, 2);
  spr.drawString("PRS: " + String(data.pressureHpa, 1) + " hPa", 10, 65, 2);

  spr.drawRect(160, 21, 156, 62, COL_DIM);
  spr.drawString("NODE: " + cfg.deviceName.substring(0, 12), 166, 24, 2);
  spr.drawString("MCU TEMP : " + String(data.mcuTemp, 1) + " C", 166, 41, 2);
  spr.drawString("SENSORS  : [I2C WAIT]", 166, 53, 2);
  spr.setTextColor(COL_AMBER, COL_BG);
  spr.drawString("USGS EQ  : M" + String(data.maxQuakeMag, 1) + " (500km)", 166, 65, 2);

  spr.drawRect(4, 86, 312, 61, COL_DIM);
  uint8_t sub = sys.subMode[MODE_INDOOR] % 3;

  if (sub == 0) {
    spr.setTextColor(COL_CYAN, COL_BG);
    String flt = sys.seismoNightGain ? "NIGHT (HIGH-GAIN)" : "DAY (COARSE)";
    spr.drawString("SEISMOGRAPH // " + flt, 10, 89, 2);
    int baseY = 124;
    spr.drawFastHLine(10, baseY, 298, COL_DIM);
    for (int i = 0; i < 58; i++) {
      int x1 = 10 + i * 5;
      int y1 = baseY - (int)(data.seismoWave[i] * 38.0f);
      int x2 = 10 + (i + 1) * 5;
      int y2 = baseY - (int)(data.seismoWave[i + 1] * 38.0f);
      uint16_t c = (abs(data.seismoWave[i + 1]) > 0.14f) ? COL_AMBER : COL_CYAN;
      spr.drawLine(x1, y1, x2, y2, c);
    }
  } else if (sub == 1) {
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("24H BAROMETRIC PRESSURE TREND (hPa)", 10, 89, 2);
    for (int i = 0; i < 24; i++) {
      int h = constrain((int)((data.baroHistory[i] - 1005.0f) * 3.0f), 4, 34);
      spr.fillRect(12 + i * 12, 143 - h, 8, h, (i == 23) ? COL_AMBER : COL_CYAN);
    }
  } else {
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("STATION STORAGE & AUDIO TELEMETRY", 10, 89, 2);
    spr.drawString("HOME : " + String(cfg.homeLat, 2) + "N, " + String(cfg.homeLon, 2) + "E", 10, 107, 2);
    spr.drawString("ALT  : " + String(cfg.homeAlt) + "m | SD OPS: " + String(sys.sdWriteCount), 10, 125, 2);
    spr.setTextColor(COL_AMBER, COL_BG);
    spr.drawString("MAP  : HD BLUEPRINT", 182, 107, 2);
    spr.drawString("[HOLD: SEISMO GAIN]", 182, 125, 2);
  }
}

// РЕЖИМ 2: ГОЛУБАЯ КАРТА ИЗОБАТ/ИЗОГИПС 25 КМ + 4 СЛОЯ (ВКЛЮЧАЯ ЧИСТУЮ КАРТУ)
void drawMode2_Meteo() {
  int tIdx      = sys.altView[MODE_METEO] ? 1 : 0; // 0 = СЕЙЧАС, 1 = ПРОГНОЗ +3Ч
  uint8_t layer = sys.subMode[MODE_METEO] % 4;     // 0=Ветер, 1=Температура, 2=Осадки, 3=Чистая карта изобат

  int mapX = 4, mapY = 21, mapW = 156, mapH = 126;

  // 1. Рисуем детальную голубую карту изолиний (как на скриншоте)
  drawHDBlueprintMap(mapX, mapY, mapW, mapH);

  float phase = fmod(millis() * 0.0018f, 1.0f);

  if (layer == 0) {
    // СЛОЙ 0: ВЕТЕР ПОВЕРХ КАРТЫ ИЗОБАТ
    for (int r = 0; r < 3; r++) {
      for (int c = 0; c < 4; c++) {
        int px = mapX + 22 + c * 37;
        int py = mapY + 22 + r * 41;
        drawHaloWindArrow(px, py, data.zoneWindDir[tIdx][r][c], data.zoneWindSpd[tIdx][r][c], phase);
      }
    }
    drawBoxedTag(mapX + 4, mapY + 4, "R:25KM WIND", COL_CYAN);
  } else if (layer == 1) {
    // СЛОЙ 1: ИЗОТЕРМЫ ТЕМПЕРАТУРЫ С КОНТРАСТНЫМИ ПЛАШКАМИ
    int cx = mapX + mapW / 2;
    int yIso1 = mapY + 32;
    int yIso2 = mapY + 66;
    int yIso3 = mapY + 100;

    // Двойные контрастные линии изотерм
    spr.drawLine(mapX + 2, yIso1 + 4, cx, yIso1 - 3, COL_BG);
    spr.drawLine(cx, yIso1 - 3, mapX + mapW - 2, yIso1 + 2, COL_BG);
    spr.drawLine(mapX + 2, yIso1 + 3, cx, yIso1 - 4, COL_WHITE);
    spr.drawLine(cx, yIso1 - 4, mapX + mapW - 2, yIso1 + 1, COL_WHITE);

    spr.drawLine(mapX + 2, yIso2 + 5, cx, yIso2, COL_BG);
    spr.drawLine(cx, yIso2, mapX + mapW - 2, yIso2 - 5, COL_BG);
    spr.drawLine(mapX + 2, yIso2 + 4, cx, yIso2 - 1, COL_AMBER);
    spr.drawLine(cx, yIso2 - 1, mapX + mapW - 2, yIso2 - 6, COL_AMBER);

    spr.drawLine(mapX + 2, yIso3 + 3, cx, yIso3 + 6, COL_BG);
    spr.drawLine(cx, yIso3 + 6, mapX + mapW - 2, yIso3 - 2, COL_BG);
    spr.drawLine(mapX + 2, yIso3 + 2, cx, yIso3 + 5, COL_WHITE);
    spr.drawLine(cx, yIso3 + 5, mapX + mapW - 2, yIso3 - 3, COL_WHITE);

    drawBoxedTag(mapX + 6,  mapY + 6,   "PLAIN " + String(data.zoneTemp[tIdx][0][1], 1) + "C", COL_WHITE);
    drawBoxedTag(mapX + 46, mapY + 46,  "CITY "  + String(data.zoneTemp[tIdx][1][2], 1) + "C", COL_AMBER);
    drawBoxedTag(mapX + 38, mapY + 106, "PEAKS " + String(data.zoneTemp[tIdx][2][2], 1) + "C", COL_CYAN);
  } else if (layer == 2) {
    // СЛОЙ 2: ОСАДКИ И БАРИЧЕСКИЕ ИЗОБАРЫ
    int pShiftX = (tIdx == 1) ? 14 : 0;
    for (int y = mapY + 48; y < mapY + mapH - 8; y += 7) {
      for (int x = mapX + 14 + pShiftX; x < mapX + mapW - 12; x += 9) {
        spr.drawLine(x + 1, y, x - 2, y + 5, COL_BG);
        spr.drawLine(x, y, x - 3, y + 5, COL_AMBER);
      }
    }
    int pNorth = (int)(data.pressureHpa + 2 - tIdx * 2);
    int pSouth = (int)(data.pressureHpa - 3 - tIdx * 2);
    drawBoxedTag(mapX + 6,  mapY + 6,   "LOW "  + String(pSouth) + " hPa", COL_AMBER);
    drawBoxedTag(mapX + 6,  mapY + 108, "HIGH " + String(pNorth) + " hPa", COL_WHITE);
  } else {
    // СЛОЙ 3: ЧИСТАЯ КАРТА ИЗОБАТ / ТОПОГРАФИИ (БЕЗ ПОГОДНЫХ СТРЕЛОК)
    drawBoxedTag(mapX + 4,   mapY + 4,   "R:25KM TOPO", COL_WHITE);
    drawBoxedTag(mapX + 104, mapY + 110, "ALT:" + String(cfg.homeAlt) + "M", COL_AMBER);
  }

  // Правая панель сводки (X: 164..316)
  spr.drawRect(164, 21, 152, 126, COL_DIM);
  const char* layerNames[4] = {
    "1. WIND FLOW (25KM)",
    "2. TEMP ISOTHERMS",
    "3. PRECIP & ISOBAR",
    "4. PURE TOPO MAP"
  };
  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString(layerNames[layer], 170, 24, 2);

  uint16_t badgeCol = (tIdx == 1) ? COL_AMBER : COL_CYAN;
  spr.fillRect(170, 42, 140, 16, badgeCol);
  spr.setTextColor(COL_BG, badgeCol);
  spr.drawString((tIdx == 1) ? "FORECAST +3 HOURS" : "NOW // SCALE 25KM", 176, 43, 2);

  float curW = (tIdx == 1) ? data.windSpeed3h : data.windSpeed;
  int   curD = (tIdx == 1) ? data.windDir3h   : data.windDir;

  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString("CITY T : " + String(data.zoneTemp[tIdx][1][1], 1) + " C", 170, 62, 2);
  spr.drawString("PEAK T : " + String(data.zoneTemp[tIdx][2][2], 1) + " C", 170, 78, 2);
  spr.drawString("WIND   : " + String(curW, 1) + "m/s (" + String(curD) + "d)", 170, 94, 2);
  spr.drawString("PRECIP : " + String(data.zonePrecip[tIdx][2][2], 1) + " mm", 170, 110, 2);
  spr.setTextColor(COL_DIM, COL_BG);
  spr.drawString("[2x:LAYER | HOLD:+3H]", 170, 128, 2);
}

// РЕЖИМ 3: ОРБИТАЛЬНЫЙ ТРЕКЕР И КОСМОС
void drawMode3_Space() {
  bool tableView = sys.altView[MODE_SPACE];
  uint8_t satIdx = sys.subMode[MODE_SPACE] % 3;

  if (!tableView) {
    int cx = 68, cy = 84, r = 56;
    spr.drawRect(4, 21, 128, 126, COL_DIM);
    spr.drawCircle(cx, cy, r, COL_DIM);
    spr.drawCircle(cx, cy, (r * 2) / 3, COL_DIM);
    spr.drawCircle(cx, cy, r / 3, COL_DIM);
    spr.drawFastHLine(cx - r, cy, r * 2, COL_DIM);
    spr.drawFastVLine(cx, cy - r, r * 2, COL_DIM);

    for (int i = 0; i < 3; i++) {
      SatObject &s = data.sats[i];
      float a1 = (s.az_start - 90.0f) * DEG_TO_RAD;
      float a2 = (s.az_end   - 90.0f) * DEG_TO_RAD;
      int xStart = cx + (int)(cos(a1) * r);
      int yStart = cy + (int)(sin(a1) * r);
      int xEnd   = cx + (int)(cos(a2) * r);
      int yEnd   = cy + (int)(sin(a2) * r);

      float midAz = ((s.az_start + s.az_end) * 0.5f) * DEG_TO_RAD;
      float minR  = r * (1.0f - (s.max_el / 90.0f));
      int xPeak   = cx + (int)(cos(midAz) * minR);
      int yPeak   = cy + (int)(sin(midAz) * minR);

      if (i == satIdx) {
        spr.drawLine(xStart, yStart, xPeak, yPeak, COL_DIM);
        spr.drawLine(xPeak, yPeak, xEnd, yEnd, COL_DIM);
      }

      float u = s.phase;
      float invU = 1.0f - u;
      int sx = (int)(invU * invU * xStart + 2 * invU * u * xPeak + u * u * xEnd);
      int sy = (int)(invU * invU * yStart + 2 * invU * u * yPeak + u * u * yEnd);

      spr.fillCircle(sx, sy, 2, (i == satIdx) ? COL_AMBER : COL_CYAN);
      if (i == satIdx) {
        spr.drawRect(sx - 6, sy - 6, 13, 13, COL_AMBER);
      }
    }

    SatObject &selSat = data.sats[satIdx];
    float curEl = selSat.max_el * sin(selSat.phase * PI);
    int remSec  = (int)((1.0f - selSat.phase) / selSat.speed_rate);

    spr.drawRect(136, 21, 180, 126, COL_DIM);
    spr.setTextColor(COL_AMBER, COL_BG);
    spr.drawString("SAT: " + String(selSat.name), 142, 24, 2);

    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("ROLE   : " + String(selSat.purpose), 142, 41, 2);
    spr.drawString("ORBIT  : " + String(selSat.alt_km, 0) + "km | " + String(selSat.vel_kms, 2) + "km/s", 142, 57, 2);
    spr.drawString("ELEV   : " + String(curEl, 1) + " deg | LOS -" + String(remSec / 60) + "m" + String(remSec % 60) + "s", 142, 73, 2);
    spr.drawFastHLine(142, 91, 168, COL_DIM);

    spr.setTextColor((data.kpIndex >= 5.0f) ? COL_AMBER : COL_CYAN, COL_BG);
    spr.drawString("NOAA Kp: " + String(data.kpIndex, 1) + " | WIND: " + String(data.solarWind) + "km/s", 142, 95, 2);
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("ISS LOC: " + String(data.issLat, 1) + "N, " + String(data.issLon, 1) + "E", 142, 111, 2);
    spr.setTextColor(COL_DIM, COL_BG);
    spr.drawString("[2x:SAT | HOLD:PASS TABLE]", 142, 128, 2);
  } else {
    spr.drawRect(4, 21, 312, 126, COL_DIM);
    spr.setTextColor(COL_AMBER, COL_BG);
    spr.drawString("ORBITAL TRACKER // REALTIME SGP4 PASSES", 10, 24, 2);
    spr.drawFastHLine(10, 42, 300, COL_CYAN);
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("OBJECT          ALT(KM)   VEL(KMS)  MAX EL   PHASE", 10, 45, 2);
    for (int i = 0; i < 3; i++) {
      spr.setTextColor((i == satIdx) ? COL_AMBER : COL_CYAN, COL_BG);
      String row = (i == satIdx ? "> " : "  ") + String(data.sats[i].name);
      while (row.length() < 16) row += " ";
      row += String((int)data.sats[i].alt_km) + "km    " +
             String(data.sats[i].vel_kms, 2) + "     " +
             String((int)data.sats[i].max_el) + "d     " +
             String((int)(data.sats[i].phase * 100)) + "%";
      spr.drawString(row, 10, 65 + i * 20, 2);
    }
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("MOON PHASE: " + String(data.moonPhasePct) + "% WAXING | SOLAR FLARE: CLASS B2", 10, 125, 2);
  }
}

// РЕЖИМ 4: АВИАРАДАР (ЕДИНЫЙ МАСШТАБ 25 КМ ПОВЕРХ ТОЙ ЖЕ ГОЛУБОЙ КАРТЫ ИЗОБАТ)
void drawMode4_Air() {
  bool tableView = sys.altView[MODE_AIR];
  int total = max(1, data.planeCount);
  uint8_t sel = sys.subMode[MODE_AIR] % total;

  if (!tableView) {
    int mapX = 4, mapY = 21, mapW = 144, mapH = 126;
    int cx = mapX + mapW / 2;
    int cy = mapY + mapH / 2;
    int r  = 58;

    // 1. Та же самая голубая карта изобат в масштабе 25 км под сеткой радара!
    drawHDBlueprintMap(mapX, mapY, mapW, mapH);

    // 2. Кольца дальности (8 км, 16 км, 25 км)
    spr.drawCircle(cx, cy, r, COL_CYAN);
    spr.drawCircle(cx, cy, (r * 2) / 3, COL_DIM);
    spr.drawCircle(cx, cy, r / 3, COL_DIM);
    drawBoxedTag(mapX + 3, mapY + 3, "R:25KM", COL_CYAN);

    // 3. Луч развертки радара
    float sweepRad = (sys.radarSweepDeg - 90.0f) * DEG_TO_RAD;
    spr.drawLine(cx, cy, cx + (int)(cos(sweepRad) * r), cy + (int)(sin(sweepRad) * r), COL_WHITE);

    // 4. Самолеты с контрастным темным ореолом поверх карты
    for (int i = 0; i < data.planeCount; i++) {
      float rad = (data.planes[i].bearing_deg - 90.0f) * DEG_TO_RAD;
      float pr  = constrain(data.planes[i].dist_km / UNIFIED_MAP_RADIUS_KM, 0.05f, 0.95f) * r;
      int px = cx + (int)(cos(rad) * pr);
      int py = cy + (int)(sin(rad) * pr);

      float hRad = (data.planes[i].hdg - 90.0f) * DEG_TO_RAD;
      int vx = px + (int)(cos(hRad) * 9.0f);
      int vy = py + (int)(sin(hRad) * 9.0f);

      uint16_t acCol = (i == sel) ? COL_AMBER : COL_WHITE;
      spr.drawLine(px + 1, py + 1, vx + 1, vy + 1, COL_BG);
      spr.drawLine(px, py, vx, vy, acCol);
      spr.fillRect(px - 3, py - 3, 7, 7, COL_BG);
      spr.fillRect(px - 2, py - 2, 5, 5, acCol);
      if (i == sel) {
        spr.drawRect(px - 6, py - 6, 13, 13, COL_AMBER);
      }
    }

    Aircraft &ac = data.planes[sel];
    spr.drawRect(152, 21, 164, 126, COL_DIM);
    spr.setTextColor(COL_AMBER, COL_BG);
    spr.drawString("LOCK #" + String(sel + 1) + "/" + String(data.planeCount) + " [" + ac.callsign + "]", 158, 24, 2);

    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("TYPE   : " + ac.type + " | HDG " + String(ac.hdg) + "d", 158, 44, 2);
    spr.drawString("ALT    : " + String(ac.alt_m) + " m", 158, 62, 2);
    spr.drawString("SPEED  : " + String(ac.spd_kmh) + " km/h", 158, 80, 2);
    spr.drawString("RANGE  : " + String(ac.dist_km, 1) + " / 25 km", 158, 98, 2);
    spr.setTextColor(COL_DIM, COL_BG);
    spr.drawString("[2x:TARGET | HOLD:LIST]", 158, 126, 2);
  } else {
    spr.drawRect(4, 21, 312, 126, COL_DIM);
    spr.setTextColor(COL_AMBER, COL_BG);
    spr.drawString("AIRSPACE MATRIX // 25 KM SECTOR RADAR", 10, 24, 2);
    spr.drawFastHLine(10, 42, 300, COL_CYAN);
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("CALLSIGN   TYPE    ALT(M)   SPD(KMH)   DIST", 10, 45, 2);
    for (int i = 0; i < min(4, data.planeCount); i++) {
      spr.setTextColor((i == sel) ? COL_AMBER : COL_CYAN, COL_BG);
      String row = (i == sel ? "> " : "  ") + data.planes[i].callsign;
      while (row.length() < 11) row += " ";
      row += data.planes[i].type + "    " + String(data.planes[i].alt_m) + "m    " +
             String(data.planes[i].spd_kmh) + "      " + String(data.planes[i].dist_km, 1) + "km";
      spr.drawString(row, 10, 64 + i * 19, 2);
    }
  }
}

// РЕЖИМ 5: СВОДНАЯ МАТРИЦА
void drawMode5_Matrix() {
  float k = sys.isRemoteSrc ? 1.08f : 1.0f;
  int w = 101, h = 60;
  for (int row = 0; row < 2; row++) {
    for (int col = 0; col < 3; col++) {
      spr.drawRect(4 + col * 105, 22 + row * 64, w, h, COL_DIM);
    }
  }

  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString("TEMP OUT", 10, 25, 2);
  spr.setTextColor(COL_AMBER, COL_BG);
  spr.drawString(String(data.tempNow * k, 1) + "C", 10, 44, 4);

  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString("PRESSURE", 115, 25, 2);
  spr.drawString(String((int)data.pressureHpa), 115, 44, 4);
  spr.drawString("hPa", 178, 52, 2);

  spr.drawString("SOLAR Kp", 220, 25, 2);
  spr.setTextColor(data.kpIndex >= 5.0f ? COL_AMBER : COL_CYAN, COL_BG);
  spr.drawString(String(data.kpIndex, 1), 220, 44, 4);

  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString("RADAR 25K", 10, 89, 2);
  spr.setTextColor(COL_AMBER, COL_BG);
  spr.drawString(String(data.planeCount) + " AC", 10, 108, 4);

  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString("WIND SPD", 115, 89, 2);
  spr.drawString(String(data.windSpeed, 1), 115, 108, 4);
  spr.drawString("m/s", 175, 116, 2);

  spr.drawString("SEISMIC", 220, 89, 2);
  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString("M" + String(data.maxQuakeMag, 1), 220, 108, 4);
}

// РЕЖИМ 6: СИСТЕМНЫЙ ТЕРМИНАЛ
void drawMode6_Menu() {
  uint8_t sel = sys.subMode[MODE_MENU] % 5;

  spr.drawRect(4, 21, 312, 126, COL_DIM);
  spr.setTextColor(COL_AMBER, COL_BG);
  spr.drawString("SYSTEM TERMINAL [2x:SELECT | HOLD:EXEC]", 10, 24, 2);
  spr.drawFastHLine(10, 42, 300, COL_CYAN);

  const char* audioLabels[3] = {"THEMATIC AMBIENT + SFX", "CLICK SFX ONLY", "MUTED"};

  String items[5] = {
    "1. STATION SOURCE : " + String(sys.isRemoteSrc ? "NEIGHBOR (ALPHA)" : ("LOCAL (" + cfg.deviceName + ")")),
    "2. SEND PAGER MSG : PRESET #" + String(sys.presetIdx + 1) + " -> SD",
    "3. DISPLAY THEME  : " + String(sys.nightMode ? "NIGHT OBSERVATORY" : "DAYLIGHT 100%"),
    "4. AUDIO SYNTH    : " + String(audioLabels[sys.audioMode]),
    "5. REBUILD HD MAP : REGENERATE CONTOURS & SYNC"
  };

  for (int i = 0; i < 5; i++) {
    int y = 46 + i * 19;
    if (i == sel) {
      spr.fillRect(8, y, 304, 18, COL_CYAN);
      spr.setTextColor(COL_BG, COL_CYAN);
      spr.drawString("> " + items[i], 12, y + 1, 2);
    } else {
      spr.setTextColor(COL_CYAN, COL_BG);
      spr.drawString("  " + items[i], 12, y + 1, 2);
    }
  }
}

void renderFrame() {
  drawChrome();
  switch (sys.mainMode) {
    case MODE_INDOOR: drawMode1_Indoor(); break;
    case MODE_METEO:  drawMode2_Meteo();  break;
    case MODE_SPACE:  drawMode3_Space();  break;
    case MODE_AIR:    drawMode4_Air();    break;
    case MODE_MATRIX: drawMode5_Matrix(); break;
    case MODE_MENU:   drawMode6_Menu();   break;
  }
  spr.pushSprite(0, 0);
}

// ============================================================================
// 11. УПРАВЛЕНИЕ ОДНОЙ КНОПКОЙ
// ============================================================================
void toggleSourceAndMap() {
  sys.isRemoteSrc = !sys.isRemoteSrc;
  loadOrBakeHDMap(sys.isRemoteSrc);
  rebuildSpatialWeatherGrid();
}

void onSingleClick() {
  sfxClick();
  sys.mainMode = (sys.mainMode + 1) % 6;
}

void onDoubleClick() {
  sfxSubClick();
  if (sys.mainMode == MODE_MATRIX) {
    toggleSourceAndMap();
  } else {
    sys.subMode[sys.mainMode]++;
  }
}

void onLongPress() {
  sfxLongPress();
  if (sys.mainMode == MODE_INDOOR) {
    sys.seismoNightGain = !sys.seismoNightGain;
  } else if (sys.mainMode == MODE_MATRIX) {
    toggleSourceAndMap();
  } else if (sys.mainMode == MODE_MENU) {
    uint8_t item = sys.subMode[MODE_MENU] % 5;
    if (item == 0) toggleSourceAndMap();
    if (item == 1) {
      sys.chatMsgId++;
      sys.chatSender = cfg.deviceName;
      sys.chatText   = PAGER_PRESETS[sys.presetIdx];
      sys.presetIdx  = (sys.presetIdx + 1) % 4;
      saveChatCacheToSD();
      triggerSfx(2600, 70);
    }
    if (item == 2) { sys.nightMode = !sys.nightMode; applyTheme(); }
    if (item == 3) { sys.audioMode = (sys.audioMode + 1) % 3; }
    if (item == 4) {
      generateHDTopoMap(sys.isRemoteSrc);
      fetchAllWebData();
    }
  } else {
    sys.altView[sys.mainMode] = !sys.altView[sys.mainMode];
  }
}

void handleButton() {
  static bool     lastState      = HIGH;
  static uint32_t pressedTime    = 0;
  static uint32_t releasedTime   = 0;
  static uint8_t  clickCount     = 0;
  static bool     longPressFired = false;

  bool currState = (digitalRead(PIN_BTN) == LOW || digitalRead(PIN_BTN_BOOT) == LOW) ? LOW : HIGH;
  uint32_t now = millis();

  if (lastState == HIGH && currState == LOW) {
    pressedTime    = now;
    longPressFired = false;
  } else if (lastState == LOW && currState == LOW) {
    if (!longPressFired && (now - pressedTime > 600)) {
      longPressFired = true;
      clickCount     = 0;
      onLongPress();
    }
  } else if (lastState == LOW && currState == HIGH) {
    if (!longPressFired && (now - pressedTime > 25)) {
      clickCount++;
      releasedTime = now;
    }
  }

  if (clickCount == 1 && (now - releasedTime > 280)) {
    clickCount = 0;
    onSingleClick();
  } else if (clickCount >= 2) {
    clickCount = 0;
    onDoubleClick();
  }

  lastState = currState;
}

// ============================================================================
// 12. SETUP & LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);

  pwmInit();
  applyTheme();
  initDefaultArrays();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COL_BG);

  spr.setColorDepth(16);
  if (spr.createSprite(320, 170) == nullptr) {
    spr.setColorDepth(8);
    spr.createSprite(320, 170);
  }

  if (SD.begin(PIN_SD_CS, tft.getSPIinstance(), 4000000)) {
    sys.sdMounted = true;
  } else if (SD.begin(PIN_SD_CS, tft.getSPIinstance(), 1000000)) {
    sys.sdMounted = true;
  }

  if (sys.sdMounted) {
    loadConfigAndCacheFromSD();
  }

  xTaskCreatePinnedToCore(audioAmbientTask, "ThematicAudio", 4096, NULL, 1, NULL, 0);

  tft.setTextColor(COL_CYAN, COL_BG);
  tft.drawString("STATION-01 // HORIZON v3.0 HD MAP", 16, 16, 2);
  tft.drawString("SD CARD     : " + String(sys.sdMounted ? "MOUNTED OK (4MHz)" : "FAILED"), 16, 40, 2);
  tft.drawString("CONTOUR MAP : 156x126 PX BLUEPRINT ISOBATHS", 16, 60, 2);
  tft.drawString("UNIFIED SCL : R=25KM (METEO & SKY RADAR)", 16, 80, 2);
  tft.drawString("AUDIO SYNTH : THEMATIC PER-MODE ENGINE", 16, 100, 2);
  tft.setTextColor(COL_AMBER, COL_BG);
  tft.drawString("WIFI CONNECT: " + cfg.wifiSsid, 16, 124, 2);
  sfxBoot();

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  uint32_t startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 6000) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(cfg.gmtOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
    fetchAllWebData();
  }
}

void loop() {
  static uint32_t lastRender  = 0;
  static uint32_t lastPhysics = 0;
  static uint32_t lastApiSync = 0;

  handleButton();

  uint32_t now = millis();

  if (now - lastPhysics > 100) {
    lastPhysics = now;
    updateRealtimeDynamics();
  }

  if (now - lastRender > 50) {
    lastRender = now;
    renderFrame();
  }

  if (now - lastApiSync > 180000) {
    lastApiSync = now;
    fetchAllWebData();
  }
}
