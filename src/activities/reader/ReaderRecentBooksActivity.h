#pragma once

#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderRecentBooksActivity final : public Activity {
 public:
  explicit ReaderRecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const std::string& currentPath);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::string currentPath;
  std::vector<RecentBook> books;
  int selectedIndex = 0;
  bool waitForBackRelease = false;
  ButtonNavigator buttonNavigator;

  void loadBooks();
};
