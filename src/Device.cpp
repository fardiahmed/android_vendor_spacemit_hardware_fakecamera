#define LOG_TAG "fakecamera-device"

#include "Device.h"

#include <aidl/android/hardware/camera/common/Status.h>
#include <log/log.h>

#include "Metadata.h"
#include "Session.h"

namespace fakecamera {

using ::aidl::android::hardware::camera::common::Status;

static ndk::ScopedAStatus st(Status s) {
    return s == Status::OK
                   ? ndk::ScopedAStatus::ok()
                   : ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(s));
}

ndk::ScopedAStatus Device::getCameraCharacteristics(device::CameraMetadata* out) {
    out->metadata = BuildStaticCharacteristics(mId);
    return st(Status::OK);
}

ndk::ScopedAStatus Device::getPhysicalCameraCharacteristics(const std::string&,
                                                            device::CameraMetadata*) {
    return st(Status::ILLEGAL_ARGUMENT);  // no physical sub-cameras
}

ndk::ScopedAStatus Device::getResourceCost(common::CameraResourceCost* out) {
    out->resourceCost = 50;
    out->conflictingDevices.clear();
    return st(Status::OK);
}

ndk::ScopedAStatus Device::isStreamCombinationSupported(const device::StreamConfiguration&,
                                                        bool* out) {
    *out = true;
    return st(Status::OK);
}

ndk::ScopedAStatus Device::open(const std::shared_ptr<device::ICameraDeviceCallback>& cb,
                                std::shared_ptr<device::ICameraDeviceSession>* out) {
    auto session = ndk::SharedRefBase::make<Session>(mId, cb);
    session->initialize();
    *out = session;
    return st(Status::OK);
}

ndk::ScopedAStatus Device::openInjectionSession(
        const std::shared_ptr<device::ICameraDeviceCallback>&,
        std::shared_ptr<device::ICameraInjectionSession>* out) {
    *out = nullptr;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Device::setTorchMode(bool) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Device::turnOnTorchWithStrengthLevel(int32_t) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Device::getTorchStrengthLevel(int32_t* out) {
    *out = 0;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Device::constructDefaultRequestSettings(device::RequestTemplate type,
                                                           device::CameraMetadata* out) {
    out->metadata = BuildDefaultRequest(mId, static_cast<int>(type));
    return st(Status::OK);
}

ndk::ScopedAStatus Device::isStreamCombinationWithSettingsSupported(
        const device::StreamConfiguration&, bool* out) {
    *out = true;
    return st(Status::OK);
}

ndk::ScopedAStatus Device::getSessionCharacteristics(const device::StreamConfiguration&,
                                                     device::CameraMetadata* out) {
    out->metadata = BuildStaticCharacteristics(mId);
    return st(Status::OK);
}

}  // namespace fakecamera
