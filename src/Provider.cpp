#define LOG_TAG "fakecamera-provider"

#include "Provider.h"

#include <aidl/android/hardware/camera/common/Status.h>
#include <log/log.h>
#include <regex>

#include "Device.h"

namespace fakecamera {

using ::aidl::android::hardware::camera::common::CameraDeviceStatus;
using ::aidl::android::hardware::camera::common::Status;
using ::aidl::android::hardware::camera::device::ICameraDevice;

namespace {
constexpr int kNumCameras = 2;  // 0 = back, 1 = front
const std::regex kNameRe("device@1\\.1/internal/([0-9]+)");

ndk::ScopedAStatus fromStatus(Status s) {
    return s == Status::OK
                   ? ndk::ScopedAStatus::ok()
                   : ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(s));
}
}  // namespace

std::string Provider::DeviceName(int id) {
    return "device@1.1/internal/" + std::to_string(id);
}

ndk::ScopedAStatus Provider::setCallback(
        const std::shared_ptr<provider::ICameraProviderCallback>& callback) {
    mCallback = callback;
    if (mCallback != nullptr) {
        for (int id = 0; id < kNumCameras; id++) {
            mCallback->cameraDeviceStatusChange(DeviceName(id), CameraDeviceStatus::PRESENT);
        }
    }
    return fromStatus(Status::OK);
}

ndk::ScopedAStatus Provider::getVendorTags(std::vector<common::VendorTagSection>* tags) {
    if (tags) tags->clear();
    return fromStatus(Status::OK);
}

ndk::ScopedAStatus Provider::getCameraIdList(std::vector<std::string>* list) {
    if (list == nullptr) return fromStatus(Status::ILLEGAL_ARGUMENT);
    list->clear();
    for (int id = 0; id < kNumCameras; id++) list->push_back(DeviceName(id));
    return fromStatus(Status::OK);
}

ndk::ScopedAStatus Provider::getCameraDeviceInterface(const std::string& name,
                                                      std::shared_ptr<ICameraDevice>* dev) {
    if (dev == nullptr) return fromStatus(Status::ILLEGAL_ARGUMENT);
    std::smatch m;
    if (!std::regex_match(name, m, kNameRe)) {
        ALOGE("%s: bad device name '%s'", __FUNCTION__, name.c_str());
        return fromStatus(Status::ILLEGAL_ARGUMENT);
    }
    int id = std::stoi(m[1]);
    if (id < 0 || id >= kNumCameras) return fromStatus(Status::ILLEGAL_ARGUMENT);
    *dev = ndk::SharedRefBase::make<Device>(id);
    return fromStatus(Status::OK);
}

ndk::ScopedAStatus Provider::notifyDeviceStateChange(int64_t) {
    return fromStatus(Status::OK);
}

ndk::ScopedAStatus Provider::getConcurrentCameraIds(
        std::vector<provider::ConcurrentCameraIdCombination>* idList) {
    if (idList) idList->clear();
    return fromStatus(Status::OK);
}

ndk::ScopedAStatus Provider::isConcurrentStreamCombinationSupported(
        const std::vector<provider::CameraIdAndStreamCombination>&, bool* support) {
    if (support) *support = false;
    return fromStatus(Status::OK);
}

}  // namespace fakecamera
