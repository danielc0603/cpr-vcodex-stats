#include "FontInstaller.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"

FontInstaller::FontInstaller(SdCardFontRegistry& registry) : registry_(registry) {}

bool FontInstaller::isValidFamilyName(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;
  if (strstr(name, "..") != nullptr || strchr(name, '/') != nullptr || strchr(name, '\\') != nullptr) return false;

  for (const char* p = name; *p != '\0'; ++p) {
    const char c = *p;
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
  }
  return true;
}

bool FontInstaller::isValidCpfontFilename(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;
  if (strstr(name, "..") != nullptr || strchr(name, '/') != nullptr || strchr(name, '\\') != nullptr) return false;

  static constexpr char kExt[] = ".cpfont";
  static constexpr size_t kExtLen = sizeof(kExt) - 1;
  const size_t nameLen = strlen(name);
  if (nameLen <= kExtLen || strcmp(name + nameLen - kExtLen, kExt) != 0) return false;

  const size_t baseLen = nameLen - kExtLen;
  for (size_t i = 0; i < baseLen; ++i) {
    const char c = name[i];
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
  }
  return true;
}

bool FontInstaller::ensureFamilyDir(const char* familyName) {
  if (!isValidFamilyName(familyName)) return false;

  const char* root = SdCardFontRegistry::findFamilyRoot(familyName);
  if (!root) root = SdCardFontRegistry::defaultWriteRoot();

  if (!Storage.exists(root) && !Storage.mkdir(root)) {
    LOG_ERR("FONT", "Failed to create font root: %s", root);
    return false;
  }

  char dirPath[160];
  snprintf(dirPath, sizeof(dirPath), "%s/%s", root, familyName);
  if (!Storage.exists(dirPath) && !Storage.mkdir(dirPath)) {
    LOG_ERR("FONT", "Failed to create font family dir: %s", dirPath);
    return false;
  }
  return true;
}

bool FontInstaller::validateCpfontFile(const char* path) {
  FsFile file;
  if (!Storage.openFileForRead("FONT", path, file)) return false;

  uint8_t magic[CPFONT_MAGIC_LEN];
  const size_t bytesRead = file.read(magic, CPFONT_MAGIC_LEN);
  file.close();

  return bytesRead == CPFONT_MAGIC_LEN && memcmp(magic, "CPFONT\0\0", CPFONT_MAGIC_LEN) == 0;
}

void FontInstaller::buildFontPath(const char* family, const char* filename, char* outBuf, size_t outBufSize) {
  const char* root = SdCardFontRegistry::findFamilyRoot(family);
  if (!root) root = SdCardFontRegistry::defaultWriteRoot();
  snprintf(outBuf, outBufSize, "%s/%s/%s", root, family, filename);
}

FontInstaller::Error FontInstaller::deleteFamily(const char* familyName) {
  if (!isValidFamilyName(familyName)) return Error::INVALID_FAMILY_NAME;

  const char* roots[] = {SdCardFontRegistry::FONTS_DIR_HIDDEN, SdCardFontRegistry::FONTS_DIR_VISIBLE};
  for (const char* root : roots) {
    char dirPath[160];
    snprintf(dirPath, sizeof(dirPath), "%s/%s", root, familyName);
    if (Storage.exists(dirPath) && !Storage.removeDir(dirPath)) {
      LOG_ERR("FONT", "Failed to remove font family dir: %s", dirPath);
      return Error::SD_WRITE_ERROR;
    }
  }

  if (strcmp(SETTINGS.sdFontFamilyName, familyName) == 0) {
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
    SETTINGS.saveToFile();
  }
  return Error::OK;
}

void FontInstaller::refreshRegistry() { registry_.discover(); }

bool FontInstaller::isFamilyInstalled(const char* familyName) const {
  return registry_.findFamily(familyName) != nullptr;
}
