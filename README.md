# Fake Camera HAL (AIDL)

A small, board-neutral Android camera HAL. It implements the AIDL
`ICameraProvider` V3 and presents **two virtual cameras** — back (id 0) and
front (id 1) — whose frames come from JPEG files instead of a sensor.

## What it's for

Use it when the board has **no real / no compatible camera sensor**, or for
**testing** camera apps and pipelines without hardware. Camera apps enumerate,
preview, take photos, and record video normally; the pixels are whatever image
or video you drop on the device.


## Layout

```
fakecamera/
  Android.bp  fakecamera.xml  fakecamera-service.rc  sepolicy/file_contexts
  media_profiles_fakecamera.xml   (installed as /vendor/etc/media_profiles_V1_0.xml)
  include/    Provider.h Device.h Session.h Metadata.h FrameSource.h
  src/        main.cpp Provider.cpp Device.cpp Session.cpp Metadata.cpp FrameSource.cpp
```

`FrameSource` decodes the JPEG(s) with libjpeg and scales/converts with libyuv;
`Metadata` builds the static characteristics; `Session` fills each requested
buffer on a worker thread and reports shutter + result.

## Supplying image / video content

Frames are read from **`/data/vendor/camera/`** (falls back to `/data/camera/`).
Per camera: `back` / `front` (id 0 / id 1).

Still image (one frame, repeated):

```
adb push photo.jpg  /data/vendor/camera/back.jpg
adb push selfie.jpg /data/vendor/camera/front.jpg
```

Looping video , convert a clip to a directory of JPEG frames on the host, then
push the directory (a directory wins over the `.jpg` for that camera):

```
mkdir -p back
# 30 fps, scaled/letterboxed to the camera's 16:9 frame so it is not stretched:
ffmpeg -i clip.mp4 -vf "fps=30,scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2" -q:v 2 back/%04d.jpg
adb push back /data/vendor/camera/
```

Notes:
- Make the directory traversable and the files readable by the HAL:
  `adb shell chmod 755 /data/vendor/camera/back && adb shell chmod 644 /data/vendor/camera/back/*.jpg`
- The source should be at least the capture resolution; smaller sources are
  upscaled (soft). `fps=30` plays in real time (one source frame per sensor frame).
- Content must exist **before** the camera is opened (the source is loaded on open).

## Enable it in a device

The HAL is opt-in. In the product makefiles:

```make
PRODUCT_PACKAGES += \
    android.hardware.camera.provider-service.fakecamera \
    media_profiles_fakecamera

# Advertise camera support so apps detect front + back cameras.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.camera.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.full.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.full.xml \
    frameworks/native/data/etc/android.hardware.camera.concurrent.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.concurrent.xml

```


and in `BoardConfig*.mk` add the SELinux label for the service binary:

```make
BOARD_SEPOLICY_DIRS += vendor/spacemit/hardware/fakecamera/sepolicy
```