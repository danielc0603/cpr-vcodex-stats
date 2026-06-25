#pragma once

class LibraryWorkers {
  bool paused = false;

 public:
  void pause() { paused = true; }
  void resume() { paused = false; }
  bool isPaused() const { return paused; }
};
