#define LOG_TAG "fakecamera-framesource"

#include "FrameSource.h"

#include <dirent.h>
#include <setjmp.h>
#include <stdio.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>

#include <libyuv.h>
#include <log/log.h>

extern "C" {
#include <jpeglib.h>
}

namespace fakecamera {

namespace {
const char* const kBaseDirs[] = {"/data/vendor/camera", "/data/camera"};

bool IsDir(const std::string& p) {
    struct stat s;
    return stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
}
bool IsFile(const std::string& p) {
    struct stat s;
    return stat(p.c_str(), &s) == 0 && S_ISREG(s.st_mode);
}
bool HasJpegExt(const std::string& n) {
    auto d = n.rfind('.');
    if (d == std::string::npos) return false;
    std::string e = n.substr(d + 1);
    for (auto& c : e) c = tolower(c);
    return e == "jpg" || e == "jpeg";
}
std::vector<std::string> Names(int id) {
    std::vector<std::string> v;
    if (id == 0) v.push_back("back");
    else if (id == 1) v.push_back("front");
    v.push_back("cam" + std::to_string(id));
    v.push_back("default");
    return v;
}
struct JErr {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};
void JErrExit(j_common_ptr c) { longjmp(reinterpret_cast<JErr*>(c->err)->jb, 1); }
}  // namespace

std::unique_ptr<FrameSource> FrameSource::Create(int id) {
    for (const char* base : kBaseDirs) {
        if (!IsDir(base)) continue;
        for (const auto& name : Names(id)) {
            std::string dir = std::string(base) + "/" + name;
            std::string still = dir + ".jpg";
            if (IsDir(dir)) {
                std::vector<std::string> frames;
                if (DIR* d = opendir(dir.c_str())) {
                    for (struct dirent* e = readdir(d); e; e = readdir(d))
                        if (HasJpegExt(e->d_name)) frames.push_back(dir + "/" + e->d_name);
                    closedir(d);
                }
                if (!frames.empty()) {
                    std::sort(frames.begin(), frames.end());
                    auto s = std::unique_ptr<FrameSource>(new FrameSource());
                    s->frames_ = std::move(frames);
                    if (s->DecodeJpegToMaster(s->frames_[0])) {
                        ALOGI("camera %d: %zu frames from %s", id, s->frames_.size(), dir.c_str());
                        return s;
                    }
                }
            }
            if (IsFile(still)) {
                auto s = std::unique_ptr<FrameSource>(new FrameSource());
                if (s->DecodeJpegToMaster(still)) {
                    ALOGI("camera %d: still %s (%dx%d)", id, still.c_str(), s->master_w_,
                          s->master_h_);
                    return s;
                }
            }
        }
    }
    return nullptr;
}

bool FrameSource::DecodeJpegToMaster(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        ALOGE("cannot open %s", path.c_str());
        return false;
    }
    struct jpeg_decompress_struct cinfo;
    JErr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = JErrExit;
    std::vector<uint8_t> rgb;
    if (setjmp(jerr.jb)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(f);
        ALOGE("jpeg decode failed: %s", path.c_str());
        return false;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;  // bytes R,G,B == libyuv RAW
    jpeg_start_decompress(&cinfo);
    const int w = cinfo.output_width, h = cinfo.output_height, rowStride = w * 3;
    rgb.resize(static_cast<size_t>(rowStride) * h);
    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t* row = rgb.data() + static_cast<size_t>(cinfo.output_scanline) * rowStride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);

    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    master_y_.resize(static_cast<size_t>(w) * h);
    master_u_.resize(static_cast<size_t>(cw) * ch);
    master_v_.resize(static_cast<size_t>(cw) * ch);
    if (libyuv::RAWToI420(rgb.data(), rowStride, master_y_.data(), w, master_u_.data(), cw,
                          master_v_.data(), cw, w, h) != 0) {
        ALOGE("RAWToI420 failed: %s", path.c_str());
        return false;
    }
    master_w_ = w;
    master_h_ = h;
    master_valid_ = true;
    return true;
}

void FrameSource::Advance() {
    if (frames_.size() < 2) return;
    index_ = (index_ + 1) % frames_.size();
    DecodeJpegToMaster(frames_[index_]);
}

void FrameSource::GetI420(int w, int h, std::vector<uint8_t>* y, std::vector<uint8_t>* u,
                          std::vector<uint8_t>* v) {
    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    y->assign(static_cast<size_t>(w) * h, 0);
    u->assign(static_cast<size_t>(cw) * ch, 128);
    v->assign(static_cast<size_t>(cw) * ch, 128);
    if (!master_valid_) return;
    const int mcw = (master_w_ + 1) / 2;
    libyuv::I420Scale(master_y_.data(), master_w_, master_u_.data(), mcw, master_v_.data(), mcw,
                      master_w_, master_h_, y->data(), w, u->data(), cw, v->data(), cw, w, h,
                      libyuv::kFilterBilinear);
}

void FrameSource::GetRGBA(int w, int h, std::vector<uint8_t>* rgba) {
    rgba->assign(static_cast<size_t>(w) * h * 4, 0);
    if (!master_valid_) return;
    std::vector<uint8_t> y, u, v;
    GetI420(w, h, &y, &u, &v);
    const int cw = (w + 1) / 2;
    libyuv::I420ToABGR(y.data(), w, u.data(), cw, v.data(), cw, rgba->data(), w * 4, w, h);
}

}  // namespace fakecamera
