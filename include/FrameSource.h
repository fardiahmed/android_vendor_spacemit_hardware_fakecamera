/*
 * File-backed frame source: JPEG still or looped JPEG sequence from
 * /data/vendor/camera, decoded (libjpeg) to an I420 master and scaled (libyuv)
 * to whatever the stream asks for. Self-contained (no EmulatedCamera deps).
 */

#ifndef FAKECAMERA_FRAMESOURCE_H
#define FAKECAMERA_FRAMESOURCE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fakecamera {

class FrameSource {
  public:
    // id 0 -> back, 1 -> front. nullptr if no file for this camera.
    static std::unique_ptr<FrameSource> Create(int id);

    bool Active() const { return master_valid_; }
    void Advance();  // next frame of a sequence (no-op for a still)

    // Scaled I420 at w x h into contiguous y (w*h), u/v ((w/2)*(h/2)).
    void GetI420(int w, int h, std::vector<uint8_t>* y, std::vector<uint8_t>* u,
                 std::vector<uint8_t>* v);
    // Scaled RGBA (bytes R,G,B,A), w*h*4 bytes.
    void GetRGBA(int w, int h, std::vector<uint8_t>* rgba);

  private:
    bool DecodeJpegToMaster(const std::string& path);

    std::vector<uint8_t> master_y_, master_u_, master_v_;
    int master_w_ = 0, master_h_ = 0;
    bool master_valid_ = false;
    std::vector<std::string> frames_;
    size_t index_ = 0;
};

}  // namespace fakecamera

#endif  // FAKECAMERA_FRAMESOURCE_H
