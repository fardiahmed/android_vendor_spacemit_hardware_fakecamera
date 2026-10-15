/*
 * ICameraDeviceSession (AIDL V3): fills output buffers from the file-backed
 * FrameSource and reports shutter + result for each request.
 */

#ifndef FAKECAMERA_SESSION_H
#define FAKECAMERA_SESSION_H

#include <HandleImporter.h>
#include <aidl/android/hardware/camera/device/BnCameraDeviceSession.h>
#include <fmq/AidlMessageQueue.h>
#include <system/graphics.h>

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "FrameSource.h"

namespace fakecamera {

namespace device = ::aidl::android::hardware::camera::device;
using ::aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using MetadataQueue = ::android::AidlMessageQueue<int8_t, SynchronizedReadWrite>;
template <typename T>
using MQDesc = ::aidl::android::hardware::common::fmq::MQDescriptor<T, SynchronizedReadWrite>;

class Session : public device::BnCameraDeviceSession {
  public:
    Session(int id, std::shared_ptr<device::ICameraDeviceCallback> cb)
        : mId(id), mCallback(std::move(cb)) {}
    ~Session();
    void initialize();

    ndk::ScopedAStatus close() override;
    ndk::ScopedAStatus configureStreams(const device::StreamConfiguration& req,
                                        std::vector<device::HalStream>* out) override;
    ndk::ScopedAStatus configureStreamsV2(const device::StreamConfiguration& req,
                                          device::ConfigureStreamsRet* out) override;
    ndk::ScopedAStatus constructDefaultRequestSettings(device::RequestTemplate type,
                                                       device::CameraMetadata* out) override;
    ndk::ScopedAStatus flush() override;
    ndk::ScopedAStatus getCaptureRequestMetadataQueue(MQDesc<int8_t>* out) override;
    ndk::ScopedAStatus getCaptureResultMetadataQueue(MQDesc<int8_t>* out) override;
    ndk::ScopedAStatus isReconfigurationRequired(const device::CameraMetadata& oldParams,
                                                 const device::CameraMetadata& newParams,
                                                 bool* out) override;
    ndk::ScopedAStatus processCaptureRequest(const std::vector<device::CaptureRequest>& requests,
                                             const std::vector<device::BufferCache>& cachesToRemove,
                                             int32_t* out) override;
    ndk::ScopedAStatus signalStreamFlush(const std::vector<int32_t>& streamIds,
                                         int32_t streamConfigCounter) override;
    ndk::ScopedAStatus switchToOffline(
            const std::vector<int32_t>& streamsToKeep,
            device::CameraOfflineSessionInfo* offlineSessionInfo,
            std::shared_ptr<device::ICameraOfflineSession>* out) override;
    ndk::ScopedAStatus repeatingRequestEnd(int32_t frameNumber,
                                           const std::vector<int32_t>& streamIds) override;

  private:
    struct StreamInfo {
        int32_t format;  // concrete format (IMPLEMENTATION_DEFINED already resolved to YUV)
        int32_t width;
        int32_t height;
        int64_t usage;
    };
    struct JobBuffer {
        int32_t streamId;
        int64_t bufferId;
        buffer_handle_t handle;
        StreamInfo info;
    };
    struct Job {
        int32_t frameNumber;
        std::vector<JobBuffer> buffers;
    };
    // Import (and cache) the buffer for a request's StreamBuffer; empty handle => use cache.
    buffer_handle_t importBuffer(int32_t streamId, const device::StreamBuffer& sb);
    void fillYuv(const android_ycbcr& dst, int w, int h);
    void writeJpeg(void* dst, size_t capacity, int w, int h);
    std::vector<uint8_t> buildResultMetadata(int64_t timestamp);
    void workerLoop();          // async: fills buffers + sends shutter/result
    void processJob(const Job& job);

    const int mId;
    std::shared_ptr<device::ICameraDeviceCallback> mCallback;
    std::unique_ptr<MetadataQueue> mRequestMQ;
    std::unique_ptr<MetadataQueue> mResultMQ;
    std::unique_ptr<FrameSource> mSource;
    ::android::hardware::camera::common::V1_0::helper::HandleImporter mImporter;
    std::mutex mImpMutex;  // serializes mImporter (import on caller, lock on worker)
    std::map<int32_t, StreamInfo> mStreams;
    std::map<int32_t, std::map<int64_t, buffer_handle_t>> mBuffers;
    std::mutex mMutex;

    std::thread mWorker;
    std::condition_variable mCv;
    std::queue<Job> mJobs;
    bool mStop = false;
};

}  // namespace fakecamera

#endif  // FAKECAMERA_SESSION_H
