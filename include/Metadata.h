/*
 * Builds the static characteristics and default request settings for the two
 * fixed virtual cameras. Kept deliberately small: LIMITED level, no RAW/flash.
 */

#ifndef FAKECAMERA_METADATA_H
#define FAKECAMERA_METADATA_H

#include <cstdint>
#include <utility>
#include <vector>

namespace fakecamera {

// Output sizes every camera supports (w,h), largest first.
const std::vector<std::pair<int, int>>& SupportedSizes();

// Serialized camera_metadata_t for AIDL CameraMetadata.metadata.
std::vector<uint8_t> BuildStaticCharacteristics(int id);

// type is aidl RequestTemplate (1=PREVIEW .. 6=MANUAL).
std::vector<uint8_t> BuildDefaultRequest(int id, int type);

}  // namespace fakecamera

#endif  // FAKECAMERA_METADATA_H
