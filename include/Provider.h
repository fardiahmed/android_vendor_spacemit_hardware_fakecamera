/*
 * ICameraProvider: advertises the two fixed virtual cameras (back id 0, front
 * id 1) and hands out their ICameraDevice.
 */

#ifndef FAKECAMERA_PROVIDER_H
#define FAKECAMERA_PROVIDER_H

#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>

namespace fakecamera {

namespace provider = ::aidl::android::hardware::camera::provider;
namespace common = ::aidl::android::hardware::camera::common;

class Provider : public provider::BnCameraProvider {
  public:
    // "device@1.1/internal/<id>" — the names returned by getCameraIdList.
    static std::string DeviceName(int id);

    ndk::ScopedAStatus setCallback(
            const std::shared_ptr<provider::ICameraProviderCallback>& callback) override;
    ndk::ScopedAStatus getVendorTags(
            std::vector<common::VendorTagSection>* tags) override;
    ndk::ScopedAStatus getCameraIdList(std::vector<std::string>* list) override;
    ndk::ScopedAStatus getCameraDeviceInterface(
            const std::string& name,
            std::shared_ptr<::aidl::android::hardware::camera::device::ICameraDevice>* dev)
            override;
    ndk::ScopedAStatus notifyDeviceStateChange(int64_t deviceState) override;
    ndk::ScopedAStatus getConcurrentCameraIds(
            std::vector<provider::ConcurrentCameraIdCombination>* idList) override;
    ndk::ScopedAStatus isConcurrentStreamCombinationSupported(
            const std::vector<provider::CameraIdAndStreamCombination>& configs,
            bool* support) override;

  private:
    std::shared_ptr<provider::ICameraProviderCallback> mCallback;
};

}  // namespace fakecamera

#endif  // FAKECAMERA_PROVIDER_H
