#include "BookReadingAdjustmentActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <variant>

#include "AchievementsStore.h"
#include "ReadingDateSelectionActivity.h"
#include "ReadingStatsStore.h"
#include "fontIds.h"
#include "activities/util/CompactHudRenderer.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int FIELD_COUNT = 7;
constexpr int OPERATION_COUNT = 2;
constexpr int64_t MINUTES_TO_MS = 60LL * 1000LL;
constexpr int DURATION_MINUTES[] = {5, 10, 15, 20, 30, 45, 60, 90, 120};
constexpr int DURATION_COUNT = sizeof(DURATION_MINUTES) / sizeof(DURATION_MINUTES[0]);
constexpr int START_DATE_FIELD_INDEX = 3;
constexpr int FINISH_DATE_FIELD_INDEX = 4;
constexpr int APPLY_FIELD_INDEX = 5;
constexpr int CANCEL_FIELD_INDEX = 6;

int wrapIndex(const int value, const int delta, const int count) {
  int next = value + delta;
  next %= count;
  if (next < 0) {
    next += count;
  }
  return next;
}
}  // namespace

void BookReadingAdjustmentActivity::onEnter() {
  Activity::onEnter();
  selectedField = 0;
  selectedOperation = 0;
  selectedDuration = 2;
  lastApplyFailed = false;
  initializeSelectedDate();
  requestUpdate();
}

void BookReadingAdjustmentActivity::adjustSelectedValue(const int delta) {
  lastApplyFailed = false;
  if (selectedField == 0) {
    selectedOperation = wrapIndex(selectedOperation, delta, OPERATION_COUNT);
  } else if (selectedField == 2) {
    selectedDuration = wrapIndex(selectedDuration, delta, DURATION_COUNT);
  }
  requestUpdate();
}

void BookReadingAdjustmentActivity::initializeSelectedDate() {
  bool usedFallback = false;
  const uint32_t referenceTimestamp = READING_STATS.getDisplayTimestamp(&usedFallback);
  if (!TimeUtils::isClockValid(referenceTimestamp)) {
    selectedDayOrdinal = 0;
    return;
  }

  selectedDayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
}

int32_t BookReadingAdjustmentActivity::getSelectedDeltaMs() const {
  int32_t deltaMs = static_cast<int32_t>(DURATION_MINUTES[selectedDuration] * MINUTES_TO_MS);
  if (selectedOperation == 1) {
    deltaMs = -deltaMs;
  }
  return deltaMs;
}

uint64_t BookReadingAdjustmentActivity::getSelectedDayReadingMs() const {
  if (selectedDayOrdinal == 0) {
    return 0;
  }

  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return 0;
  }

  for (const auto& day : book->readingDays) {
    if (day.dayOrdinal == selectedDayOrdinal) {
      return day.readingMs;
    }
    if (day.dayOrdinal > selectedDayOrdinal) {
      break;
    }
  }
  return 0;
}

bool BookReadingAdjustmentActivity::canApplySelectedAdjustment() const {
  if (selectedDayOrdinal == 0 || READING_STATS.findBook(bookPath) == nullptr) {
    return false;
  }

  const int32_t deltaMs = getSelectedDeltaMs();
  if (deltaMs >= 0) {
    return true;
  }

  return getSelectedDayReadingMs() >= static_cast<uint64_t>(-deltaMs);
}

std::string BookReadingAdjustmentActivity::getAdjustmentPreviewInfo() const {
  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return tr(STR_BOOK_NOT_FOUND);
  }
  if (selectedDayOrdinal == 0) {
    return tr(STR_SET_DATE_BEFORE_APPLYING);
  }

  const uint64_t currentMs = getSelectedDayReadingMs();
  const int32_t deltaMs = getSelectedDeltaMs();
  const std::string dayTotal = std::string(tr(STR_DAY_TOTAL)) + ": ";
  if (deltaMs < 0) {
    const uint64_t removeMs = static_cast<uint64_t>(-deltaMs);
    if (currentMs < removeMs) {
      return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " (" + tr(STR_NOT_ENOUGH) + ")";
    }
    return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " -> " +
           ReadingStatsAnalytics::formatDurationHm(currentMs - removeMs);
  }

  return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " -> " +
         ReadingStatsAnalytics::formatDurationHm(currentMs + static_cast<uint64_t>(deltaMs));
}

const char* BookReadingAdjustmentActivity::getOperationLabel() const {
  return selectedOperation == 0 ? tr(STR_ADD) : tr(STR_SUBTRACT);
}

std::string BookReadingAdjustmentActivity::getDateLabel() const {
  int selectedYear = 0;
  unsigned selectedMonth = 0;
  unsigned selectedDay = 0;
  if (selectedDayOrdinal == 0 ||
      !TimeUtils::getDateFromDayOrdinal(selectedDayOrdinal, selectedYear, selectedMonth, selectedDay)) {
    return tr(STR_NOT_SET);
  }
  return TimeUtils::formatDateParts(selectedYear, selectedMonth, selectedDay);
}

const char* BookReadingAdjustmentActivity::getDurationLabel() const {
  static char label[12];
  snprintf(label, sizeof(label), "%d min", DURATION_MINUTES[selectedDuration]);
  return label;
}

void BookReadingAdjustmentActivity::openAdjustmentDateEditor() {
  startActivityForResult(std::make_unique<ReadingDateSelectionActivity>(renderer, mappedInput, selectedDayOrdinal),
                         [this](const ActivityResult& result) {
                           mappedInput.armPressedButtonsReleaseGuard();
                           if (!result.isCancelled) {
                             if (const auto* page = std::get_if<PageResult>(&result.data)) {
                               selectedDayOrdinal = page->page;
                               lastApplyFailed = false;
                             }
                           }
                           requestUpdate();
                         });
}

std::string BookReadingAdjustmentActivity::getBookDateLabel(const bool finishDate) const {
  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return tr(STR_BOOK_NOT_FOUND);
  }
  const uint32_t timestamp = finishDate ? book->completedAt : book->firstReadAt;
  if (!TimeUtils::isClockValid(timestamp)) {
    return tr(STR_NOT_SET);
  }
  return ReadingStatsAnalytics::formatDayOrdinalLabel(TimeUtils::getLocalDayOrdinal(timestamp));
}

void BookReadingAdjustmentActivity::openBookDateEditor(const bool finishDate) {
  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    lastApplyFailed = true;
    requestUpdate();
    return;
  }

  const uint32_t fallbackTimestamp = book->lastReadAt != 0 ? book->lastReadAt : READING_STATS.getDisplayTimestamp();
  const uint32_t targetTimestamp = finishDate ? book->completedAt : book->firstReadAt;
  const uint32_t initialDay =
      TimeUtils::isClockValid(targetTimestamp)
          ? TimeUtils::getLocalDayOrdinal(targetTimestamp)
          : (TimeUtils::isClockValid(fallbackTimestamp) ? TimeUtils::getLocalDayOrdinal(fallbackTimestamp) : 0);
  startActivityForResult(std::make_unique<ReadingDateSelectionActivity>(renderer, mappedInput, initialDay, finishDate),
                         [this, finishDate](const ActivityResult& result) {
                           mappedInput.armPressedButtonsReleaseGuard();
                           if (!result.isCancelled) {
                             if (const auto* page = std::get_if<PageResult>(&result.data)) {
                               if (finishDate) {
                                 READING_STATS.setBookFinishDate(bookPath, page->page);
                               } else {
                                 READING_STATS.setBookStartDate(bookPath, page->page);
                               }
                             }
                           }
                           requestUpdate();
                         });
}

bool BookReadingAdjustmentActivity::applyAdjustment() {
  const uint32_t dayOrdinal = selectedDayOrdinal;
  const int32_t deltaMs = getSelectedDeltaMs();

  if (!READING_STATS.adjustBookReadingTime(bookPath, dayOrdinal, deltaMs)) {
    lastApplyFailed = true;
    requestUpdate();
    return false;
  }

  ACHIEVEMENTS.rebuildProgressFromCurrentStats();
  finish();
  return true;
}

void BookReadingAdjustmentActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedField == 1) {
      openAdjustmentDateEditor();
    } else if (selectedField == START_DATE_FIELD_INDEX) {
      openBookDateEditor(false);
    } else if (selectedField == FINISH_DATE_FIELD_INDEX) {
      openBookDateEditor(true);
    } else if (selectedField == APPLY_FIELD_INDEX) {
      applyAdjustment();
    } else if (selectedField == CANCEL_FIELD_INDEX) {
      finish();
    } else {
      lastApplyFailed = false;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
    selectedField = ButtonNavigator::previousIndex(selectedField, FIELD_COUNT);
    lastApplyFailed = false;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
    selectedField = ButtonNavigator::nextIndex(selectedField, FIELD_COUNT);
    lastApplyFailed = false;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustSelectedValue(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustSelectedValue(1); });
}

void BookReadingAdjustmentActivity::render(RenderLock&&) {
  const std::string subtitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), renderer.getScreenWidth() - 40);
  std::vector<CompactHudRenderer::Row> rows;
  rows.reserve(FIELD_COUNT);
  for (int index = 0; index < FIELD_COUNT; ++index) {
    std::string label;
    std::string value;
    if (index == 0) {
      label = tr(STR_ACTION);
      value = getOperationLabel();
    } else if (index == 1) {
      label = tr(STR_DATE);
      value = getDateLabel();
    } else if (index == 2) {
      label = tr(STR_AMOUNT);
      value = getDurationLabel();
    } else if (index == START_DATE_FIELD_INDEX) {
      label = tr(STR_START_DATE);
      value = getBookDateLabel(false);
    } else if (index == FINISH_DATE_FIELD_INDEX) {
      label = tr(STR_FINISH_DATE);
      value = getBookDateLabel(true);
    } else if (index == APPLY_FIELD_INDEX) {
      label = tr(STR_CONFIRM);
      value = canApplySelectedAdjustment() ? "" : tr(STR_COULD_NOT_APPLY_CORRECTION);
    } else {
      label = tr(STR_CANCEL);
      value = "";
    }
    rows.push_back(CompactHudRenderer::Row{label, value, false});
  }

  std::string info = getAdjustmentPreviewInfo();
  std::string hint;
  if (lastApplyFailed) {
    hint = tr(STR_COULD_NOT_APPLY_CORRECTION);
  } else if (!canApplySelectedAdjustment()) {
    hint = tr(STR_CHOOSE_ADD_OR_REDUCE_AMOUNT);
  }
  CompactHudRenderer::ActionListConfig config;
  config.title = tr(STR_ADJUST_READING_TIME);
  config.context = hint.empty() ? std::vector<std::string>{subtitle, info}
                                : std::vector<std::string>{subtitle, info, hint};
  config.rows = std::move(rows);
  config.selectedIndex = selectedField;
  config.minWidth = 330;
  config.maxRows = FIELD_COUNT;
  config.drawHints = false;
  CompactHudRenderer::drawActionList(renderer, mappedInput, config);
}
