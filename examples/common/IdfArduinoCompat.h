/**
 * @file IdfArduinoCompat.h
 * @brief ESP-IDF compatibility layer for the Arduino-shaped StatusLed CLI.
 *
 * This is example glue only. It implements the small Arduino surface used by
 * examples/01_status_led_cli so the ESP-IDF example can share the same command
 * implementation without linking the Arduino framework.
 */

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef STATUSLED_EXAMPLE_PLATFORM_IDF
#define STATUSLED_EXAMPLE_PLATFORM_IDF 1
#endif

#ifndef F
#define F(value) value
#endif

inline uint32_t millis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

inline TickType_t idfExampleDelayTicks(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (ticks == 0 && ms > 0U) {
    ticks = 1;
  }
  return ticks;
}

inline void delay(uint32_t ms) {
  vTaskDelay(idfExampleDelayTicks(ms));
}

inline void yield() {
  vTaskDelay(1);
}

class IdfConsole {
 public:
  void begin(unsigned long) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
      (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
  }

  int available() {
    pollInput();
    return static_cast<int>(_count);
  }

  int read() {
    pollInput();
    if (_count == 0U) {
      return -1;
    }
    const uint8_t value = _rx[_tail];
    _tail = (_tail + 1U) % kRxCapacity;
    --_count;
    return static_cast<int>(value);
  }

  size_t write(uint8_t value) {
    return fwrite(&value, 1U, 1U, stdout);
  }

  size_t write(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0U) {
      return 0U;
    }
    return fwrite(data, 1U, len, stdout);
  }

  int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int written = vfprintf(stdout, fmt, args);
    va_end(args);
    return written;
  }

  void print(const char* value) {
    if (value != nullptr) {
      fputs(value, stdout);
    }
  }

  void print(char value) {
    (void)write(static_cast<uint8_t>(value));
  }

  void print(unsigned char value) {
    (void)printf("%u", static_cast<unsigned>(value));
  }

  void print(int value) {
    (void)printf("%d", value);
  }

  void print(unsigned int value) {
    (void)printf("%u", value);
  }

  void print(long value) {
    (void)printf("%ld", value);
  }

  void print(unsigned long value) {
    (void)printf("%lu", value);
  }

  void print(long long value) {
    (void)printf("%lld", value);
  }

  void print(unsigned long long value) {
    (void)printf("%llu", value);
  }

  void print(float value) {
    (void)printf("%.2f", static_cast<double>(value));
  }

  void print(double value) {
    (void)printf("%.2f", value);
  }

  void println() {
    (void)write(static_cast<uint8_t>('\n'));
  }

  template <typename T>
  void println(T value) {
    print(value);
    println();
  }

  void flush() {
    fflush(stdout);
  }

 private:
  static constexpr size_t kRxCapacity = 256U;

  void pollInput() {
    while (_count < kRxCapacity) {
      uint8_t value = 0U;
      const ssize_t readCount = ::read(STDIN_FILENO, &value, 1U);
      if (readCount == 1) {
        _rx[_head] = value;
        _head = (_head + 1U) % kRxCapacity;
        ++_count;
        continue;
      }
      if (readCount < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
      }
      return;
    }
  }

  uint8_t _rx[kRxCapacity] = {};
  size_t _head = 0U;
  size_t _tail = 0U;
  size_t _count = 0U;
};

extern IdfConsole Serial;
