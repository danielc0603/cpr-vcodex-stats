#pragma once
#include <string>
#include <cstdint>

struct PendingBookmarkJumpState {
  bool active = false;
  std::string bookPath;
  uint16_t spineIndex = 0;
  uint16_t pageNumber = 0;
  void clear();
};

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};  // circular buffer of recent wallpaper indices
  uint8_t recentSleepPos = 0;                           // next write slot
  uint8_t recentSleepFill = 0;                          // valid entries (0..SLEEP_RECENT_COUNT)
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  uint32_t lastKnownValidTimestamp = 0;
  uint8_t syncDayReminderStartCount = 0;
  bool syncDayReminderLatched = false;
  PendingBookmarkJumpState pendingBookmarkJump;
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile();

  bool loadFromFile();
  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;
  void pushRecentSleep(uint16_t idx);
  uint16_t getMostRecentSleepIndex() const;
  void recordUsefulStart(uint8_t reminderThreshold);
  void registerValidTimeSync(uint32_t validTimestamp);
  bool shouldShowSyncDayReminder(uint8_t reminderThreshold) const;

 private:
  bool loadFromBinaryFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
