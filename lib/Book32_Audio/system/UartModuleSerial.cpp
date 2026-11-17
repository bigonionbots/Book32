/**
 * @file UartModuleSerial.cpp
 * @brief ModuleSerial over a plain UART (M5Stack PaperDIY: FPC header pins to the module's UART).
 *
 * Uses the ESP-IDF UART driver directly (not HardwareSerial) for read-with-timeout and for the
 * overflow and line-error events, which the counters report.
 */

#include "ModuleSerial.h"

#if INX_HAS_AUDIO_MODULE && INX_AUDIO_LINK_UART

#include <Arduino.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

#include "AudioModule.h"

#ifndef INX_AUDIO_UART_NUM
#define INX_AUDIO_UART_NUM 1
#endif
#ifndef INX_AUDIO_UART_TX
#error "INX_AUDIO_LINK_UART needs INX_AUDIO_UART_TX and INX_AUDIO_UART_RX (platformio.ini)"
#endif

namespace ModuleSerial {

namespace {

constexpr uart_port_t PORT = static_cast<uart_port_t>(INX_AUDIO_UART_NUM);
// The module sends ACKs, STATUS and LOG frames: little, but in bursts while its log runs.
constexpr int RX_BUFFER = 8192;
// Frames are at most audiolink::MAX_FRAME (about 1 KB); AudioLink keeps a few KB in flight.
// A buffer this size means writes return at once instead of waiting for the wire.
constexpr int TX_BUFFER = 8192;
constexpr int EVENT_QUEUE = 16;

std::atomic<State> current{State::Stopped};
QueueHandle_t events = nullptr;
uint32_t baudRate = 0;
std::atomic<uint32_t> rxBytes{0};
std::atomic<uint32_t> txBytes{0};
std::atomic<uint32_t> rxDropped{0};
std::atomic<uint32_t> lineErrors{0};
uint32_t starts = 0;
uint32_t stops = 0;
// Calls inside the driver right now. stop() marks the link stopped, waits for this to reach zero,
// and only then deletes the driver: AudioLink's task is nearly always inside read().
std::atomic<int> users{0};

// Holds the driver for one call. ok is false once stop() has begun (or before start()).
class Use {
 public:
  Use() {
    ++users;
    ok = current == State::Connected;
  }
  ~Use() { --users; }
  Use(const Use&) = delete;
  Use& operator=(const Use&) = delete;
  bool ok;
};

// The driver reports overflows and line errors as events; count them (any task, non-blocking).
void drainEvents() {
  if (!events) return;  // the caller holds a Use
  uart_event_t event;
  while (xQueueReceive(events, &event, 0) == pdTRUE) {
    switch (event.type) {
      case UART_FIFO_OVF:
      case UART_BUFFER_FULL:
        ++rxDropped;
        break;
      case UART_FRAME_ERR:
      case UART_PARITY_ERR:
        ++lineErrors;
        break;
      default:
        break;
    }
  }
}

}  // namespace

bool start(const uint32_t baud) {
  if (current == State::Connected) return true;
  uart_config_t config = {};
  config.baud_rate = static_cast<int>(baud);
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  esp_err_t err = uart_driver_install(PORT, RX_BUFFER, TX_BUFFER, EVENT_QUEUE, &events, 0);
  if (err == ESP_OK) err = uart_param_config(PORT, &config);
  if (err == ESP_OK) err = uart_set_pin(PORT, INX_AUDIO_UART_TX, INX_AUDIO_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  // The module idles its TX high; a pull-up keeps RX quiet while the module is off or in reset.
  if (err == ESP_OK) gpio_pullup_en(static_cast<gpio_num_t>(INX_AUDIO_UART_RX));
  if (err != ESP_OK) {
    AudioModule::logf("UART link: start failed (%s)", esp_err_to_name(err));
    if (uart_is_driver_installed(PORT)) uart_driver_delete(PORT);
    events = nullptr;
    current = State::Failed;
    return false;
  }
  uart_get_baudrate(PORT, &baudRate);
  ++starts;
  current = State::Connected;
  AudioModule::logf("UART link: UART%d TX %d RX %d at %lu baud", INX_AUDIO_UART_NUM, INX_AUDIO_UART_TX,
                    INX_AUDIO_UART_RX, static_cast<unsigned long>(baudRate));
  return true;
}

void stop() {
  if (current != State::Connected) {
    current = State::Stopped;
    return;
  }
  current = State::Stopped;
  while (users.load() > 0) vTaskDelay(1);  // a read waits at most its timeout
  uart_driver_delete(PORT);
  events = nullptr;
  ++stops;
  // Back to floating inputs (gpio_reset_pin leaves a pull-up on), so nothing feeds a module
  // whose supply is off.
  for (const int pin : {INX_AUDIO_UART_TX, INX_AUDIO_UART_RX}) {
    gpio_reset_pin(static_cast<gpio_num_t>(pin));
    gpio_pullup_dis(static_cast<gpio_num_t>(pin));
  }
}

State state() { return current; }

bool isConnected() { return current == State::Connected; }

DeviceInfo device() {
  DeviceInfo info;
  info.driver = "UART";
  snprintf(info.product, sizeof(info.product), "UART%d TX %d RX %d %lu baud", INX_AUDIO_UART_NUM,
           INX_AUDIO_UART_TX, INX_AUDIO_UART_RX, static_cast<unsigned long>(baudRate));
  return info;
}

Counters counters() {
  {
    Use use;
    if (use.ok) drainEvents();
  }
  Counters out;
  out.rxBytes = rxBytes;
  out.txBytes = txBytes;
  out.rxDropped = rxDropped;
  out.lineErrors = lineErrors;
  out.connects = starts;
  out.disconnects = stops;
  return out;
}

size_t write(const uint8_t* data, const size_t length, uint32_t) {
  // The timeout is moot: the TX buffer takes a whole frame unless several KB are already queued,
  // and then the wait is the time the UART needs to send them.
  Use use;
  if (!use.ok || length == 0) return 0;
  const int written = uart_write_bytes(PORT, data, length);
  if (written != static_cast<int>(length)) return 0;
  txBytes += length;
  return length;
}

namespace {

// read() with the driver held.
size_t readHeld(uint8_t* data, const size_t length, const uint32_t timeoutMs) {
  drainEvents();
  // Wait for the first byte, then take whatever else has arrived.
  int got = uart_read_bytes(PORT, data, 1, pdMS_TO_TICKS(timeoutMs));
  if (got <= 0) return 0;
  size_t buffered = 0;
  uart_get_buffered_data_len(PORT, &buffered);
  if (buffered > 0 && length > 1) {
    const int more = uart_read_bytes(PORT, data + 1, std::min(buffered, length - 1), 0);
    if (more > 0) got += more;
  }
  rxBytes += static_cast<uint32_t>(got);
  return static_cast<size_t>(got);
}

}  // namespace

size_t read(uint8_t* data, const size_t length, const uint32_t timeoutMs) {
  {
    Use use;
    if (use.ok && length > 0) return readHeld(data, length, timeoutMs);
  }
  vTaskDelay(pdMS_TO_TICKS(timeoutMs));
  return 0;
}

void clearReceived() {
  Use use;
  if (use.ok) uart_flush_input(PORT);
}

}  // namespace ModuleSerial

#endif  // INX_HAS_AUDIO_MODULE && INX_AUDIO_LINK_UART
