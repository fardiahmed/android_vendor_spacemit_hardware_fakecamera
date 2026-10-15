#define LOG_TAG "fakecamera-metadata"

#include "Metadata.h"

#include <CameraMetadata.h>
#include <log/log.h>
#include <system/camera_metadata.h>

namespace fakecamera {

using ::android::hardware::camera::common::V1_0::helper::CameraMetadata;

namespace {
// Formats offered on every size: IMPLEMENTATION_DEFINED (preview), YCbCr_420_888
// (preview/record) and BLOB (JPEG still). Numeric to avoid a graphics header dep.
constexpr int32_t kFmtImplDef = 0x22;
constexpr int32_t kFmtYuv = 0x23;
constexpr int32_t kFmtBlob = 0x21;
constexpr int64_t kFrameDurationNs = 33333333;  // ~30 fps

std::vector<uint8_t> serialize(CameraMetadata* md) {
    camera_metadata_t* raw = md->release();
    size_t n = get_camera_metadata_size(raw);
    std::vector<uint8_t> out(reinterpret_cast<uint8_t*>(raw),
                             reinterpret_cast<uint8_t*>(raw) + n);
    free_camera_metadata(raw);
    return out;
}
}  // namespace

const std::vector<std::pair<int, int>>& SupportedSizes() {
    static const std::vector<std::pair<int, int>> kSizes = {
            {1920, 1080}, {1280, 720}, {640, 480}, {320, 240}};
    return kSizes;
}

std::vector<uint8_t> BuildStaticCharacteristics(int id) {
    CameraMetadata md;
    const auto& sizes = SupportedSizes();
    const int32_t maxW = sizes.front().first;
    const int32_t maxH = sizes.front().second;

    // ---- info / lens facing / orientation ----
    uint8_t hwLevel = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED;
    md.update(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &hwLevel, 1);
    uint8_t facing = (id == 1) ? ANDROID_LENS_FACING_FRONT : ANDROID_LENS_FACING_BACK;
    md.update(ANDROID_LENS_FACING, &facing, 1);
    // 0 = aligned with the (landscape) panel, so preview fills instead of being
    // rotated to portrait and pillarboxed. 16:9 sizes match a 1920x1080 display.
    int32_t orientation = 0;
    md.update(ANDROID_SENSOR_ORIENTATION, &orientation, 1);

    // ---- stream configurations / durations ----
    std::vector<int32_t> cfgs, minDur, stall;
    for (int32_t fmt : {kFmtImplDef, kFmtYuv, kFmtBlob}) {
        for (const auto& s : sizes) {
            cfgs.insert(cfgs.end(),
                        {fmt, s.first, s.second,
                         ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT});
            minDur.insert(minDur.end(), {fmt, s.first, s.second,
                                         static_cast<int32_t>(kFrameDurationNs)});
            // Only the JPEG (BLOB) path stalls the pipeline.
            int32_t st = (fmt == kFmtBlob) ? static_cast<int32_t>(kFrameDurationNs) : 0;
            stall.insert(stall.end(), {fmt, s.first, s.second, st});
        }
    }
    md.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, cfgs.data(), cfgs.size());
    // min frame durations / stall durations are int64 tags.
    std::vector<int64_t> minDur64(minDur.begin(), minDur.end());
    std::vector<int64_t> stall64(stall.begin(), stall.end());
    md.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, minDur64.data(), minDur64.size());
    md.update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stall64.data(), stall64.size());
    uint8_t croppingType = ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY;
    md.update(ANDROID_SCALER_CROPPING_TYPE, &croppingType, 1);
    float maxZoom = 1.0f;
    md.update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &maxZoom, 1);

    // ---- sensor info ----
    int32_t activeArray[] = {0, 0, maxW, maxH};
    md.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, activeArray, 4);
    md.update(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, activeArray, 4);
    int32_t pixelArray[] = {maxW, maxH};
    md.update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixelArray, 2);
    float physicalSize[] = {3.2f, 2.4f};
    md.update(ANDROID_SENSOR_INFO_PHYSICAL_SIZE, physicalSize, 2);
    uint8_t timestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
    md.update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &timestampSource, 1);
    int64_t maxFrameDuration = 300000000;
    md.update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, &maxFrameDuration, 1);
    int32_t sensitivityRange[] = {100, 1600};
    md.update(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE, sensitivityRange, 2);
    int32_t maxAnalogSensitivity = 800;
    md.update(ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY, &maxAnalogSensitivity, 1);
    int64_t exposureRange[] = {1000, 300000000};
    md.update(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, exposureRange, 2);
    uint8_t cfa = ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_RGGB;
    md.update(ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT, &cfa, 1);
    int32_t whiteLevel = 1023;
    md.update(ANDROID_SENSOR_INFO_WHITE_LEVEL, &whiteLevel, 1);
    std::vector<int32_t> testPatternModes = {ANDROID_SENSOR_TEST_PATTERN_MODE_OFF};
    md.update(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, testPatternModes.data(),
              testPatternModes.size());

    // ---- control (3A stubs) ----
    std::vector<uint8_t> aeModes = {ANDROID_CONTROL_AE_MODE_ON};
    md.update(ANDROID_CONTROL_AE_AVAILABLE_MODES, aeModes.data(), aeModes.size());
    std::vector<uint8_t> antibanding = {ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO};
    md.update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, antibanding.data(),
              antibanding.size());
    std::vector<int32_t> fpsRanges = {15, 30, 30, 30};
    md.update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, fpsRanges.data(),
              fpsRanges.size());
    int32_t aeCompRange[] = {0, 0};
    md.update(ANDROID_CONTROL_AE_COMPENSATION_RANGE, aeCompRange, 2);
    camera_metadata_rational_t aeCompStep = {0, 1};
    md.update(ANDROID_CONTROL_AE_COMPENSATION_STEP, &aeCompStep, 1);
    uint8_t aeLockAvail = ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE;
    md.update(ANDROID_CONTROL_AE_LOCK_AVAILABLE, &aeLockAvail, 1);
    std::vector<uint8_t> afModes = {ANDROID_CONTROL_AF_MODE_OFF};
    md.update(ANDROID_CONTROL_AF_AVAILABLE_MODES, afModes.data(), afModes.size());
    std::vector<uint8_t> effects = {ANDROID_CONTROL_EFFECT_MODE_OFF};
    md.update(ANDROID_CONTROL_AVAILABLE_EFFECTS, effects.data(), effects.size());
    std::vector<uint8_t> sceneModes = {ANDROID_CONTROL_SCENE_MODE_DISABLED};
    md.update(ANDROID_CONTROL_AVAILABLE_SCENE_MODES, sceneModes.data(), sceneModes.size());
    std::vector<uint8_t> vstab = {ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF};
    md.update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, vstab.data(), vstab.size());
    std::vector<uint8_t> awbModes = {ANDROID_CONTROL_AWB_MODE_AUTO};
    md.update(ANDROID_CONTROL_AWB_AVAILABLE_MODES, awbModes.data(), awbModes.size());
    uint8_t awbLockAvail = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE;
    md.update(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &awbLockAvail, 1);
    int32_t maxRegions[] = {0, 0, 0};  // AE, AWB, AF
    md.update(ANDROID_CONTROL_MAX_REGIONS, maxRegions, 3);
    std::vector<uint8_t> controlModes = {ANDROID_CONTROL_MODE_OFF, ANDROID_CONTROL_MODE_AUTO};
    md.update(ANDROID_CONTROL_AVAILABLE_MODES, controlModes.data(), controlModes.size());

    // ---- flash / lens / jpeg / reduction modes ----
    uint8_t flashAvail = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
    md.update(ANDROID_FLASH_INFO_AVAILABLE, &flashAvail, 1);
    float apertures[] = {2.8f};
    md.update(ANDROID_LENS_INFO_AVAILABLE_APERTURES, apertures, 1);
    float focalLengths[] = {3.0f};
    md.update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, focalLengths, 1);
    std::vector<uint8_t> ois = {ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF};
    md.update(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, ois.data(), ois.size());
    float hyperfocal = 0.0f, minFocus = 0.0f;
    md.update(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, &hyperfocal, 1);
    md.update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, &minFocus, 1);
    uint8_t focusCalib = ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED;
    md.update(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION, &focusCalib, 1);

    std::vector<int32_t> thumbSizes = {0, 0, 160, 120};
    md.update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, thumbSizes.data(), thumbSizes.size());
    int32_t jpegMaxSize = maxW * maxH * 3 / 2 + 1024;
    md.update(ANDROID_JPEG_MAX_SIZE, &jpegMaxSize, 1);

    std::vector<uint8_t> nr = {ANDROID_NOISE_REDUCTION_MODE_OFF, ANDROID_NOISE_REDUCTION_MODE_FAST};
    md.update(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, nr.data(), nr.size());
    std::vector<uint8_t> edge = {ANDROID_EDGE_MODE_OFF, ANDROID_EDGE_MODE_FAST};
    md.update(ANDROID_EDGE_AVAILABLE_EDGE_MODES, edge.data(), edge.size());
    std::vector<uint8_t> abmodes = {ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF};
    md.update(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES, abmodes.data(),
              abmodes.size());

    // ---- statistics / sync ----
    std::vector<uint8_t> faceModes = {ANDROID_STATISTICS_FACE_DETECT_MODE_OFF};
    md.update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, faceModes.data(),
              faceModes.size());
    int32_t maxFaceCount = 0;
    md.update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, &maxFaceCount, 1);
    std::vector<uint8_t> lsmodes = {ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF};
    md.update(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES, lsmodes.data(),
              lsmodes.size());
    int32_t syncLatency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
    md.update(ANDROID_SYNC_MAX_LATENCY, &syncLatency, 1);

    // ---- request limits + capabilities ----
    int32_t maxOutStreams[] = {0, 2, 1};  // raw, processed(non-stalling), processed(stalling)
    md.update(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, maxOutStreams, 3);
    int32_t partialCount = 1;
    md.update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &partialCount, 1);
    uint8_t pipelineMaxDepth = 4;
    md.update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &pipelineMaxDepth, 1);
    std::vector<uint8_t> caps = {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE};
    md.update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, caps.data(), caps.size());

    std::vector<int32_t> requestKeys = {
            ANDROID_CONTROL_AE_MODE,       ANDROID_CONTROL_AWB_MODE,
            ANDROID_CONTROL_AF_MODE,       ANDROID_CONTROL_MODE,
            ANDROID_CONTROL_CAPTURE_INTENT, ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
            ANDROID_SCALER_CROP_REGION,    ANDROID_JPEG_ORIENTATION,
            ANDROID_JPEG_QUALITY,          ANDROID_JPEG_THUMBNAIL_SIZE};
    md.update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, requestKeys.data(), requestKeys.size());
    std::vector<int32_t> resultKeys = {
            ANDROID_CONTROL_AE_MODE,       ANDROID_CONTROL_AWB_MODE,
            ANDROID_CONTROL_AF_MODE,       ANDROID_CONTROL_MODE,
            ANDROID_CONTROL_AE_STATE,      ANDROID_CONTROL_AWB_STATE,
            ANDROID_CONTROL_AF_STATE,      ANDROID_SENSOR_TIMESTAMP,
            ANDROID_SENSOR_EXPOSURE_TIME,  ANDROID_SENSOR_SENSITIVITY,
            ANDROID_SENSOR_FRAME_DURATION, ANDROID_REQUEST_PIPELINE_DEPTH,
            ANDROID_FLASH_STATE,           ANDROID_FLASH_MODE,
            ANDROID_LENS_STATE};
    md.update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, resultKeys.data(), resultKeys.size());
    std::vector<int32_t> charKeys = {
            ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, ANDROID_LENS_FACING,
            ANDROID_SENSOR_ORIENTATION, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
            ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
            ANDROID_REQUEST_AVAILABLE_CAPABILITIES, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
            ANDROID_FLASH_INFO_AVAILABLE, ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES};
    md.update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, charKeys.data(), charKeys.size());
    std::vector<int32_t> sessionKeys = {};
    md.update(ANDROID_REQUEST_AVAILABLE_SESSION_KEYS, sessionKeys.data(), sessionKeys.size());

    return serialize(&md);
}

std::vector<uint8_t> BuildDefaultRequest(int /*id*/, int type) {
    CameraMetadata md;
    uint8_t ctrlMode = ANDROID_CONTROL_MODE_AUTO;
    md.update(ANDROID_CONTROL_MODE, &ctrlMode, 1);
    uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    md.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
    uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    md.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
    uint8_t afMode = ANDROID_CONTROL_AF_MODE_OFF;
    md.update(ANDROID_CONTROL_AF_MODE, &afMode, 1);
    int32_t fps[] = {30, 30};
    md.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fps, 2);
    uint8_t intent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
    switch (type) {
        case 2: intent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE; break;      // STILL_CAPTURE
        case 3: intent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD; break;        // VIDEO_RECORD
        case 4: intent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT; break;      // VIDEO_SNAPSHOT
        case 5: intent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG; break;    // ZSL
        default: intent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW; break;
    }
    md.update(ANDROID_CONTROL_CAPTURE_INTENT, &intent, 1);
    uint8_t jpegQuality = 90;
    md.update(ANDROID_JPEG_QUALITY, &jpegQuality, 1);
    int32_t jpegOrientation = 0;
    md.update(ANDROID_JPEG_ORIENTATION, &jpegOrientation, 1);
    int32_t thumb[] = {160, 120};
    md.update(ANDROID_JPEG_THUMBNAIL_SIZE, thumb, 2);
    return serialize(&md);
}

}  // namespace fakecamera
