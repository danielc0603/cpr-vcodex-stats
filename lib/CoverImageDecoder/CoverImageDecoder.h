#pragma once

#include <HalStorage.h>

class Print;

class CoverImageDecoder {
 public:
  static bool decodeJpegCoverToBmp(FsFile& jpegFile, Print& bmpOut, bool crop);
  static bool decodePngCoverToBmp(FsFile& pngFile, Print& bmpOut, bool crop);
  static bool decodeJpegThumbnailToBmp(FsFile& jpegFile, Print& bmpOut, int maxWidth, int maxHeight);
  static bool decodePngThumbnailToBmp(FsFile& pngFile, Print& bmpOut, int maxWidth, int maxHeight);
};
