#pragma once

#include <cstdint>

#include "../Activity.h"

class ReadingTimeChartActivity final : public Activity {
  enum class ChartMode : uint8_t { TimeOfDay, DayOfWeek };

  ChartMode mode = ChartMode::TimeOfDay;

  void toggleMode();

 public:
  explicit ReadingTimeChartActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingTimeChart", renderer, mappedInput) {}

  void loop() override;
  void render(RenderLock&&) override;
};
