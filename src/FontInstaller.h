#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>

class FontInstaller {
 public:
  enum class Error {
    OK,
    INVALID_FAMILY_NAME,
    INVALID_FILE,
    SD_WRITE_ERROR,
  };

  explicit FontInstaller(SdCardFontRegistry& registry);

  static bool isValidFamilyName(const char* name);
  static bool isValidCpfontFilename(const char* name);
  bool ensureFamilyDir(const char* familyName);
  bool validateCpfontFile(const char* path);
  static void buildFontPath(const char* family, const char* filename, char* outBuf, size_t outBufSize);
  Error deleteFamily(const char* familyName);
  void refreshRegistry();
  bool isFamilyInstalled(const char* familyName) const;

 private:
  SdCardFontRegistry& registry_;

  static constexpr size_t CPFONT_MAGIC_LEN = 8;
};
