#include "CoverImageDecoder.h"

#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <PngToBmpConverter.h>

bool CoverImageDecoder::decodeJpegCoverToBmp(FsFile& jpegFile, Print& bmpOut, const bool crop) {
  // Preferred decoder boundary. The current firmware keeps the proven picojpeg path as the
  // safe fallback until a device-native JPEG backend is available behind this interface.
  const bool ok = JpegToBmpConverter::jpegFileToBmpStream(jpegFile, bmpOut, crop);
  if (!ok) {
    LOG_ERR("COVDEC", "JPEG cover decode failed");
  }
  return ok;
}

bool CoverImageDecoder::decodePngCoverToBmp(FsFile& pngFile, Print& bmpOut, const bool crop) {
  const bool ok = PngToBmpConverter::pngFileToBmpStream(pngFile, bmpOut, crop);
  if (!ok) {
    LOG_ERR("COVDEC", "PNG cover decode failed");
  }
  return ok;
}

bool CoverImageDecoder::decodeJpegThumbnailToBmp(FsFile& jpegFile, Print& bmpOut, const int maxWidth,
                                                 const int maxHeight) {
  const bool ok = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(jpegFile, bmpOut, maxWidth, maxHeight);
  if (!ok) {
    LOG_ERR("COVDEC", "JPEG thumbnail decode failed");
  }
  return ok;
}

bool CoverImageDecoder::decodePngThumbnailToBmp(FsFile& pngFile, Print& bmpOut, const int maxWidth,
                                                const int maxHeight) {
  const bool ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, bmpOut, maxWidth, maxHeight);
  if (!ok) {
    LOG_ERR("COVDEC", "PNG thumbnail decode failed");
  }
  return ok;
}
