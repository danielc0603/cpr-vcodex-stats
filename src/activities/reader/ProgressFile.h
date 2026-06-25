#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ProgressFile {

inline bool writeAtomicPath(const std::string& finalPath, const uint8_t* data, const size_t len) {
  if (finalPath.empty() || data == nullptr || len == 0) {
    return false;
  }

  const std::string tmpPath = finalPath + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite("PRG", tmpPath, f)) {
      LOG_ERR("PRG", "Could not open temp progress file: %s", tmpPath.c_str());
      return false;
    }
    const size_t written = f.write(data, len);
    if (written != len) {
      LOG_ERR("PRG", "Short progress write: %u/%u", static_cast<unsigned>(written), static_cast<unsigned>(len));
      f.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }
    f.flush();
    f.close();
  }

  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("PRG", "Failed to rename temp progress file: %s", finalPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}

inline bool writeAtomic(const std::string& cachePath, const uint8_t* data, const size_t len) {
  return writeAtomicPath(cachePath + "/progress.bin", data, len);
}

}  // namespace ProgressFile
