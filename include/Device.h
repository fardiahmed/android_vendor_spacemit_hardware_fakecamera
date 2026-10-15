/*
 * ICameraDevice for one virtual camera (id 0 back, 1 front).
 */

#ifndef FAKECAMERA_DEVICE_H
#define FAKECAMERA_DEVICE_H

#include <aidl/android/hardware/camera/device/BnCameraDevice.h>

namespace fakecamera {

namespace device = ::aidl::android::hardware::camera::device;
namespace common = ::aidl::android::hardware::camera::common;

class Device : public device::BnCameraDevice {
  public:
    explicit Device(int id) : mId(id) {}

    ndk::ScopedAStatus getCameraCharacteristics(device::CameraMetadata* out) override;
    ndk::ScopedAStatus getPhysicalCameraCharacteristics(const std::string& physicalId,
                                                        device::CameraMetadata* out) override;
    ndk::ScopedAStatus getResourceCost(common::CameraResourceCost* out) override;
    ndk::ScopedAStatus isStreamCombinationSupported(const device::StreamConfiguration& streams,
                                                    bool* out) override;
    ndk::ScopedAStatus open(const std::shared_ptr<device::ICameraDeviceCallback>& cb,
                            std::shared_ptr<device::ICameraDeviceSession>* out) override;
    ndk::ScopedAStatus openInjectionSession(
            const std::shared_ptr<device::ICameraDeviceCallback>& cb,
            std::shared_ptr<device::ICameraInjectionSession>* out) override;
    ndk::ScopedAStatus setTorchMode(bool on) override;
    ndk::ScopedAStatus turnOnTorchWithStrengthLevel(int32_t level) override;
    ndk::ScopedAStatus getTorchStrengthLevel(int32_t* out) override;
    ndk::ScopedAStatus constructDefaultRequestSettings(device::RequestTemplate type,
                                                       device::CameraMetadata* out) override;
    ndk::ScopedAStatus isStreamCombinationWithSettingsSupported(
            const device::StreamConfiguration& streams, bool* out) override;
    ndk::ScopedAStatus getSessionCharacteristics(const device::StreamConfiguration& sessionConfig,
                                                 device::CameraMetadata* out) override;

  private:
    const int mId;
};

}  // namespace fakecamera

#endif  // FAKECAMERA_DEVICE_H
