#include "Pulse.h"
#include <atomic>
#include <limits>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/************************************************************
*                          Defines                          *
*************************************************************/
// Pines de entrada para los contadores de pulsos
#define PULSE_PIN_1        GPIO_NUM_27
#define PULSE_PIN_2        GPIO_NUM_35

// Filtro anti‑bouncing por software (en microsegundos)
#define DEBOUNCE_US        500

/*************************************************************
*                     Variables globales                     *
**************************************************************/

// Variables atómicas para conteo de pulsos
static std::atomic<uint32_t> pulseCount1(0);
static std::atomic<uint32_t> pulseCount2(0);
static SemaphoreHandle_t pulseMutex = NULL;
static PulseData pulseState;
static uint32_t pulseSeq = 0;

/*************************************************************
*                           ISRs                             *
**************************************************************/

void IRAM_ATTR countPulse1() {
    static uint32_t lastTime = 0;
    uint32_t now = micros();
    if (now - lastTime >= DEBOUNCE_US) {
        pulseCount1.fetch_add(1, std::memory_order_relaxed);
        lastTime = now;
    }
}

void IRAM_ATTR countPulse2() {
    static uint32_t lastTime = 0;
    uint32_t now = micros();
    if (now - lastTime >= DEBOUNCE_US) {
        pulseCount2.fetch_add(1, std::memory_order_relaxed);
        lastTime = now;
    }
}

void initPulseCounters() {
    // Configurar pines de entrada con pull-up interno
    pinMode(PULSE_PIN_1, INPUT_PULLUP);
    pinMode(PULSE_PIN_2, INPUT_PULLUP);

    // Interrupciones por flanco ascendente con filtro por software
    attachInterrupt(digitalPinToInterrupt(PULSE_PIN_1), countPulse1, RISING);
    attachInterrupt(digitalPinToInterrupt(PULSE_PIN_2), countPulse2, RISING);

    pulseMutex = xSemaphoreCreateMutex();
}

static float pulsesToPerSecond(uint32_t pulses, uint32_t elapsedMs) {
    if (elapsedMs == 0) return 0.0f;
    float seconds = static_cast<float>(elapsedMs) / 1000.0f;
    return static_cast<float>(pulses) / seconds;
}

void pulseUpdateIfDue() {
    static TickType_t lastSampleTick = 0;
    const TickType_t period = pdMS_TO_TICKS(2000);
    static uint64_t total1 = 0;
    static uint64_t total2 = 0;

    if (lastSampleTick == 0) {
        lastSampleTick = xTaskGetTickCount();
        return;
    }

    TickType_t nowTick = xTaskGetTickCount();
    if (nowTick - lastSampleTick < period) {
        return;
    }

    uint32_t elapsedMs = (nowTick - lastSampleTick) * portTICK_PERIOD_MS;
    lastSampleTick = nowTick;

    uint32_t pulses1 = pulseCount1.exchange(0, std::memory_order_relaxed);
    uint32_t pulses2 = pulseCount2.exchange(0, std::memory_order_relaxed);

    const uint64_t maxVal = std::numeric_limits<uint64_t>::max();
    if (total1 > maxVal - pulses1) {
        total1 = 0;
    }
    if (total2 > maxVal - pulses2) {
        total2 = 0;
    }
    total1 += pulses1;
    total2 += pulses2;

    if (xSemaphoreTake(pulseMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        pulseState.pulses1 = pulses1;
        pulseState.pulses2 = pulses2;
        pulseState.elapsedMs = elapsedMs;
        pulseState.rate1 = pulsesToPerSecond(pulses1, elapsedMs);
        pulseState.rate2 = pulsesToPerSecond(pulses2, elapsedMs);
        pulseState.total1 = total1;
        pulseState.total2 = total2;
        pulseSeq++;
        xSemaphoreGive(pulseMutex);
    }
}

bool pulseGetData(PulseData& out, uint32_t& seq) {
    if (pulseMutex == NULL) {
        return false;
    }
    pulseUpdateIfDue();
    if (xSemaphoreTake(pulseMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return false;
    }
    out = pulseState;
    seq = pulseSeq;
    xSemaphoreGive(pulseMutex);
    return seq != 0;
}
