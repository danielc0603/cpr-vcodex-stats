#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderJumpMenuActivity final : public Activity {
 public:
  enum class Action { CHAPTERS = 0, PERCENT = 1, BOOKMARKS = 2, BACK_TO_READING = 3 };

  explicit ReaderJumpMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  bool hasChapters, bool hasBookmarks);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  struct Item {
    Action action;
    StrId labelId;
  };

  std::string title;
  std::vector<Item> items;
  int selectedIndex = 0;
  bool waitForBackRelease = false;
  ButtonNavigator buttonNavigator;
};
