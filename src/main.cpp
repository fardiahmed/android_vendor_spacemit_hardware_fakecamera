/*
 * Fake camera provider service entry point.
 */

#define LOG_TAG "fakecamera-service"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "Provider.h"

using ::fakecamera::Provider;

int main() {
    ALOGI("fake camera provider starting");
    ABinderProcess_setThreadPoolMaxThreadCount(6);

    std::shared_ptr<Provider> provider = ndk::SharedRefBase::make<Provider>();
    const std::string name = std::string(Provider::descriptor) + "/internal/0";

    binder_exception_t ret =
            AServiceManager_addService(provider->asBinder().get(), name.c_str());
    LOG_ALWAYS_FATAL_IF(ret != EX_NONE, "fakecamera: addService failed: %d", ret);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // not reached
}
