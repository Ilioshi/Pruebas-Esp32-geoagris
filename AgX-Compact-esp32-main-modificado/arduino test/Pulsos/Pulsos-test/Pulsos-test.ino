#include <Arduino.h>

/************** CONFIG **************/
#define PIN_CH1     18
#define PIN_CH2     19

#define FREQ_CH1_HZ 1000
#define FREQ_CH2_HZ 500

#define TIMER_TICK_US 10   // resolución del timer
/***********************************/

hw_timer_t* timer = nullptr;

volatile uint32_t cnt1 = 0;
volatile uint32_t cnt2 = 0;

volatile bool lvl1 = true;
volatile bool lvl2 = true;

// Cuántos ticks hacen medio período
constexpr uint32_t TICKS_CH1 = (500000 / FREQ_CH1_HZ) / TIMER_TICK_US;
constexpr uint32_t TICKS_CH2 = (500000 / FREQ_CH2_HZ) / TIMER_TICK_US;

void IRAM_ATTR onTimer() {
  // Canal 1
  if (++cnt1 >= TICKS_CH1) {
    cnt1 = 0;
    lvl1 = !lvl1;
    gpio_set_level((gpio_num_t)PIN_CH1, lvl1);
  }

  // Canal 2
  if (++cnt2 >= TICKS_CH2) {
    cnt2 = 0;
    lvl2 = !lvl2;
    gpio_set_level((gpio_num_t)PIN_CH2, lvl2);
  }
}

void setup() {
  pinMode(PIN_CH1, OUTPUT);
  pinMode(PIN_CH2, OUTPUT);

  digitalWrite(PIN_CH1, HIGH);
  digitalWrite(PIN_CH2, HIGH);

  // Timer a 1 MHz (1 µs por tick)
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);

  // Interrupción cada TIMER_TICK_US
  timerAlarm(timer, TIMER_TICK_US, true, 0);
  timerStart(timer);
}

void loop() {
  // Nada
}
