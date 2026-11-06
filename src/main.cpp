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

// Резервный Wi-Fi на случай, если флешка не вставлена или не прочиталась
const char* FALLBACK_SSID = "YOUR_WIFI_SSID";
const char* FALLBACK_PASS = "YOUR_WIFI_PASSWORD";

// ============================================================================
// 2. ПАЛИТРА SCI-FI ТЕРМИНАЛА (RGB565)
// ============================================================================
uint16_t COL_BG      = 0x0041; // #02080D (Глубокий угольно-синий)
uint16_t COL_CYAN    = 0x073F; // #00E5FF (Ледяной циан)
uint16_t COL_DIM     = 0x0A4B; // #0A4A58 (Технический бирюзовый для рельефа/сеток)
uint16_t COL_AMBER   = 0xFB60; // #FF6B00 (Янтарный акцент целей и фронтов)

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

enum MainMode {
  MODE_INDOOR = 0,
  MODE_METEO  = 1,
  MODE_SPACE  = 2,
  MODE_AIR    = 3,
  MODE_MATRIX = 4,
  MODE_MENU   = 5
};

const char* MODE_TITLES[6] = {
  "01 // INDOOR LAB",
  "02 // METEO MAP 100KM",
  "03 // SPACE & ORBIT",
  "04 // SKY TRAFFIC RADAR",
  "05 // PLANETARY MATRIX",
  "06 // SYSTEM & TERMINAL"
};

// ============================================================================
// 3. СТРУКТУРЫ КОНФИГА, ВЕКТОРНОЙ КАРТЫ И ПРОСТРАНСТВЕННОЙ СЕТКИ ПОГОДЫ
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

// Векторный сегмент карты (координаты в диапазоне -75..+75 по X и -60..+60 по Y от центра карты)
struct VecSegment {
  int8_t  x1, y1, x2, y2;
  uint8_t type; // 0 = гидрология (реки/берега), 1 = магистрали, 2 = разломы/хребты
};

#define MAX_VEC_SEGMENTS 28
VecSegment activeMap[MAX_VEC_SEGMENTS];
uint8_t    activeMapCount = 0;

// Цифровая модель рельефа 9x7 (высоты в сотнях метров над уровнем моря для построения изогипс)
// Отражает реальную топографию 100х100 км (на юге горный хребет до 3800 м, на севере равнина и капчагайская котловина 500 м)
const float TERRAIN_LOCAL[7][9] = {
  { 5.2f,  5.0f,  4.8f,  4.7f,  4.6f,  4.8f,  5.1f,  5.5f,  6.0f}, // Север (+50 км)
  { 5.8f,  5.5f,  5.3f,  5.1f,  5.0f,  5.3f,  5.8f,  6.4f,  7.2f},
  { 6.5f,  6.2f,  6.0f,  5.9f,  6.1f,  6.5f,  7.2f,  8.1f,  9.5f},
  { 8.0f,  7.8f,  7.5f,  7.8f,  8.2f,  9.0f, 10.5f, 12.0f, 14.0f}, // Центр (0 км — станция)
  {14.0f, 15.5f, 16.8f, 18.5f, 19.5f, 21.0f, 22.5f, 23.0f, 21.0f},
  {24.0f, 27.5f, 31.0f, 34.5f, 36.0f, 35.0f, 33.0f, 30.0f, 27.0f},
  {29.0f, 33.0f, 37.5f, 41.0f, 43.5f, 42.0f, 39.0f, 35.0f, 31.0f}  // Юг (-50 км — высокогорье)
};

const float TERRAIN_NEIGHBOR[7][9] = {
  {12.0f, 14.0f, 15.0f, 13.0f, 10.0f,  8.0f,  7.5f,  8.0f,  9.0f},
  {15.0f, 19.0f, 21.0f, 17.0f, 12.0f,  9.0f,  8.0f,  8.5f, 10.0f},
  {14.0f, 18.0f, 22.0f, 19.0f, 13.0f,  9.5f,  8.2f,  9.0f, 11.5f},
  {11.0f, 13.5f, 16.0f, 14.0f, 10.5f,  8.5f,  7.8f,  9.5f, 13.0f},
  { 9.0f, 10.0f, 11.0f, 10.0f,  8.5f,  7.5f,  8.0f, 11.0f, 16.0f},
  { 8.0f,  8.5f,  9.0f,  8.5f,  7.8f,  7.2f,  9.0f, 14.0f, 20.0f},
  { 7.5f,  7.8f,  8.0f,  7.5f,  7.0f,  7.5f, 10.5f, 16.5f, 23.0f}
};

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
  bool     seismoNightGain = false; // Ручное/авто переключение чувствительности сейсмографа
  bool     sdMounted       = false;
  bool     configLoaded    = false;
  bool     cacheLoaded     = false;
  uint32_t sdWriteCount    = 0;
  int      chatMsgId       = 104;
  String   chatSender      = "ALMATY-CORE";
  String   chatText        = "All systems nominal. Spatial weather map & orbital engine online.";
  uint8_t  presetIdx       = 0;
} sys;

const char* PAGER_PRESETS[4] = {
  "Nominal telemetry. Sky clear over station, wind calm.",
  "ADS-B radar target locked in 100km sector! Check Mode 4.",
  "ISS orbital pass in progress overhead! Check Mode 3.",
  "Solar geomagnetic Kp spike detected! Watch isotherms."
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
  float       az_start;   // Азимут восхода
  float       az_end;     // Азимут захода
  float       max_el;     // Максимальная элевация пролета
  float       phase;      // 0.0 .. 1.0 (текущая фаза 10-минутного пролета)
  float       speed_rate; // Реальная доля дуги в секунду (~1/600 с)
};

struct TelemetryData {
  // Центральная точка станции
  float tempNow       = 19.2f;
  float tempPlus3h    = 16.0f;
  int   humidity      = 45;
  float dewPoint      = 6.9f;
  float pressureHpa   = 1014.5f;
  float windSpeed     = 4.5f;
  int   windDir       = 235;
  float windSpeed3h   = 7.2f;
  int   windDir3h     = 265;
  float precipMm      = 0.0f;
  float precip3h      = 1.6f;

  // Пространственная сетка 5x5 по квадрату 100х100 км (0 = Сейчас, 1 = Прогноз +3ч)
  float gridTemp[2][5][5];
  float gridWindSpd[2][5][5];
  float gridWindDir[2][5][5];
  float gridPrecip[2][5][5];
  float gridPress[2][5][5];

  float mcuTemp       = 42.0f;
  float seismoWave[60];
  float baroHistory[24];
  float maxQuakeMag   = 2.9f;

  // Космос
  float issLat        = 41.8f;
  float issLon        = 74.2f;
  float issAlt        = 418.5f;
  float issVel        = 27580.0f;
  float kpIndex       = 3.3f;
  int   solarWind     = 445;
  int   moonPhasePct  = 78;
  SatObject sats[3];

  // Авиация
  Aircraft planes[8];
  int      planeCount = 5;
} data;

// ============================================================================
// 4. ГЕНЕРАТИВНЫЙ LO-FI / SCI-FI АУДИО-СИНТЕЗАТОР (НА ЯДРЕ 0 FREERTOS)
// ============================================================================
volatile uint32_t sfxReqFreq1 = 0;
volatile uint32_t sfxReqDur1  = 0;
volatile uint32_t sfxReqFreq2 = 0;
volatile uint32_t sfxReqDur2  = 0;

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

// Неблокирующий заказ SFX-отклика кнопки
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

// 4 пентатонические гаммы (меняются каждые 5 минут согласно ТЗ)
const uint16_t AMBIENT_SCALES[4][6] = {
  {196, 220, 262, 294, 330, 392}, // Пентатоника A minor (спокойная)
  {175, 220, 262, 294, 349, 440}, // Пентатоника F major Lydian (космос)
  {165, 196, 220, 247, 294, 330}, // Пентатоника E minor (ветер/погода)
  {147, 175, 220, 262, 294, 349}  // Пентатоника D minor (глубокий эмбиент)
};

// Фоновая задача процедурного эмбиента на Ядре 0
void audioAmbientTask(void* pvParameters) {
  uint32_t lastStepTime = 0;
  uint32_t stepInterval = 900;
  uint8_t  noteIdx      = 0;

  for (;;) {
    // 1. Приоритетное воспроизведение коротких SFX кликов интерфейса
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
      lastStepTime = millis();
    }

    // 2. Генеративная фоновая подложка (Data-Driven Sonification)
    uint32_t now = millis();
    if (sys.audioMode == AUDIO_AMBIENT_AND_SFX && (now - lastStepTime >= stepInterval)) {
      lastStepTime = now;

      // Каждые 5 минут (300 000 мс) плавно сдвигается корневая гамма
      uint8_t scaleIdx = ((now / 300000UL) + sys.mainMode) % 4;

      // Выбор ноты зависит от реальных показателей телеметрии (ветер, Kp, сейсмика)
      uint32_t seed = (uint32_t)(data.windSpeed * 10) + (uint32_t)(data.kpIndex * 10) + (now / 400);
      noteIdx = (noteIdx + (seed % 3) + 1) % 6;
      uint16_t baseFreq = AMBIENT_SCALES[scaleIdx][noteIdx];

      // Тематический характер под каждый режим
      uint32_t noteDur = 45;
      if (sys.mainMode == MODE_METEO) {
        // В режиме погоды темп зависит от скорости ветра, мягкие переливы
        stepInterval = constrain((int)(1100 - data.windSpeed * 55), 380, 1100);
        noteDur = 55;
      } else if (sys.mainMode == MODE_SPACE) {
        // В режиме космоса редкие глубокие квинты и октавы
        stepInterval = 1350;
        if ((seed % 4) == 0) baseFreq *= 2;
        noteDur = 75;
      } else if (sys.mainMode == MODE_AIR) {
        // В режиме авиарадара мягкий пеленгационный пульс
        stepInterval = 1000;
        noteDur = 35;
      } else {
        // Спокойный настольный Lo-Fi пульс
        stepInterval = 950 + (seed % 350);
        noteDur = 45;
      }

      // Мягкая двухфазная огибающая (attack + release) без резкого щелчка
      pwmTone(baseFreq);
      vTaskDelay(pdMS_TO_TICKS(noteDur));
      if ( (seed % 3) == 0 ) {
        // Иногда добавляем мягкое эхо на квинту выше
        pwmTone(AMBIENT_SCALES[scaleIdx][(noteIdx + 2) % 6]);
        vTaskDelay(pdMS_TO_TICKS(noteDur / 2));
      }
      pwmTone(0);
    }

    vTaskDelay(pdMS_TO_TICKS(15));
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
  } else {
    pwmBacklight(255);
    COL_BG    = 0x0041;
    COL_CYAN  = 0x073F;
    COL_DIM   = 0x0A4B;
    COL_AMBER = 0xFB60;
  }
}

// ============================================================================
// 5. ПОСТРОЕНИЕ ПРОСТРАНСТВЕННОЙ СЕТКИ ПОГОДЫ 5x5 ПО РЕЛЬЕФУ И ДАННЫМ API
// ============================================================================
float getTerrainElev(int gx, int gy) {
  gx = constrain(gx, 0, 8);
  gy = constrain(gy, 0, 6);
  return sys.isRemoteSrc ? TERRAIN_NEIGHBOR[gy][gx] : TERRAIN_LOCAL[gy][gx];
}

// Пересчет честного 2D-поля температуры, ветра, давления и осадков по квадрату 100х100 км
// Учитывает высотный градиент (-0.65 C на 100 м высоты рельефа), горно-долинную циркуляцию и фронты
void rebuildSpatialWeatherGrid() {
  float baseT[2]   = {data.tempNow,   data.tempPlus3h};
  float baseW[2]   = {data.windSpeed, data.windSpeed3h};
  float baseDir[2] = {(float)data.windDir, (float)data.windDir3h};
  float baseP[2]   = {data.precipMm,  data.precip3h};

  float centerElev = getTerrainElev(4, 3); // Высота в центре (в сотнях метров)

  for (int t = 0; t < 2; t++) {
    for (int r = 0; r < 5; r++) {
      for (int c = 0; c < 5; c++) {
        // Сопоставляем узлы 5x5 с матрицей рельефа 9x7
        float elev = getTerrainElev(c * 2, (r * 6) / 4);
        float dElevHundredM = elev - centerElev;

        // 1. Температура падает с высотой рельефа (-0.65°C на каждые 100 м) + широтный градиент с севера на юг
        float latGrad = (2 - r) * 0.45f;
        data.gridTemp[t][r][c] = baseT[t] - (dElevHundredM * 0.65f) + latGrad;

        // 2. Направление ветра огибает горные склоны (орографическая дефлексия) + поворот фронта
        float slopeX = getTerrainElev(min(8, c * 2 + 1), (r * 6) / 4) - getTerrainElev(max(0, c * 2 - 1), (r * 6) / 4);
        float slopeY = getTerrainElev(c * 2, min(6, (r * 6) / 4 + 1)) - getTerrainElev(c * 2, max(0, (r * 6) / 4 - 1));
        float deflect = (slopeX * 3.5f) - (slopeY * 2.2f) + (c - 2) * 8.0f + (r - 2) * 5.0f;
        data.gridWindDir[t][r][c] = fmod(baseDir[t] + deflect + 360.0f, 360.0f);

        // 3. Скорость ветра усиливается в предгорьях/перевалах и на открытой северной равнине
        float spdFactor = 1.0f + (abs(dElevHundredM) * 0.025f) + ((2 - r) * 0.08f);
        data.gridWindSpd[t][r][c] = max(0.5f, baseW[t] * spdFactor);

        // 4. Осадки конденсируются вдоль наветренных склонов и фронтальной полосы
        float orographicRain = max(0.0f, (elev - 10.0f) * 0.12f);
        float frontZone = (t == 1) ? max(0.0f, 1.8f - abs(c - 1 - r) * 0.6f) : max(0.0f, 1.2f - abs(c - 3) * 0.5f);
        data.gridPrecip[t][r][c] = max(0.0f, baseP[t] + orographicRain + frontZone);

        // 5. Барическое поле (приведенное давление с градиентом ложбины)
        data.gridPress[t][r][c] = data.pressureHpa + (r - 2) * 1.4f - (c - 2) * 1.1f - (t * 1.5f);
      }
    }
  }
}

// Билинейная интерполяция любого 5x5 поля в произвольной точке карты (u, v в диапазоне 0.0 .. 1.0)
float sampleGrid5x5(const float grid[5][5], float u, float v) {
  float gx = constrain(u * 4.0f, 0.0f, 3.999f);
  float gy = constrain(v * 4.0f, 0.0f, 3.999f);
  int x0 = (int)gx;
  int y0 = (int)gy;
  float fx = gx - x0;
  float fy = gy - y0;

  float v00 = grid[y0][x0];
  float v10 = grid[y0][x0 + 1];
  float v01 = grid[y0 + 1][x0];
  float v11 = grid[y0 + 1][x0 + 1];

  float top = v00 + fx * (v10 - v00);
  float bot = v01 + fx * (v11 - v01);
  return top + fy * (bot - top);
}

// ============================================================================
// 6. РАБОТА С ФАЙЛОВОЙ СИСТЕМОЙ MICRO-SD (/config.json, /maps, /cache)
// ============================================================================
bool loadVectorMapFromSD(const char* path) {
  if (!sys.sdMounted || !SD.exists(path)) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  uint8_t count = 0;
  if (f.read(&count, 1) == 1 && count <= MAX_VEC_SEGMENTS) {
    f.read((uint8_t*)activeMap, count * sizeof(VecSegment));
    activeMapCount = count;
  }
  f.close();
  return (activeMapCount > 0);
}

// Заполнение векторных слоев рек, озер и магистралей 100х100 км
void loadDefaultVectorSegments(bool remote) {
  if (!remote) {
    // Топография 100х100 км: южная береговая линия Капчагая, реки Или / Б.Алматинка / Каскелен и трассы А-2 / А-3 / БАКАД
    VecSegment localSegs[16] = {
      // Береговая линия водохранилища и р. Или на севере карты
      {-70, -46, -30, -42, 0}, {-30, -42,  10, -48, 0}, { 10, -48,  55, -44, 0},
      {  5, -46,  28, -36, 0}, { 28, -36,  62, -38, 0}, { 62, -38,  72, -43, 0},
      // Горные реки, текущие с южных хребтов на север
      {-42,  52, -36,  10, 0}, {-36,  10, -32, -42, 0},
      { -4,  54,  -2,   4, 0}, { -2,   4,   6, -46, 0},
      { 34,  50,  28,  12, 0}, { 28,  12,  25, -36, 0},
      // Основные автомагистрали (Запад-Восток и Север-Юг)
      {-72,   4,  -8,   2, 1}, { -8,   2,  72,  -6, 1},
      { -2,   2,   8, -55, 1}, {-26,   8,  24,   8, 1}
    };
    memcpy(activeMap, localSegs, sizeof(localSegs));
    activeMapCount = 16;
  } else {
    VecSegment remSegs[10] = {
      {-68, -35, -15,  -5, 0}, {-15,  -5,  25,  15, 0}, { 25,  15,  70,  42, 0},
      {-50,  48,  -5,   2, 0}, { -5,   2,  45, -45, 0},
      {-70,  12,  70, -14, 1}, {-12, -55,  14,  55, 1},
      {-35, -20,  35, -20, 1}, {-35,  25,  35,  25, 1}, {-20, -35, -20,  35, 1}
    };
    memcpy(activeMap, remSegs, sizeof(remSegs));
    activeMapCount = 10;
  }
}

void createDefaultVectorMapsOnSD() {
  if (!sys.sdMounted) return;
  loadDefaultVectorSegments(false);
  File f1 = SD.open("/maps/local_100km.vec", FILE_WRITE);
  if (f1) {
    f1.write(&activeMapCount, 1);
    f1.write((uint8_t*)activeMap, activeMapCount * sizeof(VecSegment));
    f1.close();
    sys.sdWriteCount++;
  }
  loadDefaultVectorSegments(true);
  File f2 = SD.open("/maps/neighbor_alpha.vec", FILE_WRITE);
  if (f2) {
    f2.write(&activeMapCount, 1);
    f2.write((uint8_t*)activeMap, activeMapCount * sizeof(VecSegment));
    f2.close();
    sys.sdWriteCount++;
  }
  loadDefaultVectorSegments(sys.isRemoteSrc);
}

void saveWeatherCacheToSD() {
  if (!sys.sdMounted) return;
  JsonDocument doc;
  doc["ts"]         = (uint32_t)(millis() / 1000);
  doc["temp_now"]   = data.tempNow;
  doc["temp_3h"]    = data.tempPlus3h;
  doc["hum"]        = data.humidity;
  doc["dew"]        = data.dewPoint;
  doc["press"]      = data.pressureHpa;
  doc["wind_spd"]   = data.windSpeed;
  doc["wind_dir"]   = data.windDir;
  doc["wind_3h"]    = data.windSpeed3h;
  doc["precip_now"] = data.precipMm;
  doc["precip_3h"]  = data.precip3h;

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

  // Поддерживаем как /config.json, так и /config.txt
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
  } else {
    JsonDocument defCfg;
    defCfg["device_id"]               = cfg.deviceId;
    defCfg["device_name"]             = cfg.deviceName;
    defCfg["wifi_ssid"]               = cfg.wifiSsid;
    defCfg["wifi_pass"]               = cfg.wifiPass;
    defCfg["home_coords"]["lat"]      = cfg.homeLat;
    defCfg["home_coords"]["lon"]      = cfg.homeLon;
    defCfg["home_coords"]["alt_m"]    = cfg.homeAlt;
    defCfg["sync_interval_sec"]       = cfg.syncIntervalSec;
    defCfg["filament_brightness_pct"] = cfg.filamentBrightPct;
    JsonArray nets = defCfg["networks"].to<JsonArray>();
    nets.add("https://script.google.com/macros/s/AKfycbx_NETWORK_1_ID/exec");

    File f = SD.open("/config.json", FILE_WRITE);
    if (f) {
      serializeJsonPretty(defCfg, f);
      f.close();
      sys.sdWriteCount++;
    }
  }

  createDefaultVectorMapsOnSD();

  if (SD.exists("/cache/weather_grid.json")) {
    File f = SD.open("/cache/weather_grid.json", FILE_READ);
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        data.tempNow     = doc["temp_now"]   | data.tempNow;
        data.tempPlus3h  = doc["temp_3h"]    | data.tempPlus3h;
        data.humidity    = doc["hum"]        | data.humidity;
        data.dewPoint    = doc["dew"]        | data.dewPoint;
        data.pressureHpa = doc["press"]      | data.pressureHpa;
        data.windSpeed   = doc["wind_spd"]   | data.windSpeed;
        data.windDir     = doc["wind_dir"]   | data.windDir;
        data.windSpeed3h = doc["wind_3h"]    | data.windSpeed3h;
        data.precipMm    = doc["precip_now"] | data.precipMm;
        data.precip3h    = doc["precip_3h"]  | data.precip3h;
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

  rebuildSpatialWeatherGrid();
}

// ============================================================================
// 7. ФИЗИКА ОРБИТ, АВИАЦИИ И СЕТЕВЫЕ ЗАПРОСЫ (OPEN-METEO GRID, ISS, ADS-B)
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

  // Реалистичные параметры орбит спутников: пролет через небосвод занимает ~10 минут (600 секунд!)
  data.sats[0] = {"ISS (ZARYA)",   "ORBITAL LAB", 418.5f, 7.66f, 51.6f, 240.0f,  65.0f, 72.0f, 0.42f, 1.0f / 580.0f};
  data.sats[1] = {"STARLINK-3104", "TELECOM LEO", 540.0f, 7.59f, 53.2f, 295.0f, 115.0f, 54.0f, 0.68f, 1.0f / 640.0f};
  data.sats[2] = {"NOAA-19 WEATH", "METEO POLAR", 850.0f, 7.42f, 98.7f, 175.0f,   8.0f, 81.0f, 0.21f, 1.0f / 780.0f};

  data.planes[0] = {"KZR912", "A321", 10350, 845, 245, 26.4f,  48.0f};
  data.planes[1] = {"IQ351",  "DH8D", 6400,  510, 110, 44.8f, 132.0f};
  data.planes[2] = {"TK350",  "B77W", 11280, 910, 285, 68.5f, 225.0f};
  data.planes[3] = {"DV704",  "B738", 4200,  460,  25, 16.2f, 315.0f};
  data.planes[4] = {"AZG784", "B744", 10900, 890,  95, 84.0f, 165.0f};
  data.planeCount = 5;

  loadDefaultVectorSegments(false);
  rebuildSpatialWeatherGrid();
}

// Вызывается 10 раз в секунду (dt = 0.1 с)
void updateRealtimeDynamics() {
  data.mcuTemp = temperatureRead();

  // Сейсмограф с учетом дневного (COARSE) и ночного (HIGH-GAIN) фильтра
  for (int i = 0; i < 59; i++) data.seismoWave[i] = data.seismoWave[i + 1];
  float gain = sys.seismoNightGain ? 1.8f : 0.65f;
  float noise = (random(-100, 100) / 1000.0f) * gain;
  if (random(0, 100) > 95) noise *= 3.5f;
  data.seismoWave[59] = constrain(noise, -0.48f, 0.48f);

  // Реальная орбитальная угловая скорость МКС: 360 град / 5556 сек = 0.065 град/сек (0.0065 за 0.1 с)
  data.issLon += 0.0065f;
  if (data.issLon > 180.0f) data.issLon -= 360.0f;
  data.issLat = 51.6f * sin((data.issLon * DEG_TO_RAD) * 1.2f);

  // Реальное медленное движение спутников по небосводу (1 полный пролет = 10..13 минут)
  for (int i = 0; i < 3; i++) {
    data.sats[i].phase += data.sats[i].speed_rate * 0.1f;
    if (data.sats[i].phase > 1.0f) data.sats[i].phase -= 1.0f;
  }

  // Реалистичное смещение самолетов по их курсу и путевой скорости (800 км/ч = 0.022 км за 0.1 с)
  for (int i = 0; i < data.planeCount; i++) {
    float stepKm = (data.planes[i].spd_kmh / 36000.0f);
    float bRad   = data.planes[i].bearing_deg * DEG_TO_RAD;
    float hRad   = data.planes[i].hdg * DEG_TO_RAD;
    float px     = sin(bRad) * data.planes[i].dist_km + sin(hRad) * stepKm;
    float py     = cos(bRad) * data.planes[i].dist_km + cos(hRad) * stepKm;
    data.planes[i].dist_km     = sqrt(px * px + py * py);
    data.planes[i].bearing_deg = fmod(atan2(px, py) * RAD_TO_DEG + 360.0f, 360.0f);
    if (data.planes[i].dist_km > 98.0f) data.planes[i].dist_km = 12.0f;
  }
}

void fetchAllWebData() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.setTimeout(3500);

  // 1. Open-Meteo API
  String meteoUrl = "http://api.open-meteo.com/v1/forecast?latitude=" + String(cfg.homeLat, 4) +
                    "&longitude=" + String(cfg.homeLon, 4) +
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

  // 2. ISS Orbit API
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

  // 3. ADS-B Radar 100 km
  String adsbUrl = "https://api.adsb.lol/v2/point/" + String(cfg.homeLat, 4) + "/" + String(cfg.homeLon, 4) + "/60";
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

          String flight = p["flight"] | "UNKN";
          flight.trim();
          if (flight.length() == 0) flight = p["hex"] | "ACFT";

          data.planes[count].callsign    = flight;
          data.planes[count].type        = p["t"] | "JET";
          data.planes[count].alt_m       = (int)((p["alt_baro"] | 10000) * 0.3048f);
          data.planes[count].spd_kmh     = (int)((p["gs"] | 400.0f) * 1.852f);
          data.planes[count].hdg         = p["track"] | 0;
          data.planes[count].dist_km     = calcDistanceKm(cfg.homeLat, cfg.homeLon, pLat, pLon);
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
// 8. КАРТОГРАФИЧЕСКИЙ ДВИЖОК (MARCHING SQUARES ИЗОГИПСЫ, ИЗОТЕРМЫ, ВЕКТОРЫ)
// ============================================================================

// Отрисовка изолиний (изогипс рельефа или изотерм погоды) методом Marching Squares
void drawContourLines(int mapX, int mapY, int mapW, int mapH, bool isTerrain, int timeIdx, float level, uint16_t color) {
  const int NX = 16;
  const int NY = 12;
  float cellW = (float)mapW / NX;
  float cellH = (float)mapH / NY;

  for (int iy = 0; iy < NY; iy++) {
    float v0 = (float)iy / NY;
    float v1 = (float)(iy + 1) / NY;
    for (int ix = 0; ix < NX; ix++) {
      float u0 = (float)ix / NX;
      float u1 = (float)(ix + 1) / NX;

      float f00, f10, f11, f01;
      if (isTerrain) {
        f00 = getTerrainElev((int)(u0 * 8), (int)(v0 * 6));
        f10 = getTerrainElev((int)(u1 * 8), (int)(v0 * 6));
        f11 = getTerrainElev((int)(u1 * 8), (int)(v1 * 6));
        f01 = getTerrainElev((int)(u0 * 8), (int)(v1 * 6));
      } else {
        f00 = sampleGrid5x5(data.gridTemp[timeIdx], u0, v0);
        f10 = sampleGrid5x5(data.gridTemp[timeIdx], u1, v0);
        f11 = sampleGrid5x5(data.gridTemp[timeIdx], u1, v1);
        f01 = sampleGrid5x5(data.gridTemp[timeIdx], u0, v1);
      }

      int mask = 0;
      if (f00 >= level) mask |= 1;
      if (f10 >= level) mask |= 2;
      if (f11 >= level) mask |= 4;
      if (f01 >= level) mask |= 8;
      if (mask == 0 || mask == 15) continue;

      int xLeft  = mapX + (int)(ix * cellW);
      int xRight = mapX + (int)((ix + 1) * cellW);
      int xMid   = (xLeft + xRight) >> 1;
      int yTop   = mapY + (int)(iy * cellH);
      int yBot   = mapY + (int)((iy + 1) * cellH);
      int yMid   = (yTop + yBot) >> 1;

      switch (mask) {
        case 1: case 14: spr.drawLine(xLeft, yMid, xMid, yTop, color); break;
        case 2: case 13: spr.drawLine(xMid, yTop, xRight, yMid, color); break;
        case 4: case 11: spr.drawLine(xRight, yMid, xMid, yBot, color); break;
        case 8: case 7:  spr.drawLine(xLeft, yMid, xMid, yBot, color); break;
        case 3: case 12: spr.drawLine(xLeft, yMid, xRight, yMid, color); break;
        case 6: case 9:  spr.drawLine(xMid, yTop, xMid, yBot, color); break;
        case 5:
          spr.drawLine(xLeft, yMid, xMid, yBot, color);
          spr.drawLine(xMid, yTop, xRight, yMid, color);
          break;
        case 10:
          spr.drawLine(xLeft, yMid, xMid, yTop, color);
          spr.drawLine(xMid, yBot, xRight, yMid, color);
          break;
      }
    }
  }
}

// Базовая топографическая подложка 100х100 км (для Карты Погоды в Режиме 2 и Авиарадара в Режиме 4)
void drawBaseTopographicMap(int mapX, int mapY, int mapW, int mapH) {
  spr.drawRect(mapX, mapY, mapW, mapH, COL_CYAN);

  // Координатная сетка 25 км
  for (int i = 1; i < 4; i++) {
    int gx = mapX + (mapW * i) / 4;
    int gy = mapY + (mapH * i) / 4;
    for (int x = mapX + 2; x < mapX + mapW - 2; x += 6) spr.drawPixel(x, gy, COL_DIM);
    for (int y = mapY + 2; y < mapY + mapH - 2; y += 6) spr.drawPixel(gx, y, COL_DIM);
  }

  // 1. Изогипсы рельефа местности (высоты 700 м, 1400 м, 2400 м, 3400 м)
  float elevLevels[4] = {7.0f, 14.0f, 24.0f, 34.0f};
  for (int i = 0; i < 4; i++) {
    drawContourLines(mapX, mapY, mapW, mapH, true, 0, elevLevels[i], COL_DIM);
  }

  // 2. Реки, береговая линия и автомагистрали из .vec карты
  int cx = mapX + mapW / 2;
  int cy = mapY + mapH / 2;
  for (uint8_t i = 0; i < activeMapCount; i++) {
    int x1 = constrain(cx + activeMap[i].x1, mapX + 1, mapX + mapW - 2);
    int y1 = constrain(cy + activeMap[i].y1, mapY + 1, mapY + mapH - 2);
    int x2 = constrain(cx + activeMap[i].x2, mapX + 1, mapX + mapW - 2);
    int y2 = constrain(cy + activeMap[i].y2, mapY + 1, mapY + mapH - 2);
    spr.drawLine(x1, y1, x2, y2, COL_DIM);
  }

  // Отметка нашей станции в центре карты
  spr.drawFastHLine(cx - 3, cy, 7, COL_AMBER);
  spr.drawFastVLine(cx, cy - 3, 7, COL_AMBER);
}

// ============================================================================
// 9. СКВОЗНОЙ СТАТУС-БАР И БЕГУЩАЯ СТРОКА ПЕЙДЖЕРА (CHROME)
// ============================================================================
void drawChrome() {
  spr.fillSprite(COL_BG);

  // Верхний статус-бар (Y: 0..17) — координаты выверены, ничего не слипается
  spr.drawFastHLine(0, 18, 320, COL_CYAN);
  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString(MODE_TITLES[sys.mainMode], 4, 2, 2);

  uint16_t srcCol = sys.isRemoteSrc ? COL_AMBER : COL_CYAN;
  spr.setTextColor(srcCol, COL_BG);
  spr.drawString(sys.isRemoteSrc ? "REM:ALPHA" : "LOC:HOME", 156, 2, 2);

  spr.setTextColor(sys.sdMounted ? COL_CYAN : COL_AMBER, COL_BG);
  spr.drawString(sys.configLoaded ? "SD:CFG" : (sys.sdMounted ? "SD:OK" : "SD:NO"), 216, 2, 2);

  bool online = (WiFi.status() == WL_CONNECTED);
  spr.setTextColor(online ? COL_CYAN : COL_AMBER, COL_BG);
  spr.drawString(online ? "NET" : "SIM", 260, 2, 2);

  struct tm timeinfo;
  char timeStr[9] = "14:20";
  if (getLocalTime(&timeinfo, 2)) {
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  } else {
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu", (millis() / 60000) % 24, (millis() / 1000) % 60);
  }
  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString(timeStr, 286, 2, 2);

  // Нижняя сквозная пейджер-строка с плавной автопрокруткой (Y: 150..169)
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
    String flt = sys.seismoNightGain ? "FILTER: NIGHT (HIGH-GAIN)" : "FILTER: DAY (COARSE)";
    spr.drawString("SEISMO // " + flt, 10, 89, 2);
    int baseY = 124;
    spr.drawFastHLine(10, baseY, 298, COL_DIM);
    for (int i = 0; i < 58; i++) {
      int x1 = 10 + i * 5;
      int y1 = baseY - (int)(data.seismoWave[i] * 38.0f);
      int x2 = 10 + (i + 1) * 5;
      int y2 = baseY - (int)(data.seismoWave[i + 1] * 38.0f);
      uint16_t c = (abs(data.seismoWave[i + 1]) > 0.15f) ? COL_AMBER : COL_CYAN;
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
    spr.drawString("AUDIO: " + String(sys.audioMode == 0 ? "GEN AMBIENT" : (sys.audioMode == 1 ? "SFX ONLY" : "MUTED")), 182, 107, 2);
    spr.drawString("[HOLD: GAIN FILTER]", 182, 125, 2);
  }
}

// РЕЖИМ 2: ПОЛНОЦЕННАЯ ТОПОГРАФИЧЕСКАЯ КАРТА ПОГОДЫ 100x100 КМ
void drawMode2_Meteo() {
  int tIdx      = sys.altView[MODE_METEO] ? 1 : 0; // 0 = СЕЙЧАС, 1 = ПРОГНОЗ +3Ч
  uint8_t layer = sys.subMode[MODE_METEO] % 3;     // 0 = Ветер, 1 = Температура/Изотермы, 2 = Осадки/Изобары

  int mapX = 4, mapY = 21, mapW = 156, mapH = 126;
  drawBaseTopographicMap(mapX, mapY, mapW, mapH);

  float phase = fmod(millis() * 0.004f, 1.0f);

  if (layer == 0) {
    // СЛОЙ 0: ПРОСТРАНСТВЕННОЕ ПОЛЕ ВЕТРОВ (25 независимых векторов 5x5 по рельефу местности)
    for (int r = 0; r < 5; r++) {
      for (int c = 0; c < 5; c++) {
        int px = mapX + 16 + c * 31;
        int py = mapY + 13 + r * 25;

        float spd = data.gridWindSpd[tIdx][r][c];
        float dir = data.gridWindDir[tIdx][r][c];
        float rad = (dir - 90.0f) * DEG_TO_RAD;

        int len = constrain((int)(5.0f + spd * 1.1f), 6, 14);
        int x1  = px - (int)(cos(rad) * (len / 2));
        int y1  = py - (int)(sin(rad) * (len / 2));
        int x2  = px + (int)(cos(rad) * (len / 2));
        int y2  = py + (int)(sin(rad) * (len / 2));

        uint16_t vCol = (spd >= 6.5f) ? COL_AMBER : COL_CYAN;
        spr.drawLine(x1, y1, x2, y2, vCol);

        // Острие стрелки по локальному вектору ветра
        float head1 = rad + 2.6f;
        float head2 = rad - 2.6f;
        spr.drawLine(x2, y2, x2 + (int)(cos(head1) * 4.0f), y2 + (int)(sin(head1) * 4.0f), vCol);
        spr.drawLine(x2, y2, x2 + (int)(cos(head2) * 4.0f), y2 + (int)(sin(head2) * 4.0f), vCol);

        // Частица воздушного потока, бегущая вдоль локального вектора
        int mx = x1 + (int)((x2 - x1) * phase);
        int my = y1 + (int)((y2 - y1) * phase);
        spr.fillCircle(mx, my, 1, COL_AMBER);
      }
    }
  } else if (layer == 1) {
    // СЛОЙ 1: НАСТОЯЩИЕ ИЗОТЕРМЫ ТЕМПЕРАТУРЫ ПО МЕСТНОСТИ (без вращения!)
    for (float isoT = -6.0f; isoT <= 30.0f; isoT += 3.0f) {
      uint16_t col = (abs(isoT - data.tempNow) < 2.0f) ? COL_AMBER : COL_CYAN;
      drawContourLines(mapX, mapY, mapW, mapH, false, tIdx, isoT, col);
    }
    // Подписи реальной температуры по высотным зонам карты (Север, Центр, Южные горы)
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString(String(data.gridTemp[tIdx][0][1], 0) + "C", mapX + 12, mapY + 6, 2);
    spr.drawString(String(data.gridTemp[tIdx][0][4], 0) + "C", mapX + 118, mapY + 6, 2);
    spr.setTextColor(COL_AMBER, COL_BG);
    spr.drawString(String(data.gridTemp[tIdx][2][2], 1) + "C", mapX + 62, mapY + 54, 2);
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString(String(data.gridTemp[tIdx][4][1], 0) + "C", mapX + 12, mapY + 106, 2);
    spr.drawString(String(data.gridTemp[tIdx][4][3], 0) + "C", mapX + 108, mapY + 106, 2);
  } else {
    // СЛОЙ 2: ОСАДКИ И БАРИЧЕСКИЕ ИЗОБАРЫ
    for (int iy = 4; iy < mapH - 4; iy += 4) {
      for (int ix = 4; ix < mapW - 4; ix += 4) {
        float pr = sampleGrid5x5(data.gridPrecip[tIdx], (float)ix / mapW, (float)iy / mapH);
        if (pr > 0.3f) {
          uint16_t pCol = (pr > 1.5f) ? COL_AMBER : COL_CYAN;
          spr.drawPixel(mapX + ix, mapY + iy, pCol);
          if (pr > 1.2f) spr.drawLine(mapX + ix, mapY + iy, mapX + ix - 2, mapY + iy + 3, pCol);
        }
      }
    }
    // Линии барических изобар (hPa)
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString(String((int)data.gridPress[tIdx][0][2]) + " hPa", mapX + 48, mapY + 8, 2);
    spr.drawString(String((int)data.gridPress[tIdx][4][2]) + " hPa", mapX + 48, mapY + 106, 2);
  }

  // Правая приборная панель сводки (X: 164..316)
  spr.drawRect(164, 21, 152, 126, COL_DIM);
  const char* layerNames[3] = {"1. WIND FIELD MAP", "2. TEMP ISOTHERMS", "3. PRECIP & ISOBAR"};
  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString(layerNames[layer], 170, 24, 2);

  uint16_t badgeCol = (tIdx == 1) ? COL_AMBER : COL_CYAN;
  spr.fillRect(170, 42, 140, 16, badgeCol);
  spr.setTextColor(COL_BG, badgeCol);
  spr.drawString((tIdx == 1) ? "FORECAST +3 HOURS" : "REALTIME 100KM MAP", 176, 43, 2);

  spr.setTextColor(COL_CYAN, COL_BG);
  spr.drawString("VALLEY : " + String(data.gridTemp[tIdx][1][2], 1) + " C", 170, 62, 2);
  spr.drawString("PEAKS  : " + String(data.gridTemp[tIdx][4][2], 1) + " C", 170, 78, 2);
  spr.drawString("WIND   : " + String(data.gridWindSpd[tIdx][2][2], 1) + "m/s (" + String((int)data.gridWindDir[tIdx][2][2]) + "d)", 170, 94, 2);
  spr.drawString("PRECIP : " + String(data.gridPrecip[tIdx][2][2], 1) + " mm", 170, 110, 2);
  spr.setTextColor(COL_DIM, COL_BG);
  spr.drawString("[2x:LAYER | HOLD:+3H]", 170, 128, 2);
}

// РЕЖИМ 3: ОРБИТАЛЬНЫЙ ТРЕКЕР И КОСМОС (С РЕАЛЬНОЙ СКОРОСТЬЮ ПРОЛЕТА)
void drawMode3_Space() {
  bool tableView = sys.altView[MODE_SPACE];
  uint8_t satIdx = sys.subMode[MODE_SPACE] % 3;

  if (!tableView) {
    int cx = 68, cy = 84, r = 56;
    spr.drawRect(4, 21, 128, 126, COL_DIM);
    spr.drawCircle(cx, cy, r, COL_DIM);         // Горизонт 0°
    spr.drawCircle(cx, cy, (r * 2) / 3, COL_DIM); // Элевация 30°
    spr.drawCircle(cx, cy, r / 3, COL_DIM);     // Элевация 60°
    spr.drawFastHLine(cx - r, cy, r * 2, COL_DIM);
    spr.drawFastVLine(cx, cy - r, r * 2, COL_DIM);

    // Отрисовка траекторий пролета и реального положения спутников
    for (int i = 0; i < 3; i++) {
      SatObject &s = data.sats[i];
      float a1 = (s.az_start - 90.0f) * DEG_TO_RAD;
      float a2 = (s.az_end   - 90.0f) * DEG_TO_RAD;
      int xStart = cx + (int)(cos(a1) * r);
      int yStart = cy + (int)(sin(a1) * r);
      int xEnd   = cx + (int)(cos(a2) * r);
      int yEnd   = cy + (int)(sin(a2) * r);

      // Дуга кульминации через максимальную элевацию
      float midAz = ((s.az_start + s.az_end) * 0.5f) * DEG_TO_RAD;
      float minR  = r * (1.0f - (s.max_el / 90.0f));
      int xPeak   = cx + (int)(cos(midAz) * minR);
      int yPeak   = cy + (int)(sin(midAz) * minR);

      if (i == satIdx) {
        spr.drawLine(xStart, yStart, xPeak, yPeak, COL_DIM);
        spr.drawLine(xPeak, yPeak, xEnd, yEnd, COL_DIM);
      }

      // Интерполяция позиции вдоль дуги (фаза 0..1 проходит за ~10 минут!)
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

// РЕЖИМ 4: АВИАРАДАР 100 КМ ПОВЕРХ ТОПОГРАФИЧЕСКОЙ КАРТЫ МЕСТНОСТИ
void drawMode4_Air() {
  bool tableView = sys.altView[MODE_AIR];
  int total = max(1, data.planeCount);
  uint8_t sel = sys.subMode[MODE_AIR] % total;

  if (!tableView) {
    int mapX = 4, mapY = 21, mapW = 144, mapH = 126;
    int cx = mapX + mapW / 2;
    int cy = mapY + mapH / 2;
    int r  = 58;

    // 1. Рисуем подложку топографической карты с изогипсами рельефа, реками и дорогами!
    drawBaseTopographicMap(mapX, mapY, mapW, mapH);

    // 2. Поверх карты накладываем концентрические кольца дальности 25 / 50 / 100 км и луч
    spr.drawCircle(cx, cy, r, COL_CYAN);
    spr.drawCircle(cx, cy, r / 2, COL_DIM);
    spr.drawCircle(cx, cy, r / 4, COL_DIM);

    float sweepRad = (millis() * 0.04f) * DEG_TO_RAD;
    spr.drawLine(cx, cy, cx + (int)(cos(sweepRad) * r), cy + (int)(sin(sweepRad) * r), COL_DIM);

    // 3. Отрисовка самолетов с векторами путевой скорости
    for (int i = 0; i < data.planeCount; i++) {
      float rad = (data.planes[i].bearing_deg - 90.0f) * DEG_TO_RAD;
      float pr  = constrain(data.planes[i].dist_km / 100.0f, 0.05f, 0.95f) * r;
      int px = cx + (int)(cos(rad) * pr);
      int py = cy + (int)(sin(rad) * pr);

      // Вектор курса воздушного судна
      float hRad = (data.planes[i].hdg - 90.0f) * DEG_TO_RAD;
      int vx = px + (int)(cos(hRad) * 8.0f);
      int vy = py + (int)(sin(hRad) * 8.0f);

      uint16_t acCol = (i == sel) ? COL_AMBER : COL_CYAN;
      spr.drawLine(px, py, vx, vy, acCol);
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
    spr.drawString("DIST   : " + String(ac.dist_km, 1) + " km (" + String((int)ac.bearing_deg) + "d)", 158, 98, 2);
    spr.setTextColor(COL_DIM, COL_BG);
    spr.drawString("[2x:TARGET | HOLD:LIST]", 158, 126, 2);
  } else {
    spr.drawRect(4, 21, 312, 126, COL_DIM);
    spr.setTextColor(COL_AMBER, COL_BG);
    spr.drawString("AIRSPACE MATRIX // 100 KM ADS-B LIST", 10, 24, 2);
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
  spr.drawString("SKY RADAR", 10, 89, 2);
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

// РЕЖИМ 6: СИСТЕМНЫЙ ТЕРМИНАЛ, НАСТРОЙКА ЭМБИЕНТА И SD
void drawMode6_Menu() {
  uint8_t sel = sys.subMode[MODE_MENU] % 5;

  spr.drawRect(4, 21, 312, 126, COL_DIM);
  spr.setTextColor(COL_AMBER, COL_BG);
  spr.drawString("SYSTEM TERMINAL [2x:SELECT | HOLD:EXEC]", 10, 24, 2);
  spr.drawFastHLine(10, 42, 300, COL_CYAN);

  const char* audioLabels[3] = {"AMBIENT + SFX (CORE 0)", "CLICK SFX ONLY", "MUTED"};

  String items[5] = {
    "1. STATION SOURCE : " + String(sys.isRemoteSrc ? "NEIGHBOR (ALPHA)" : ("LOCAL (" + cfg.deviceName + ")")),
    "2. SEND PAGER MSG : PRESET #" + String(sys.presetIdx + 1) + " -> SD",
    "3. DISPLAY THEME  : " + String(sys.nightMode ? "NIGHT OBSERVATORY" : "DAYLIGHT 100%"),
    "4. AUDIO SYNTH    : " + String(audioLabels[sys.audioMode]),
    "5. RELOAD SD & API: SYNC CONFIG.JSON + METEO"
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
// 11. УПРАВЛЕНИЕ ОДНОЙ КНОПКОЙ (БЕЗ АВТОПЕРЕКЛЮЧЕНИЯ РЕЖИМОВ)
// ============================================================================
void toggleSourceAndMap() {
  sys.isRemoteSrc = !sys.isRemoteSrc;
  if (!loadVectorMapFromSD(sys.isRemoteSrc ? "/maps/neighbor_alpha.vec" : "/maps/local_100km.vec")) {
    loadDefaultVectorSegments(sys.isRemoteSrc);
  }
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
      loadConfigAndCacheFromSD();
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

  // 1. Инициализация экрана ST7789 (320x170)
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COL_BG);

  spr.setColorDepth(16);
  if (spr.createSprite(320, 170) == nullptr) {
    spr.setColorDepth(8);
    spr.createSprite(320, 170);
  }

  // 2. Надежное монтирование MicroSD через общий экземпляр SPI библиотеки TFT_eSPI (на 4 МГц)
  if (SD.begin(PIN_SD_CS, tft.getSPIinstance(), 4000000)) {
    sys.sdMounted = true;
  } else if (SD.begin(PIN_SD_CS, tft.getSPIinstance(), 1000000)) {
    sys.sdMounted = true;
  }

  if (sys.sdMounted) {
    loadConfigAndCacheFromSD();
  }

  // 3. Запуск генеративного аудио-синтезатора на Ядре 0
  xTaskCreatePinnedToCore(audioAmbientTask, "AmbientSynth", 4096, NULL, 1, NULL, 0);

  // 4. Экран загрузки с диагностикой SD и конфига
  tft.setTextColor(COL_CYAN, COL_BG);
  tft.drawString("STATION-01 // HORIZON v2.0 BOOT", 16, 16, 2);
  tft.drawString("SD CARD     : " + String(sys.sdMounted ? "MOUNTED OK (4MHz)" : "FAILED (CHECK WIRING)"), 16, 40, 2);
  tft.drawString("CONFIG.JSON : " + String(sys.configLoaded ? ("LOADED [" + cfg.deviceName + "]") : "USING DEFAULTS"), 16, 60, 2);
  tft.drawString("TERRAIN MAP : 100x100KM ISOHYPSES + VEC", 16, 80, 2);
  tft.drawString("AUDIO SYNTH : CORE 0 LO-FI AMBIENT READY", 16, 100, 2);
  tft.setTextColor(COL_AMBER, COL_BG);
  tft.drawString("WIFI CONNECT: " + cfg.wifiSsid, 16, 124, 2);
  sfxBoot();

  // 5. Подключение к Wi-Fi из config.json
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