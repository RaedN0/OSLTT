/*
  OSLTT Firmware v3.0 - Optimized for Mouse Movement + Photodiode Latency Testing
  Targets: Seeed XIAO M0 / Adafruit Feather M0 (ATSAMD21G18A)

  Optimizations:
  - 48MHz CPU with tuned wait states
  - Direct ADC register access (~3us conversion, ~300ksps effective)
  - Microsecond-precise sampling using busy-wait timing
  - Minimal protocol: configure, trigger, result
  - All non-essential modes removed (audio, click-test, pretest, DirectX, etc.)
*/

#include "Mouse.h"
#include "clangd_arduino_stubs.h"

/* ========== BOARD COMPATIBILITY ========== */
#if defined(ARDUINO_SEEED_XIAO_M0)
  const char* boardName = "XIAO";
#elif defined(ARDUINO_SAMD_ZERO) || defined(ARDUINO_FEATHER_M0)
  const char* boardName = "Feather";
#else
  const char* boardName = "SAMD21";
#endif

/* ========== PINS ========== */
const int OSLTT_PIN_LED      = 3;   // Status LED (PA11 on XIAO, PA09 on Feather)
const int OSLTT_PIN_BUTTON   = 6;   // Push button (PB08 on XIAO, PA20 on Feather)
const int OSLTT_PIN_LIGHT    = A0;  // Photodiode / light sensor (PA02 = AIN0 on both)

/* ========== CONSTANTS ========== */
#define FW_VERSION           "3.0"
#define CMD_BUFFER_SIZE      64
#define MAX_MAX_SAMPLES      12000    // Hard ceiling
#define DEFAULT_INTERVAL_US  20       // 20us = 50ksps. Reduced from 10us to avoid USB HID noise
#define DEFAULT_WINDOW_MS    40       // 40ms max sampling window (480Hz OLED optimized)
#define DEFAULT_SHOT_DELAY   100      // ms between shots
#define DEFAULT_SHOTS        100
#define DEFAULT_MOVE         100      // pixels
#define DEFAULT_THRESHOLD    0        // 0 = auto
#define BASELINE_SAMPLES     20       // Samples to establish baseline
#define CONSECUTIVE_OUT_OF_BOUNDS 5    // Consecutive out-of-bounds samples to confirm detection
#define MOUSE_RESET_DELAY    50       // ms to wait before moving mouse back

/* ========== STATE ========== */
uint16_t adcBuffer[MAX_MAX_SAMPLES];

volatile bool running         = false;
volatile bool abortRequested  = false;
volatile bool armed           = false;

uint16_t cfgShots             = DEFAULT_SHOTS;
uint16_t cfgDelayMs           = DEFAULT_SHOT_DELAY;
int8_t   cfgMoveDistance      = DEFAULT_MOVE;
uint16_t cfgThreshold         = DEFAULT_THRESHOLD;
uint16_t cfgWindowMs          = DEFAULT_WINDOW_MS;
uint16_t cfgSampleIntervalUs  = DEFAULT_INTERVAL_US;

char     cmdBuffer[CMD_BUFFER_SIZE];
uint8_t  cmdIndex = 0;

/* ========== FAST ADC (Direct Register Access) ========== */
static inline void initFastADC() {
  PM->APBCMASK.reg |= PM_APBCMASK_ADC;
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_ADC |
                      GCLK_CLKCTRL_GEN_GCLK0 |
                      GCLK_CLKCTRL_CLKEN;
  while (GCLK->STATUS.bit.SYNCBUSY);
  ADC->CTRLA.reg = ADC_CTRLA_SWRST;
  while (ADC->CTRLA.bit.SWRST || ADC->STATUS.bit.SYNCBUSY);
  ADC->REFCTRL.reg = ADC_REFCTRL_REFSEL_INTVCC1;
  ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_1;
  ADC->SAMPCTRL.reg = ADC_SAMPCTRL_SAMPLEN(0);
  ADC->CTRLB.reg = ADC_CTRLB_RESSEL_12BIT |
                   ADC_CTRLB_PRESCALER_DIV4;
  ADC->INPUTCTRL.reg = ADC_INPUTCTRL_MUXNEG_GND |
                       ADC_INPUTCTRL_MUXPOS_PIN0;
  ADC->CTRLA.reg = ADC_CTRLA_ENABLE;
  while (ADC->STATUS.bit.SYNCBUSY);
}

static inline uint16_t fastAnalogRead() {
  ADC->SWTRIG.reg = ADC_SWTRIG_START;
  while (ADC->INTFLAG.bit.RESRDY == 0);
  return ADC->RESULT.reg;
}

/* ========== LED ========== */
static inline void setLED(bool on) {
  digitalWrite(OSLTT_PIN_LED, on ? HIGH : LOW);
}

static void blinkLED(int times, int msOn, int msOff) {
  for (int i = 0; i < times; i++) {
    setLED(true);
    delay(msOn);
    setLED(false);
    if (i < times - 1) delay(msOff);
  }
}

/* ========== SERIAL HELPERS ========== */
static void sendResultHeader(uint32_t latencyUs, uint16_t latencyIndex,
                             uint16_t numSamples, uint16_t sampleIntervalUs,
                             uint16_t baseline, uint16_t actualThreshold) {
  Serial.print("R,");
  Serial.print(latencyUs);
  Serial.print(',');
  Serial.print(latencyIndex);
  Serial.print(',');
  Serial.print(sampleIntervalUs);
  Serial.print(',');
  Serial.print(numSamples);
  Serial.print(',');
  Serial.print(baseline);
  Serial.print(',');
  Serial.println(actualThreshold);
}

static void sendSamples(uint16_t count) {
  for (uint16_t i = 0; i < count; i++) {
    Serial.print(adcBuffer[i]);
    if (i + 1 < count) Serial.print(',');
  }
  Serial.println();
}

/* ========== CORE SAMPLING ROUTINE ========== */
static void computeBaseline(uint16_t &baseline, uint16_t &actualThreshold,
                            uint16_t &lowerBound, uint16_t &upperBound) {
  uint32_t baselineSum = 0;
  for (int i = 0; i < BASELINE_SAMPLES; i++) {
    baselineSum += fastAnalogRead();
    delayMicroseconds(cfgSampleIntervalUs);
  }
  baseline = (uint16_t)(baselineSum / BASELINE_SAMPLES);

  actualThreshold = cfgThreshold;
  if (actualThreshold == 0) {
    uint16_t tempThresh = (uint16_t)((baseline * 12UL) / 100);
    actualThreshold = (tempThresh > 60) ? tempThresh : (uint16_t)60;
  }
  lowerBound = (baseline > actualThreshold) ? (baseline - actualThreshold) : 0;
  uint16_t sumThresh = baseline + actualThreshold;
  upperBound = (sumThresh < 4095) ? sumThresh : (uint16_t)4095;
}

static void runSingleShot(uint16_t shotNum,
                          uint16_t baseline, uint16_t actualThreshold,
                          uint16_t lowerBound, uint16_t upperBound) {
  running = true;
  abortRequested = false;
  
  uint16_t maxSamples = (uint16_t)(((uint32_t)cfgWindowMs * 1000UL) / cfgSampleIntervalUs);
  if (maxSamples > MAX_MAX_SAMPLES) maxSamples = MAX_MAX_SAMPLES;
  
  /* --- 3. Mouse move + precise sampling --- */
  uint32_t nextTime = micros();
  Mouse.move(cfgMoveDistance, 0, 0);

  for (uint16_t i = 0; i < maxSamples; i++) {
    nextTime += cfgSampleIntervalUs;
    while (micros() < nextTime);

    adcBuffer[i] = fastAnalogRead();
  }

  /* --- 4. Reset mouse position --- */
  delay(MOUSE_RESET_DELAY);
  Mouse.move(-cfgMoveDistance, 0, 0);

  /* --- 5. Find threshold crossing (require consecutive samples to ignore outliers) --- */
  uint16_t latencyIndex = 0xFFFF;
  uint8_t consecutive = 0;
  for (uint16_t i = 0; i < maxSamples; i++) {
    uint16_t val = adcBuffer[i];
    if (val < lowerBound || val > upperBound) {
      consecutive++;
      if (consecutive >= CONSECUTIVE_OUT_OF_BOUNDS) {
        latencyIndex = i - consecutive + 1;
        break;
      }
    } else {
      consecutive = 0;
    }
  }

  /* --- 6. Stream result --- */
  if (latencyIndex != 0xFFFF) {
    uint32_t latencyUs = (uint32_t)(latencyIndex + 1) * cfgSampleIntervalUs;
    sendResultHeader(latencyUs, latencyIndex, maxSamples, cfgSampleIntervalUs,
                     baseline, actualThreshold);
    sendSamples(maxSamples);
  } else {
    Serial.println("TIMEOUT");
  }

  running = false;
}

/* ========== TEST SEQUENCE ========== */
static void runFullTest() {
  if (running) { Serial.println("ERR:busy"); return; }
  abortRequested = false;

  /* Compute baseline + auto-threshold once for the entire test */
  uint16_t baseline, actualThreshold, lowerBound, upperBound;
  computeBaseline(baseline, actualThreshold, lowerBound, upperBound);

  Serial.println("START");
  for (uint16_t s = 0; s < cfgShots; s++) {
    if (abortRequested) break;
    runSingleShot(s + 1, baseline, actualThreshold, lowerBound, upperBound);
    if (s + 1 < cfgShots && !abortRequested) {
      delay(cfgDelayMs);
    }
  }
  Serial.println("DONE");
}

/* ========== COMMAND PARSER ========== */
static int parseUInt(const char* str, uint16_t* out) {
  char* end;
  long v = strtol(str, &end, 10);
  if (end == str || v < 0 || v > 65535) return -1;
  *out = (uint16_t)v;
  return 0;
}

static int parseInt8(const char* str, int8_t* out) {
  char* end;
  long v = strtol(str, &end, 10);
  if (end == str || v < -127 || v > 127) return -1;
  *out = (int8_t)v;
  return 0;
}

static void handleConfig() {
  char* tok = strtok(cmdBuffer, ",");
  if (!tok || tok[0] != 'C') { Serial.println("ERR:bad_cmd"); return; }

  uint16_t vals[6] = {0};
  int8_t   movePx  = DEFAULT_MOVE;
  bool     hasMove = false;
  int      argIdx  = 0;

  while ((tok = strtok(NULL, ",")) != NULL && argIdx < 6) {
    if (argIdx == 2) {
      if (parseInt8(tok, &movePx) != 0) { Serial.println("ERR:movePx"); return; }
      hasMove = true;
    } else {
      if (parseUInt(tok, &vals[argIdx]) != 0) {
        Serial.print("ERR:arg"); Serial.println(argIdx); return;
      }
    }
    argIdx++;
  }

  if (argIdx < 5) { Serial.println("ERR:too_few"); return; }

  cfgShots            = vals[0] ? vals[0] : DEFAULT_SHOTS;
  cfgDelayMs          = vals[1];
  if (hasMove) cfgMoveDistance = movePx;
  cfgThreshold        = vals[3];
  cfgWindowMs         = vals[4] ? vals[4] : DEFAULT_WINDOW_MS;
  if (argIdx >= 6 && vals[5] >= 5 && vals[5] <= 100) cfgSampleIntervalUs = vals[5];

  uint16_t maxSamp = (uint16_t)(((uint32_t)cfgWindowMs * 1000UL) / cfgSampleIntervalUs);
  if (maxSamp > MAX_MAX_SAMPLES) {
    cfgWindowMs = (uint16_t)((uint32_t)MAX_MAX_SAMPLES * cfgSampleIntervalUs / 1000UL);
  }

  Serial.println("OK");
}

static void processCommand() {
  if (cmdIndex == 0) return;
  cmdBuffer[cmdIndex] = '\0';

  switch (cmdBuffer[0]) {
    case 'V':
      Serial.print("FW:");
      Serial.println(FW_VERSION);
      break;

    case 'C':
      handleConfig();
      break;

    case 'S': {
      abortRequested = false;
      if (!running) {
        uint16_t baseline, actualThreshold, lowerBound, upperBound;
        computeBaseline(baseline, actualThreshold, lowerBound, upperBound);
        runSingleShot(0, baseline, actualThreshold, lowerBound, upperBound);
      }
      else Serial.println("ERR:busy");
      break;
    }

    case 'A': {
      if (running) { Serial.println("ERR:busy"); break; }
      armed = true;
      Serial.println("ARMED");
      break;
    }

    case 'D': {
      armed = false;
      Serial.println("DISARMED");
      break;
    }

    case 'T': {
      abortRequested = false;
      runFullTest();
      break;
    }

    case 'X':
      abortRequested = true;
      armed = false;
      Serial.println("ABORTED");
      break;

    default:
      Serial.println("ERR:unknown");
      break;
  }

  cmdIndex = 0;
}

/* ========== SETUP / LOOP ========== */
void setup() {
  pinMode(OSLTT_PIN_LED, OUTPUT);
  pinMode(OSLTT_PIN_BUTTON, INPUT_PULLUP);

  initFastADC();

  Serial.begin(2000000);
  while (!Serial && millis() < 3000);

  blinkLED(2, 80, 80);

  Serial.println("OSLTT");
  Serial.print("BOARD:");
  Serial.println(boardName);
  Serial.print("FW:");
  Serial.println(FW_VERSION);
}

void loop() {
  // ---- Serial input ----
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdIndex > 0) {
        processCommand();
      }
    } else if (cmdIndex < CMD_BUFFER_SIZE - 1) {
      cmdBuffer[cmdIndex++] = c;
    }
  }

  // ---- Physical button handler ----
  static uint8_t btnState = 0;
  static uint32_t btnTimer = 0;
  const uint32_t DEBOUNCE_MS = 50;
  const uint32_t COOLDOWN_MS = 300;

  bool btnLow = (digitalRead(OSLTT_PIN_BUTTON) == LOW);

  if (btnState == 0) {
    if (btnLow) {
      btnState = 1;
      btnTimer = millis();
    }
  } else if (btnState == 1) {
    if (!btnLow && (millis() - btnTimer) >= DEBOUNCE_MS) {
      if (!running) {
        if (armed) {
          armed = false;
          runFullTest();
        } else {
          uint16_t baseline, actualThreshold, lowerBound, upperBound;
          computeBaseline(baseline, actualThreshold, lowerBound, upperBound);
          runSingleShot(0, baseline, actualThreshold, lowerBound, upperBound);
        }
      }
      btnState = 2;
      btnTimer = millis();
    } else if (!btnLow && (millis() - btnTimer) < DEBOUNCE_MS) {
      btnState = 0;
    } else if (btnLow && (millis() - btnTimer) > 3000) {
      btnState = 0;
    }
  } else if (btnState == 2) {
    if ((millis() - btnTimer) >= COOLDOWN_MS) {
      btnState = 0;
    }
  }
}
