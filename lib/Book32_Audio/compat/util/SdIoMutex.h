#pragma once

namespace SdIoMutex {

/** Serializes SD card access between the main loop and the audio tasks. */
void lock();
void unlock();

class Lock final {
 public:
  Lock() { lock(); }
  ~Lock() { unlock(); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

}  // namespace SdIoMutex
