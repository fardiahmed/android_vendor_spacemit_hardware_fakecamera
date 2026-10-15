#define LOG_TAG "fakecamera-session"

#include "Session.h"

#include <CameraMetadata.h>
#include <aidl/android/hardware/camera/common/Status.h>
#include <aidl/android/hardware/camera/device/BufferStatus.h>
#include <aidl/android/hardware/camera/device/CameraBlob.h>
#include <aidl/android/hardware/camera/device/CameraBlobId.h>
#include <aidl/android/hardware/camera/device/NotifyMsg.h>
#include <aidl/android/hardware/camera/device/ShutterMsg.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <android/sync.h>
#include <cutils/native_handle.h>
#include <libyuv.h>
#include <log/log.h>
#include <system/camera_metadata.h>
#include <ui/Rect.h>
#include <utils/Timers.h>

#include <cinttypes>
#include <cstring>

extern "C" {
#include <jpeglib.h>
}

#include "Metadata.h"

namespace fakecamera {

using ::aidl::android::hardware::camera::common::Status;
using ::aidl::android::hardware::camera::device::BufferStatus;
using ::aidl::android::hardware::camera::device::CameraBlob;
using ::aidl::android::hardware::camera::device::CameraBlobId;
using ::aidl::android::hardware::camera::device::CaptureResult;
using ::aidl::android::hardware::camera::device::HalStream;
using ::aidl::android::hardware::camera::device::NotifyMsg;
using ::aidl::android::hardware::camera::device::ShutterMsg;
using ::aidl::android::hardware::camera::device::StreamBuffer;
using ::aidl::android::hardware::camera::device::StreamConfiguration;

namespace {
using HelperMetadata = ::android::hardware::camera::common::V1_0::helper::CameraMetadata;
constexpr int32_t kFmtImplDef = 0x22, kFmtYuv = 0x23, kFmtBlob = 0x21;
constexpr uint64_t kCpuUsage = 0x3 | 0x30;  // SW read-often | write-often

ndk::ScopedAStatus ok() { return ndk::ScopedAStatus::ok(); }

size_t jpegCapacity() {
    const auto& mx = SupportedSizes().front();
    return static_cast<size_t>(mx.first) * mx.second * 3 / 2 + 1024;
}
}  // namespace

void Session::initialize() {
    static constexpr size_t kQueueSize = 1 << 20;
    mRequestMQ = std::make_unique<MetadataQueue>(kQueueSize, false);
    mResultMQ = std::make_unique<MetadataQueue>(kQueueSize, false);
    mSource = FrameSource::Create(mId);
    ALOGI("camera %d session: source %s", mId, mSource ? "loaded" : "none (gray frames)");
    mStop = false;
    mWorker = std::thread(&Session::workerLoop, this);
}

ndk::ScopedAStatus Session::getCaptureRequestMetadataQueue(MQDesc<int8_t>* out) {
    *out = mRequestMQ->dupeDesc();
    return ok();
}
ndk::ScopedAStatus Session::getCaptureResultMetadataQueue(MQDesc<int8_t>* out) {
    *out = mResultMQ->dupeDesc();
    return ok();
}

ndk::ScopedAStatus Session::constructDefaultRequestSettings(device::RequestTemplate type,
                                                            device::CameraMetadata* out) {
    out->metadata = BuildDefaultRequest(mId, static_cast<int>(type));
    return ok();
}

ndk::ScopedAStatus Session::configureStreams(const StreamConfiguration& req,
                                             std::vector<HalStream>* out) {
    std::lock_guard<std::mutex> lk(mMutex);
    mStreams.clear();
    out->clear();
    for (const auto& s : req.streams) {
        int32_t fmt = static_cast<int32_t>(s.format);
        if (fmt == kFmtImplDef) fmt = kFmtYuv;  // resolve to a lockable YUV format
        mStreams[s.id] = {fmt, s.width, s.height, static_cast<int64_t>(s.usage)};
        HalStream hs;
        hs.id = s.id;
        hs.overrideFormat =
                static_cast<::aidl::android::hardware::graphics::common::PixelFormat>(fmt);
        hs.overrideDataSpace = s.dataSpace;
        hs.producerUsage = s.usage;
        hs.consumerUsage = static_cast<::aidl::android::hardware::graphics::common::BufferUsage>(0);
        hs.maxBuffers = 4;
        hs.physicalCameraId = s.physicalCameraId;
        hs.supportOffline = false;
        out->push_back(hs);
    }
    return ok();
}

ndk::ScopedAStatus Session::configureStreamsV2(const StreamConfiguration& req,
                                               device::ConfigureStreamsRet* out) {
    return configureStreams(req, &out->halStreams);
}

ndk::ScopedAStatus Session::isReconfigurationRequired(const device::CameraMetadata&,
                                                      const device::CameraMetadata&, bool* out) {
    *out = true;
    return ok();
}

buffer_handle_t Session::importBuffer(int32_t streamId, const StreamBuffer& sb) {
    auto& perStream = mBuffers[streamId];
    auto it = perStream.find(sb.bufferId);
    if (it != perStream.end()) return it->second;  // already imported
    native_handle_t* h = ::android::makeFromAidl(sb.buffer);
    buffer_handle_t imported = h;
    {
        std::lock_guard<std::mutex> lk(mImpMutex);
        if (!mImporter.importBuffer(imported) || imported == nullptr) {
            ALOGE("import failed stream %d buf %" PRId64, streamId, sb.bufferId);
            native_handle_delete(h);
            return nullptr;
        }
    }
    native_handle_delete(h);
    perStream[sb.bufferId] = imported;
    return imported;
}

void Session::fillYuv(const android_ycbcr& dst, int w, int h) {
    std::vector<uint8_t> y, u, v;
    if (mSource) {
        mSource->GetI420(w, h, &y, &u, &v);
    } else {
        y.assign(static_cast<size_t>(w) * h, 0);
        u.assign(static_cast<size_t>((w + 1) / 2) * ((h + 1) / 2), 128);
        v = u;
    }
    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    for (int r = 0; r < h; r++)
        memcpy(static_cast<uint8_t*>(dst.y) + r * dst.ystride, y.data() + r * w, w);
    uint8_t* cb = static_cast<uint8_t*>(dst.cb);
    uint8_t* cr = static_cast<uint8_t*>(dst.cr);
    for (int r = 0; r < ch; r++) {
        for (int c = 0; c < cw; c++) {
            cb[r * dst.cstride + c * dst.chroma_step] = u[r * cw + c];
            cr[r * dst.cstride + c * dst.chroma_step] = v[r * cw + c];
        }
    }
}

void Session::writeJpeg(void* dst, size_t capacity, int w, int h) {
    std::vector<uint8_t> y, u, v;
    if (mSource)
        mSource->GetI420(w, h, &y, &u, &v);
    else {
        y.assign(static_cast<size_t>(w) * h, 0);
        u.assign(static_cast<size_t>((w + 1) / 2) * ((h + 1) / 2), 128);
        v = u;
    }
    const int cw = (w + 1) / 2;
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    libyuv::I420ToRAW(y.data(), w, u.data(), cw, v.data(), cw, rgb.data(), w * 3, w, h);

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    uint8_t* out = nullptr;
    unsigned long outSize = 0;
    jpeg_mem_dest(&cinfo, &out, &outSize);
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 90, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = rgb.data() + static_cast<size_t>(cinfo.next_scanline) * w * 3;
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    size_t jpegSize = outSize;
    if (jpegSize + sizeof(CameraBlob) > capacity) jpegSize = capacity - sizeof(CameraBlob);
    memcpy(dst, out, jpegSize);
    CameraBlob blob{CameraBlobId::JPEG, static_cast<int32_t>(jpegSize)};
    memcpy(static_cast<uint8_t*>(dst) + capacity - sizeof(CameraBlob), &blob, sizeof(CameraBlob));
    free(out);
}

std::vector<uint8_t> Session::buildResultMetadata(int64_t timestamp) {
    HelperMetadata md;
    md.update(ANDROID_SENSOR_TIMESTAMP, &timestamp, 1);
    uint8_t pipelineDepth = 4;
    md.update(ANDROID_REQUEST_PIPELINE_DEPTH, &pipelineDepth, 1);
    uint8_t ctrlMode = ANDROID_CONTROL_MODE_AUTO;
    md.update(ANDROID_CONTROL_MODE, &ctrlMode, 1);
    uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON, aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
    md.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
    md.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
    uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO, awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    md.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
    md.update(ANDROID_CONTROL_AWB_STATE, &awbState, 1);
    uint8_t afMode = ANDROID_CONTROL_AF_MODE_OFF, afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
    md.update(ANDROID_CONTROL_AF_MODE, &afMode, 1);
    md.update(ANDROID_CONTROL_AF_STATE, &afState, 1);
    uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;
    md.update(ANDROID_FLASH_STATE, &flashState, 1);
    uint8_t flashMode = ANDROID_FLASH_MODE_OFF;  // apps read+unbox this for EXIF
    md.update(ANDROID_FLASH_MODE, &flashMode, 1);
    uint8_t lensState = ANDROID_LENS_STATE_STATIONARY;
    md.update(ANDROID_LENS_STATE, &lensState, 1);
    int64_t exposure = 16000000, frameDuration = 33333333;
    int32_t sensitivity = 100;
    md.update(ANDROID_SENSOR_EXPOSURE_TIME, &exposure, 1);
    md.update(ANDROID_SENSOR_FRAME_DURATION, &frameDuration, 1);
    md.update(ANDROID_SENSOR_SENSITIVITY, &sensitivity, 1);
    camera_metadata_t* raw = md.release();
    size_t n = get_camera_metadata_size(raw);
    std::vector<uint8_t> out(reinterpret_cast<uint8_t*>(raw), reinterpret_cast<uint8_t*>(raw) + n);
    free_camera_metadata(raw);
    return out;
}

ndk::ScopedAStatus Session::processCaptureRequest(
        const std::vector<device::CaptureRequest>& requests,
        const std::vector<device::BufferCache>&, int32_t* out) {
    {
        std::lock_guard<std::mutex> lk(mMutex);
        for (const auto& req : requests) {
            if (req.fmqSettingsSize > 0) {  // drain settings FMQ (ignored)
                std::vector<int8_t> tmp(req.fmqSettingsSize);
                mRequestMQ->read(tmp.data(), req.fmqSettingsSize);
            }
            Job job;
            job.frameNumber = req.frameNumber;
            for (const auto& sb : req.outputBuffers) {
                // Wait the acquire fence here (fd is valid during this call).
                native_handle_t* fh = ::android::makeFromAidl(sb.acquireFence);
                if (fh && fh->numFds == 1 && fh->data[0] >= 0) sync_wait(fh->data[0], 1000);
                native_handle_delete(fh);

                buffer_handle_t buf = importBuffer(sb.streamId, sb);
                auto sit = mStreams.find(sb.streamId);
                job.buffers.push_back({sb.streamId, sb.bufferId, buf,
                                       sit != mStreams.end() ? sit->second : StreamInfo{}});
            }
            mJobs.push(std::move(job));
        }
        *out = static_cast<int32_t>(requests.size());
    }
    mCv.notify_all();
    return ok();
}

void Session::workerLoop() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mCv.wait(lk, [&] { return mStop || !mJobs.empty(); });
            if (mStop && mJobs.empty()) return;
            job = std::move(mJobs.front());
            mJobs.pop();
        }
        processJob(job);
    }
}

void Session::processJob(const Job& job) {
    const int64_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);

    NotifyMsg shutter;
    shutter.set<NotifyMsg::Tag::shutter>(
            ShutterMsg{.frameNumber = job.frameNumber, .timestamp = timestamp});
    mCallback->notify({shutter});

    if (mSource) mSource->Advance();

    std::vector<StreamBuffer> outBufs;
    for (const auto& jb : job.buffers) {
        StreamBuffer rb;
        rb.streamId = jb.streamId;
        rb.bufferId = jb.bufferId;
        rb.status = BufferStatus::OK;
        rb.releaseFence = ::aidl::android::hardware::common::NativeHandle();
        if (jb.handle != nullptr) {
            buffer_handle_t buf = jb.handle;
            std::lock_guard<std::mutex> lk(mImpMutex);
            if (jb.info.format == kFmtBlob) {
                size_t cap = jpegCapacity();
                void* p = mImporter.lock(buf, kCpuUsage, cap);
                if (p) writeJpeg(p, cap, jb.info.width, jb.info.height);
                mImporter.unlock(buf);
            } else {
                ::android::Rect region(0, 0, jb.info.width, jb.info.height);
                android_ycbcr yc = mImporter.lockYCbCr(buf, kCpuUsage, region);
                if (yc.y) fillYuv(yc, jb.info.width, jb.info.height);
                mImporter.unlock(buf);
            }
        } else {
            rb.status = BufferStatus::ERROR;
        }
        outBufs.push_back(std::move(rb));
    }

    CaptureResult result;
    result.frameNumber = job.frameNumber;
    result.result.metadata = buildResultMetadata(timestamp);
    result.outputBuffers = std::move(outBufs);
    // No input buffer: streamId -1, else cameraserver derefs a null input stream.
    result.inputBuffer.streamId = -1;
    result.inputBuffer.bufferId = 0;
    result.partialResult = 1;
    result.fmqResultSize = 0;
    std::vector<CaptureResult> results;
    results.push_back(std::move(result));
    mCallback->processCaptureResult(results);
}

ndk::ScopedAStatus Session::signalStreamFlush(const std::vector<int32_t>&, int32_t) {
    return ok();
}
ndk::ScopedAStatus Session::switchToOffline(const std::vector<int32_t>&,
                                            device::CameraOfflineSessionInfo*,
                                            std::shared_ptr<device::ICameraOfflineSession>* out) {
    *out = nullptr;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}
ndk::ScopedAStatus Session::repeatingRequestEnd(int32_t, const std::vector<int32_t>&) {
    return ok();
}
ndk::ScopedAStatus Session::flush() { return ok(); }

Session::~Session() {
    {
        std::lock_guard<std::mutex> lk(mMutex);
        mStop = true;
    }
    mCv.notify_all();
    if (mWorker.joinable()) mWorker.join();
}

ndk::ScopedAStatus Session::close() {
    {
        std::lock_guard<std::mutex> lk(mMutex);
        mStop = true;
    }
    mCv.notify_all();
    if (mWorker.joinable()) mWorker.join();
    std::lock_guard<std::mutex> lk(mImpMutex);
    for (auto& perStream : mBuffers)
        for (auto& kv : perStream.second) mImporter.freeBuffer(kv.second);
    mBuffers.clear();
    mStreams.clear();
    mRequestMQ.reset();
    mResultMQ.reset();
    return ok();
}

}  // namespace fakecamera
