#include "ReadingTimeChartActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <string>

#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int TIME_BUCKETS = 4;
constexpr int DAY_BUCKETS = 7;

uint8_t getHourBucket(const uint32_t epochSeconds) {
  if (!TimeUtils::isClockValid(epochSeconds)) {
    return 0;
  }
  time_t rawTime = static_cast<time_t>(epochSeconds);
  tm localTime = {};
  if (localtime_r(&rawTime, &localTime) == nullptr) {
    return static_cast<uint8_t>(((epochSeconds % 86400UL) / 3600UL) / 6UL);
  }
  return static_cast<uint8_t>(std::clamp(localTime.tm_hour / 6, 0, TIME_BUCKETS - 1));
}

uint8_t getDayBucket(const uint32_t dayOrdinal) {
  if (dayOrdinal == 0) {
    return 0;
  }
  // Unix day 0 is Thursday. Use Sunday-first compact buckets to avoid adding seven new labels.
  return static_cast<uint8_t>((dayOrdinal + 4U) % DAY_BUCKETS);
}

const char* getTimeBucketLabel(const int index) {
  static constexpr const char* LABELS[TIME_BUCKETS] = {"00", "06", "12", "18"};
  return LABELS[std::clamp(index, 0, TIME_BUCKETS - 1)];
}

const char* getDayBucketLabel(const int index) {
  static constexpr const char* LABELS[DAY_BUCKETS] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return LABELS[std::clamp(index, 0, DAY_BUCKETS - 1)];
}
}  // namespace

void ReadingTimeChartActivity::toggleMode() {
  mode = mode == ChartMode::TimeOfDay ? ChartMode::DayOfWeek : ChartMode::TimeOfDay;
  requestUpdate();
}

void ReadingTimeChartActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    toggleMode();
  }
}

void ReadingTimeChartActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_TIME_CHART));

  const bool timeOfDay = mode == ChartMode::TimeOfDay;
  std::array<uint64_t, DAY_BUCKETS> dayBuckets{};
  std::array<uint64_t, TIME_BUCKETS> timeBuckets{};
  bool hasTimestampSamples = false;

  if (timeOfDay) {
    for (const auto& book : READING_STATS.getBooks()) {
      for (const auto& sample : book.progressSamples) {
        if (sample.sessionMs == 0 || !TimeUtils::isClockValid(sample.endedAt)) {
          continue;
        }
        hasTimestampSamples = true;
        timeBuckets[getHourBucket(sample.endedAt)] += sample.sessionMs;
      }
    }
  } else {
    for (const auto& day : READING_STATS.getReadingDays()) {
      if (day.dayOrdinal == 0 || day.readingMs == 0) {
        continue;
      }
      hasTimestampSamples = true;
      dayBuckets[getDayBucket(day.dayOrdinal)] += day.readingMs;
    }
  }

  const char* modeLabel = timeOfDay ? tr(STR_TIME_OF_DAY) : tr(STR_DAY_OF_WEEK);
  GUI.drawSubHeader(renderer, Rect{0, contentTop, pageWidth, 28}, modeLabel, nullptr);

  if (!hasTimestampSamples) {
    const int y = contentTop + 58;
    const std::string title = renderer.truncatedText(UI_10_FONT_ID, tr(STR_NO_READING_STATS),
                                                     pageWidth - sidePadding * 2, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, sidePadding, y, title.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, sidePadding, y + renderer.getLineHeight(UI_10_FONT_ID) + 8,
                      tr(STR_TIME_SOURCE_UNAVAILABLE), true);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int bucketCount = timeOfDay ? TIME_BUCKETS : DAY_BUCKETS;
  uint64_t maxValue = 1;
  for (int i = 0; i < bucketCount; ++i) {
    maxValue = std::max(maxValue, timeOfDay ? timeBuckets[i] : dayBuckets[i]);
  }

  const int chartTop = contentTop + 46;
  const int chartHeight = std::max(80, contentBottom - chartTop - 42);
  const int chartWidth = pageWidth - sidePadding * 2;
  const int gap = 6;
  const int barWidth = std::max(8, (chartWidth - gap * (bucketCount - 1)) / bucketCount);
  const int baseline = chartTop + chartHeight;
  renderer.drawLine(sidePadding, baseline, sidePadding + chartWidth, baseline, true);

  for (int i = 0; i < bucketCount; ++i) {
    const uint64_t value = timeOfDay ? timeBuckets[i] : dayBuckets[i];
    const int barHeight = static_cast<int>((value * static_cast<uint64_t>(chartHeight - 16)) / maxValue);
    const int x = sidePadding + i * (barWidth + gap);
    const int y = baseline - barHeight;
    if (barHeight > 0) {
      renderer.fillRect(x, y, barWidth, barHeight, true);
    }
    const char* label = timeOfDay ? getTimeBucketLabel(i) : getDayBucketLabel(i);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, x + std::max(0, (barWidth - labelWidth) / 2), baseline + 7, label, true);
  }

  const std::string maxLabel =
      std::string(tr(STR_BEST_DAY)) + ": " + ReadingStatsAnalytics::formatDurationHm(maxValue);
  renderer.drawText(SMALL_FONT_ID, sidePadding, contentBottom - renderer.getLineHeight(SMALL_FONT_ID),
                    renderer.truncatedText(SMALL_FONT_ID, maxLabel.c_str(), chartWidth).c_str(), true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
