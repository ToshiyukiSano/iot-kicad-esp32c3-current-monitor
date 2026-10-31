/*
  current_monitor.ino  —  AC current monitor for the current-monitor PCB
  ---------------------------------------------------------------------
  Board : Seeed Studio XIAO ESP32C3   (Arduino IDE, board "XIAO_ESP32C3")
  Core  : Arduino-ESP32 3.x (ESP-IDF 5.x drivers are used directly)
  Input : A1 = GPIO3 = ADC1 channel 3
  Sensor: SR-3704-150N split-core CT, ratio 3000:1, burden R1 = 10 ohm
  Front : bias 1.65 V (10k/10k from 3V3), R4 4.7k + C2 100n low-pass (fc ~ 339 Hz)

  Sampling
    SAMPLER_MODE 0 (default): ESP-IDF ADC continuous (DMA) driver.  The ADC is paced by
      hardware at 20 kHz and every 10 conversions are averaged -> 2 kHz samples.
      The 10x boxcar average is an extra digital anti-alias filter (nulls at 2, 4, ... kHz)
      and lowers white noise by ~sqrt(10).  No per-sample interrupts, so Wi-Fi added later
      cannot disturb the sample timing.
    SAMPLER_MODE 1 (comparison): hardware timer at 2 kHz + analogReadMilliVolts() in a task.
      Prints measured sample-interval statistics so you can see whether polling keeps up.

  Every 200 ms (= 400 samples = 10 cycles @50 Hz / 12 cycles @60 Hz):
    mean (ADC offset, removed), AC RMS -> primary current (A).
  Output: CSV on Serial (115200).  Type "help" in the Serial Monitor for commands.

  Wi-Fi: measurements are delivered through a FreeRTOS queue to loop().  Put network code in
  publishMeasurement() — it runs in loop(), never in the sampling task.
*/

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#define SAMPLER_MODE 0            // 0 = ADC continuous/DMA (recommended), 1 = timer + analogReadMilliVolts

#if SAMPLER_MODE == 0
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#endif

// ----------------------------------------------------------------- configuration
static const uint8_t  ADC_GPIO       = 3;        // XIAO ESP32C3 A1 / D1
static const uint32_t FS_HZ          = 2000;     // output sample rate
static const uint32_t OVERSAMPLE     = 10;       // mode 0: ADC runs at FS_HZ * OVERSAMPLE
static const uint32_t WINDOW_MS      = 200;
static const uint32_t N_WIN          = FS_HZ * WINDOW_MS / 1000;   // 400 samples / window
static const float    CT_RATIO       = 3000.0f;
static const float    R_BURDEN       = 10.0f;    // ohm (R1)
static const float    R_FILTER       = 4700.0f;  // R4
static const float    C_FILTER       = 100e-9f;  // C2
static const float    ADC_LOW_MV     = 100.0f;   // clip warning thresholds (ATTEN 12 dB range ~0..2500 mV)
static const float    ADC_HIGH_MV    = 2400.0f;

// ----------------------------------------------------------------- settings (saved in NVS)
struct Settings {
  float gain    = 1.0f;    // calibration gain applied after theoretical scaling
  float noiseA  = 0.0f;    // zero-current noise (A rms) used for quadrature subtraction
  float mainsHz = 50.0f;   // used for the RC-filter amplitude compensation
};
static Settings cfg;
static Preferences prefs;

// ----------------------------------------------------------------- measurement record
struct Measurement {
  uint32_t t_ms;          // millis() at end of window
  uint32_t win;           // window counter
  float    irmsA;         // calibrated, noise-corrected primary current
  float    irmsRawA;      // calibrated, without noise correction
  float    meanMv;        // ADC mean (offset) in mV
  float    vrmsMv;        // AC rms at the ADC pin in mV
  float    vminMv, vmaxMv;
  uint32_t n;             // samples in window
  float    fsHz;          // measured sample rate over the window
  uint32_t overflows;     // mode 0: DMA pool overflows since start; mode 1: missed ticks
  float    dtMinUs, dtMaxUs, dtMeanUs;   // mode 1 only (0 in mode 0)
  bool     clip;
};
static QueueHandle_t measQueue;
static volatile uint32_t queueDrops = 0;

// ----------------------------------------------------------------- window accumulator
struct Accum {
  double s = 0, ss = 0; float vmin = 1e9f, vmax = -1e9f; uint32_t n = 0;
  int64_t tStartUs = 0;
  double dtSum = 0; float dtMin = 1e9f, dtMax = 0; uint32_t dtN = 0; int64_t lastUs = 0;
  void reset(int64_t now) { s = ss = 0; vmin = 1e9f; vmax = -1e9f; n = 0; tStartUs = now;
                            dtSum = 0; dtMin = 1e9f; dtMax = 0; dtN = 0; }
};

static float filterGain(float f) {           // |H| of R4/C2 low-pass (x 10-point boxcar in mode 0) at f
  const float fc = 1.0f / (2.0f * PI * R_FILTER * C_FILTER);
  float g = 1.0f / sqrtf(1.0f + (f / fc) * (f / fc));
#if SAMPLER_MODE == 0
  const float fadc = (float)(FS_HZ * OVERSAMPLE);
  g *= sinf(PI * f * OVERSAMPLE / fadc) / (OVERSAMPLE * sinf(PI * f / fadc));   // 0.9994 @50 Hz
#endif
  return g;
}

static volatile uint32_t overflowCount = 0;
static uint32_t winCounter = 0;

static void finishWindow(Accum &a, int64_t nowUs) {
  Measurement m{};
  double mean = a.s / a.n;
  double var  = a.ss / a.n - mean * mean;
  if (var < 0) var = 0;
  m.t_ms   = millis();
  m.win    = winCounter++;
  m.meanMv = (float)mean;
  m.vrmsMv = (float)sqrt(var);
  m.vminMv = a.vmin; m.vmaxMv = a.vmax;
  m.n      = a.n;
  m.fsHz   = (float)(a.n * 1e6 / (double)(nowUs - a.tStartUs));
  m.overflows = overflowCount;
  if (a.dtN) { m.dtMinUs = a.dtMin; m.dtMaxUs = a.dtMax; m.dtMeanUs = (float)(a.dtSum / a.dtN); }
  m.clip   = (a.vmin < ADC_LOW_MV) || (a.vmax > ADC_HIGH_MV);
  // V at ADC -> V across burden (undo RC attenuation) -> secondary I -> primary I
  float iPrim = (m.vrmsMv / 1000.0f) / filterGain(cfg.mainsHz) / R_BURDEN * CT_RATIO;
  m.irmsRawA = iPrim * cfg.gain;
  float c2 = m.irmsRawA * m.irmsRawA - cfg.noiseA * cfg.noiseA;
  m.irmsA = c2 > 0 ? sqrtf(c2) : 0.0f;
  if (xQueueSend(measQueue, &m, 0) != pdTRUE) queueDrops++;
  a.reset(nowUs);
}

static inline void addSample(Accum &a, float mv) {
  a.s += mv; a.ss += (double)mv * mv; a.n++;
  if (mv < a.vmin) a.vmin = mv;
  if (mv > a.vmax) a.vmax = mv;
}

// ================================================================= sampler, mode 0
#if SAMPLER_MODE == 0
static adc_continuous_handle_t adcHandle;
static adc_cali_handle_t caliHandle = nullptr;
static adc_channel_t adcChannel;
static const uint32_t FRAME_SAMPLES = 400;                         // 20 ms of 20 kHz conversions
static const uint32_t FRAME_BYTES   = FRAME_SAMPLES * SOC_ADC_DIGI_RESULT_BYTES;

static bool IRAM_ATTR onPoolOvf(adc_continuous_handle_t, const adc_continuous_evt_data_t *, void *) {
  overflowCount++;
  return false;
}

static float rawToMv(int raw) {
  int mv;
  if (caliHandle && adc_cali_raw_to_voltage(caliHandle, raw, &mv) == ESP_OK) return (float)mv;
  return raw * 2500.0f / 4095.0f;                                   // uncalibrated fallback
}

static void samplerTask(void *) {
  static uint8_t buf[FRAME_BYTES];
  Accum acc; acc.reset(esp_timer_get_time());
  uint32_t decimSum = 0, decimN = 0;
  for (;;) {
    uint32_t got = 0;
    esp_err_t e = adc_continuous_read(adcHandle, buf, FRAME_BYTES, &got, 100);
    if (e != ESP_OK) continue;                                     // ESP_ERR_TIMEOUT: retry
    for (uint32_t i = 0; i < got; i += SOC_ADC_DIGI_RESULT_BYTES) {
      adc_digi_output_data_t *p = (adc_digi_output_data_t *)&buf[i];
      if (p->type2.channel != adcChannel) continue;
      decimSum += p->type2.data;
      if (++decimN == OVERSAMPLE) {
        // average raw first, then calibrate once per 2 kHz sample
        float mv = rawToMv((int)((decimSum + OVERSAMPLE / 2) / OVERSAMPLE));
        decimSum = decimN = 0;
        addSample(acc, mv);
        if (acc.n >= N_WIN) finishWindow(acc, esp_timer_get_time());
      }
    }
  }
}

static bool startSampler() {
  adc_unit_t unit;
  if (adc_continuous_io_to_channel(ADC_GPIO, &unit, &adcChannel) != ESP_OK || unit != ADC_UNIT_1) {
    Serial.println("# ERROR: GPIO3 is not an ADC1 channel"); return false;
  }
  adc_continuous_handle_cfg_t hc = {};
  hc.max_store_buf_size = FRAME_BYTES * 4;
  hc.conv_frame_size    = FRAME_BYTES;
  if (adc_continuous_new_handle(&hc, &adcHandle) != ESP_OK) { Serial.println("# ERROR: adc handle"); return false; }

  adc_digi_pattern_config_t pat = {};
  pat.atten     = ADC_ATTEN_DB_12;
  pat.channel   = adcChannel;
  pat.unit      = ADC_UNIT_1;
  pat.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  adc_continuous_config_t dc = {};
  dc.pattern_num    = 1;
  dc.adc_pattern    = &pat;
  dc.sample_freq_hz = FS_HZ * OVERSAMPLE;
  dc.conv_mode      = ADC_CONV_SINGLE_UNIT_1;
  dc.format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
  if (adc_continuous_config(adcHandle, &dc) != ESP_OK) { Serial.println("# ERROR: adc config"); return false; }

  adc_continuous_evt_cbs_t cbs = {};
  cbs.on_pool_ovf = onPoolOvf;
  adc_continuous_register_event_callbacks(adcHandle, &cbs, nullptr);

  adc_cali_curve_fitting_config_t cc = {};
  cc.unit_id  = ADC_UNIT_1;
  cc.chan     = adcChannel;
  cc.atten    = ADC_ATTEN_DB_12;
  cc.bitwidth = ADC_BITWIDTH_DEFAULT;
  if (adc_cali_create_scheme_curve_fitting(&cc, &caliHandle) != ESP_OK) {
    caliHandle = nullptr;
    Serial.println("# WARNING: ADC eFuse calibration unavailable, using linear 0..2500 mV");
  }
  if (adc_continuous_start(adcHandle) != ESP_OK) { Serial.println("# ERROR: adc start"); return false; }
  xTaskCreate(samplerTask, "sampler", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr);
  Serial.printf("# sampler: ADC continuous/DMA, %lu Hz x%lu oversample -> %lu Hz, window %lu samples\n",
                (unsigned long)(FS_HZ * OVERSAMPLE), (unsigned long)OVERSAMPLE,
                (unsigned long)FS_HZ, (unsigned long)N_WIN);
  return true;
}

// ================================================================= sampler, mode 1
#else
static TaskHandle_t samplerHandle;
static hw_timer_t *tmr;
static volatile uint32_t tickCount = 0;

static void ARDUINO_ISR_ATTR onTimer() {
  tickCount++;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(samplerHandle, &woken);
  if (woken) portYIELD_FROM_ISR();
}

static void samplerTask(void *) {
  Accum acc; acc.reset(esp_timer_get_time());
  uint32_t handled = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    float mv = (float)analogReadMilliVolts(ADC_GPIO);
    handled++;
    overflowCount = tickCount - handled;                          // ticks that found the task busy
    if (acc.lastUs) {
      float dt = (float)(now - acc.lastUs);
      acc.dtSum += dt; acc.dtN++;
      if (dt < acc.dtMin) acc.dtMin = dt;
      if (dt > acc.dtMax) acc.dtMax = dt;
    }
    acc.lastUs = now;
    addSample(acc, mv);
    if (acc.n >= N_WIN) finishWindow(acc, now);
  }
}

static bool startSampler() {
  analogSetPinAttenuation(ADC_GPIO, ADC_11db);                     // 12 dB (0..~2.5 V) on ESP32-C3
  analogReadResolution(12);
  // measure the cost of one analogReadMilliVolts() call
  int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < 200; i++) (void)analogReadMilliVolts(ADC_GPIO);
  float us = (esp_timer_get_time() - t0) / 200.0f;
  Serial.printf("# analogReadMilliVolts(): %.1f us per call (budget %.0f us at %lu Hz)\n",
                us, 1e6f / FS_HZ, (unsigned long)FS_HZ);
  xTaskCreate(samplerTask, "sampler", 4096, nullptr, configMAX_PRIORITIES - 2, &samplerHandle);
  tmr = timerBegin(1000000);                                       // 1 MHz timer clock
  timerAttachInterrupt(tmr, &onTimer);
  timerAlarm(tmr, 1000000 / FS_HZ, true, 0);
  Serial.printf("# sampler: hw timer %lu Hz + analogReadMilliVolts\n", (unsigned long)FS_HZ);
  return true;
}
#endif

// ================================================================= statistics for tests
struct Stat { char label[16]; uint32_t n; double mean, sd, min, max, meanMv; };
static const int MAX_STATS = 12;
static Stat stats[MAX_STATS];
static int nStats = 0;

enum CollectMode { C_NONE, C_STAT, C_ZERO, C_CAL };
static struct {
  CollectMode mode = C_NONE; uint32_t want = 0, got = 0;
  double s = 0, ss = 0, s2raw = 0, mn = 1e9, mx = -1e9, smv = 0; float refA = 0; char label[16] = "";
} col;

static bool streamCsv = true;

static void startCollect(CollectMode m, uint32_t n, const char *label, float refA) {
  col = {}; col.mode = m; col.want = n ? n : 50; col.refA = refA;
  strncpy(col.label, label ? label : "", sizeof(col.label) - 1);
  Serial.printf("# collecting %lu windows (%.1f s)...\n", (unsigned long)col.want, col.want * WINDOW_MS / 1000.0f);
}

static void feedCollect(const Measurement &m) {
  if (col.mode == C_NONE) return;
  double v = (col.mode == C_STAT) ? m.irmsA : m.irmsRawA;
  if (col.mode == C_CAL) v = m.irmsRawA / cfg.gain;               // uncalibrated
  col.s += v; col.ss += v * v; col.smv += m.meanMv;
  col.s2raw += (double)m.irmsRawA * m.irmsRawA;
  if (v < col.mn) col.mn = v;
  if (v > col.mx) col.mx = v;
  if (++col.got < col.want) return;
  double n = col.got, mean = col.s / n, sd = sqrt(fmax(0, col.ss / n - mean * mean));
  if (col.mode == C_STAT) {
    Stat st{}; strncpy(st.label, col.label, 15);
    st.n = col.got; st.mean = mean; st.sd = sd; st.min = col.mn; st.max = col.mx; st.meanMv = col.smv / n;
    if (nStats == MAX_STATS) { memmove(stats, stats + 1, sizeof(Stat) * (MAX_STATS - 1)); nStats--; }
    stats[nStats++] = st;
    Serial.printf("#STAT,%s,n=%lu,mean_A=%.4f,sd_A=%.4f,min_A=%.4f,max_A=%.4f,adc_mean_mV=%.1f\n",
                  st.label, (unsigned long)st.n, st.mean, st.sd, st.min, st.max, st.meanMv);
  } else if (col.mode == C_ZERO) {
    cfg.noiseA = (float)sqrt(col.s2raw / n);                       // rms of the zero-current readings
    Serial.printf("#ZERO,n=%lu,noise_rms_A=%.4f,sd_A=%.4f,max_A=%.4f -> noise floor set (use 'save')\n",
                  (unsigned long)col.got, cfg.noiseA, sd, col.mx);
  } else if (col.mode == C_CAL) {
    double corr = sqrt(fmax(0, mean * mean - (cfg.noiseA / cfg.gain) * (cfg.noiseA / cfg.gain)));
    if (corr > 0) {
      cfg.gain = (float)(col.refA / corr);
      cfg.noiseA = 0;                                               // re-run 'zero' with the new gain
      Serial.printf("#CAL,ref_A=%.4f,measured_uncal_A=%.4f,gain=%.5f (noise floor cleared: run 'zero', then 'save')\n",
                    col.refA, corr, cfg.gain);
    } else {
      Serial.println("#CAL,failed: reading is zero");
    }
  }
  col.mode = C_NONE;
}

static void printCompare() {
  Serial.println("#CMP,label,mean_A,sd_A,delta_from_prev_A,separation_index(=delta/sqrt(sd1^2+sd2^2))");
  for (int i = 0; i < nStats; i++) {
    if (i == 0) { Serial.printf("#CMP,%s,%.4f,%.4f,,\n", stats[i].label, stats[i].mean, stats[i].sd); continue; }
    double d = stats[i].mean - stats[i - 1].mean;
    double s = sqrt(stats[i].sd * stats[i].sd + stats[i - 1].sd * stats[i - 1].sd);
    Serial.printf("#CMP,%s,%.4f,%.4f,%.4f,%.1f\n", stats[i].label, stats[i].mean, stats[i].sd, d, s > 0 ? fabs(d) / s : INFINITY);
  }
}

// ================================================================= settings / commands
static void loadSettings() {
  prefs.begin("curmon", true);
  cfg.gain    = prefs.getFloat("gain", 1.0f);
  cfg.noiseA  = prefs.getFloat("noise", 0.0f);
  cfg.mainsHz = prefs.getFloat("hz", 50.0f);
  prefs.end();
}
static void saveSettings() {
  prefs.begin("curmon", false);
  prefs.putFloat("gain", cfg.gain); prefs.putFloat("noise", cfg.noiseA); prefs.putFloat("hz", cfg.mainsHz);
  prefs.end();
  Serial.println("# settings saved");
}
static void showSettings() {
  Serial.printf("# gain=%.5f noise_A=%.4f mains=%.0f Hz filter|H|=%.4f  CT %.0f:1, burden %.1f ohm, mode %d\n",
                cfg.gain, cfg.noiseA, cfg.mainsHz, filterGain(cfg.mainsHz), CT_RATIO, R_BURDEN, SAMPLER_MODE);
}
static void printHeader() {
  Serial.println("t_ms,win,irms_A,irms_raw_A,adc_mean_mV,vrms_mV,vmin_mV,vmax_mV,n,fs_Hz,overflows,dt_min_us,dt_max_us,dt_mean_us,clip");
}
static void help() {
  Serial.println(
    "# commands:\n"
    "#  help                 this text\n"
    "#  stream on|off        CSV stream (summaries are always printed)\n"
    "#  zero [N]             at 0 A: measure noise floor over N windows (default 50 = 10 s)\n"
    "#  cal <A> [N]          with a known reference current <A>: compute gain\n"
    "#  stat <label> [N]     collect N windows, print mean/sd/min/max, keep for 'cmp'\n"
    "#  cmp                  compare all kept stats (delta and separation index)\n"
    "#  clear                forget kept stats\n"
    "#  gain <x> | noise <A> | hz <50|60>   set values manually\n"
    "#  save | load | show | header");
}

static void handleCommand(String line) {
  line.trim();
  if (!line.length()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String rest = sp < 0 ? "" : line.substring(sp + 1);
  rest.trim();
  String a1 = rest, a2 = "";
  int sp2 = rest.indexOf(' ');
  if (sp2 >= 0) { a1 = rest.substring(0, sp2); a2 = rest.substring(sp2 + 1); a2.trim(); }
  cmd.toLowerCase();
  if (cmd == "help") help();
  else if (cmd == "stream") streamCsv = (a1 != "off");
  else if (cmd == "zero") startCollect(C_ZERO, a1.toInt(), "zero", 0);
  else if (cmd == "cal") { if (a1.toFloat() > 0) startCollect(C_CAL, a2.toInt(), "cal", a1.toFloat()); else Serial.println("# usage: cal <A> [N]"); }
  else if (cmd == "stat") startCollect(C_STAT, a2.toInt(), a1.length() ? a1.c_str() : "run", 0);
  else if (cmd == "cmp") printCompare();
  else if (cmd == "clear") nStats = 0;
  else if (cmd == "gain" && a1.toFloat() > 0) cfg.gain = a1.toFloat();
  else if (cmd == "noise") cfg.noiseA = fmaxf(0, a1.toFloat());
  else if (cmd == "hz" && (a1.toInt() == 50 || a1.toInt() == 60)) cfg.mainsHz = a1.toInt();
  else if (cmd == "save") saveSettings();
  else if (cmd == "load") loadSettings();
  else if (cmd == "show") showSettings();
  else if (cmd == "header") printHeader();
  else Serial.println("# unknown command, type 'help'");
  if (cmd == "gain" || cmd == "noise" || cmd == "hz" || cmd == "load") showSettings();
}

// ================================================================= network hook (add Wi-Fi later)
static void publishMeasurement(const Measurement &m) {
  // Runs in loop(), not in the sampling task: a slow network call only delays the queue
  // (16 records = 3.2 s of buffering); 'queue_drops' in the status line shows if it overflowed.
  // Example later: if (WiFi.status() == WL_CONNECTED) mqtt.publish("cm/irms", String(m.irmsA, 3));
  (void)m;
}

// ================================================================= Arduino entry points
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);              // USB-CDC: wait briefly for the monitor
  loadSettings();
  measQueue = xQueueCreate(16, sizeof(Measurement));
  Serial.println("# current-monitor firmware  (XIAO ESP32C3, A1/GPIO3, SR-3704-150N 3000:1, 10 ohm)");
  showSettings();
  if (!startSampler()) { Serial.println("# sampler failed to start"); return; }
  help();
  printHeader();
}

void loop() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { handleCommand(line); line = ""; }
    else if (line.length() < 64) line += c;
  }
  Measurement m;
  while (xQueueReceive(measQueue, &m, 0) == pdTRUE) {
    if (streamCsv) {
      Serial.printf("%lu,%lu,%.4f,%.4f,%.2f,%.3f,%.1f,%.1f,%lu,%.1f,%lu,%.1f,%.1f,%.1f,%d\n",
                    (unsigned long)m.t_ms, (unsigned long)m.win, m.irmsA, m.irmsRawA, m.meanMv, m.vrmsMv,
                    m.vminMv, m.vmaxMv, (unsigned long)m.n, m.fsHz, (unsigned long)m.overflows,
                    m.dtMinUs, m.dtMaxUs, m.dtMeanUs, m.clip ? 1 : 0);
    }
    feedCollect(m);
    publishMeasurement(m);
    if (m.win % 50 == 49 && queueDrops) Serial.printf("# queue_drops=%lu\n", (unsigned long)queueDrops);
  }
  delay(5);
}
