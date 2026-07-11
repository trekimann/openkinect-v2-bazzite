#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/packet_pipeline.h>
#if __has_include(<libfreenect2/opencl_packet_pipeline.h>)
#include <libfreenect2/opencl_packet_pipeline.h>
#define OPENKINECT_HAS_OPENCL 1
#endif
#if __has_include(<libfreenect2/opengl_packet_pipeline.h>)
#include <libfreenect2/opengl_packet_pipeline.h>
#define OPENKINECT_HAS_OPENGL 1
#endif
#if __has_include(<libfreenect2/cpu_packet_pipeline.h>)
#include <libfreenect2/cpu_packet_pipeline.h>
#endif
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <linux/videodev2.h>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr double kYRed = 0.257;
constexpr double kYGreen = 0.504;
constexpr double kYBlue = 0.098;
constexpr double kYBias = 16.0;
constexpr double kURed = -0.148;
constexpr double kUGreen = -0.291;
constexpr double kUBlue = 0.439;
constexpr double kVRed = 0.439;
constexpr double kVGreen = -0.368;
constexpr double kVBlue = -0.071;
constexpr double kUvBias = 128.0;

volatile sig_atomic_t stop_requested = 0;

void handle_signal(int) {
    stop_requested = 1;
}

std::string trim(const std::string &value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool parse_bool(const std::string &value, bool default_value) {
    const std::string normalized = trim(value);
    if (normalized == "1" || normalized == "true" || normalized == "TRUE" || normalized == "yes") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "FALSE" || normalized == "no") {
        return false;
    }
    return default_value;
}

int parse_int(const std::string &value, int default_value) {
    try {
        return std::stoi(trim(value));
    } catch (...) {
        return default_value;
    }
}

float parse_float(const std::string &value, float default_value) {
    try {
        return std::stof(trim(value));
    } catch (...) {
        return default_value;
    }
}

struct Config {
    std::string pipeline = "auto";
    bool enable_color = true;
    bool enable_ir = false;
    bool enable_depth = false;
    std::string color_label = "Kinect_Color";
    std::string ir_label = "Kinect_IR";
    std::string depth_label = "Kinect_Depth";
    std::string color_device;
    std::string ir_device;
    std::string depth_device;
    int color_width = 1920;
    int color_height = 1080;
    int ir_width = 512;
    int ir_height = 424;
    int depth_width = 512;
    int depth_height = 424;
    float depth_min_mm = 500.0f;
    float depth_max_mm = 4500.0f;
};

Config load_config(const std::string &path) {
    Config config;
    std::ifstream input(path);
    if (!input.is_open()) {
        return config;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, separator));
        std::string value = trim(line.substr(separator + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "PIPELINE") config.pipeline = value;
        else if (key == "ENABLE_COLOR") config.enable_color = parse_bool(value, config.enable_color);
        else if (key == "ENABLE_IR") config.enable_ir = parse_bool(value, config.enable_ir);
        else if (key == "ENABLE_DEPTH") config.enable_depth = parse_bool(value, config.enable_depth);
        else if (key == "COLOR_LABEL") config.color_label = value;
        else if (key == "IR_LABEL") config.ir_label = value;
        else if (key == "DEPTH_LABEL") config.depth_label = value;
        else if (key == "COLOR_DEVICE") config.color_device = value;
        else if (key == "IR_DEVICE") config.ir_device = value;
        else if (key == "DEPTH_DEVICE") config.depth_device = value;
        else if (key == "COLOR_WIDTH") config.color_width = parse_int(value, config.color_width);
        else if (key == "COLOR_HEIGHT") config.color_height = parse_int(value, config.color_height);
        else if (key == "IR_WIDTH") config.ir_width = parse_int(value, config.ir_width);
        else if (key == "IR_HEIGHT") config.ir_height = parse_int(value, config.ir_height);
        else if (key == "DEPTH_WIDTH") config.depth_width = parse_int(value, config.depth_width);
        else if (key == "DEPTH_HEIGHT") config.depth_height = parse_int(value, config.depth_height);
        else if (key == "DEPTH_MIN_MM") config.depth_min_mm = parse_float(value, config.depth_min_mm);
        else if (key == "DEPTH_MAX_MM") config.depth_max_mm = parse_float(value, config.depth_max_mm);
    }

    return config;
}

std::optional<std::string> resolve_video_device_by_label(const std::string &label) {
    namespace fs = std::filesystem;
    const fs::path root("/sys/class/video4linux");
    if (!fs::exists(root)) {
        return std::nullopt;
    }

    for (const auto &entry : fs::directory_iterator(root)) {
        std::ifstream name_file(entry.path() / "name");
        std::string current_name;
        std::getline(name_file, current_name);
        if (trim(current_name) == label) {
            return std::string("/dev/") + entry.path().filename().string();
        }
    }

    return std::nullopt;
}

struct OutputDevice {
    std::string name;
    std::string label;
    std::string path;
    int width = 0;
    int height = 0;
    int fd = -1;
    std::vector<uint8_t> buffer;

    bool open_device() {
        fd = open(path.c_str(), O_RDWR);
        if (fd < 0) {
            std::cerr << "Failed to open " << name << " device at " << path << std::endl;
            return false;
        }

        struct v4l2_format format;
        std::memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        format.fmt.pix.width = width;
        format.fmt.pix.height = height;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.sizeimage = width * height * 2;
        format.fmt.pix.field = V4L2_FIELD_NONE;

        if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
            std::cerr << "Failed to set V4L2 format for " << name << std::endl;
            close(fd);
            fd = -1;
            return false;
        }

        buffer.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 2U);
        std::cout << "Streaming " << name << " to " << path << " (label " << label << ")" << std::endl;
        return true;
    }

    void close_device() {
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }

    bool write_frame() {
        const ssize_t expected = static_cast<ssize_t>(buffer.size());
        const ssize_t written = write(fd, buffer.data(), buffer.size());
        if (written != expected) {
            std::cerr << "Short write to " << name << " output" << std::endl;
            return false;
        }
        return true;
    }
};

void rgb_to_yuyv(const uint8_t *bgrx, std::vector<uint8_t> &yuyv, int width, int height) {
    size_t output_index = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; x += 2) {
            const int index0 = (y * width + x) * 4;
            const int paired_x = (x + 1 < width) ? x + 1 : x;
            const int index1 = (y * width + paired_x) * 4;
            const int b0 = bgrx[index0 + 0];
            const int g0 = bgrx[index0 + 1];
            const int r0 = bgrx[index0 + 2];
            const int b1 = bgrx[index1 + 0];
            const int g1 = bgrx[index1 + 1];
            const int r1 = bgrx[index1 + 2];

            const int y0 = std::clamp(static_cast<int>(kYRed * r0 + kYGreen * g0 + kYBlue * b0 + kYBias), 0, 255);
            const int y1 = std::clamp(static_cast<int>(kYRed * r1 + kYGreen * g1 + kYBlue * b1 + kYBias), 0, 255);
            const int u = std::clamp(static_cast<int>((kURed * r0 + kUGreen * g0 + kUBlue * b0 + kURed * r1 + kUGreen * g1 + kUBlue * b1) / 2.0 + kUvBias), 0, 255);
            const int v = std::clamp(static_cast<int>((kVRed * r0 + kVGreen * g0 + kVBlue * b0 + kVRed * r1 + kVGreen * g1 + kVBlue * b1) / 2.0 + kUvBias), 0, 255);

            yuyv[output_index++] = static_cast<uint8_t>(y0);
            yuyv[output_index++] = static_cast<uint8_t>(u);
            yuyv[output_index++] = static_cast<uint8_t>(y1);
            yuyv[output_index++] = static_cast<uint8_t>(v);
        }
    }
}

void grayscale_to_yuyv(const std::vector<uint8_t> &gray, std::vector<uint8_t> &yuyv) {
    size_t output_index = 0;
    for (size_t input_index = 0; input_index < gray.size(); input_index += 2) {
        const uint8_t y0 = gray[input_index];
        const size_t second_index = (input_index + 1 < gray.size()) ? input_index + 1 : input_index;
        const uint8_t y1 = gray[second_index];
        yuyv[output_index++] = y0;
        yuyv[output_index++] = 128;
        yuyv[output_index++] = y1;
        yuyv[output_index++] = 128;
    }
}

std::vector<uint8_t> normalize_ir_frame(const libfreenect2::Frame *frame) {
    const auto *input = reinterpret_cast<const float *>(frame->data);
    std::vector<uint8_t> gray(static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height));

    float min_value = std::numeric_limits<float>::max();
    float max_value = std::numeric_limits<float>::lowest();
    for (size_t i = 0; i < gray.size(); ++i) {
        const float value = input[i];
        if (std::isfinite(value)) {
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
        }
    }

    if (max_value <= min_value) {
        std::fill(gray.begin(), gray.end(), 0);
        return gray;
    }

    const float scale = 255.0f / (max_value - min_value);
    for (size_t i = 0; i < gray.size(); ++i) {
        const float value = input[i];
        gray[i] = std::isfinite(value)
            ? static_cast<uint8_t>(std::clamp((value - min_value) * scale, 0.0f, 255.0f))
            : 0;
    }

    return gray;
}

void write_depth_color(float normalized, uint8_t &r, uint8_t &g, uint8_t &b) {
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    if (normalized < 0.25f) {
        r = 0;
        g = static_cast<uint8_t>(normalized / 0.25f * 255.0f);
        b = 255;
    } else if (normalized < 0.5f) {
        r = 0;
        g = 255;
        b = static_cast<uint8_t>((1.0f - (normalized - 0.25f) / 0.25f) * 255.0f);
    } else if (normalized < 0.75f) {
        r = static_cast<uint8_t>(((normalized - 0.5f) / 0.25f) * 255.0f);
        g = 255;
        b = 0;
    } else {
        r = 255;
        g = static_cast<uint8_t>((1.0f - (normalized - 0.75f) / 0.25f) * 255.0f);
        b = 0;
    }
}

std::vector<uint8_t> colorize_depth_frame(const libfreenect2::Frame *frame, float min_mm, float max_mm) {
    const auto *input = reinterpret_cast<const float *>(frame->data);
    std::vector<uint8_t> bgrx(static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height) * 4U, 0);
    const float range = std::max(1.0f, max_mm - min_mm);

    for (size_t pixel = 0; pixel < static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height); ++pixel) {
        const float value = input[pixel];
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        if (std::isfinite(value) && value >= min_mm && value <= max_mm) {
            write_depth_color((value - min_mm) / range, r, g, b);
        }
        const size_t base = pixel * 4;
        bgrx[base + 0] = b;
        bgrx[base + 1] = g;
        bgrx[base + 2] = r;
        bgrx[base + 3] = 255;
    }

    return bgrx;
}

std::unique_ptr<libfreenect2::PacketPipeline> make_pipeline(const std::string &requested) {
    const std::string pipeline = requested.empty() ? "auto" : requested;
#if defined(OPENKINECT_HAS_OPENCL)
    if (pipeline == "opencl" || pipeline == "auto") {
        try {
            std::cout << "Using OpenCL packet pipeline" << std::endl;
            return std::make_unique<libfreenect2::OpenCLPacketPipeline>();
        } catch (...) {
            if (pipeline == "opencl") throw;
        }
    }
#endif
#if defined(OPENKINECT_HAS_OPENGL)
    if (pipeline == "opengl" || pipeline == "auto") {
        try {
            std::cout << "Using OpenGL packet pipeline" << std::endl;
            return std::make_unique<libfreenect2::OpenGLPacketPipeline>();
        } catch (...) {
            if (pipeline == "opengl") throw;
        }
    }
#endif
    std::cout << "Using CPU packet pipeline" << std::endl;
    return std::make_unique<libfreenect2::CpuPacketPipeline>();
}

bool configure_output(OutputDevice &device) {
    if (!device.path.empty()) {
        return device.open_device();
    }

    auto resolved = resolve_video_device_by_label(device.label);
    if (!resolved) {
        std::cerr << "Unable to locate V4L2 loopback device labeled " << device.label << std::endl;
        return false;
    }

    device.path = *resolved;
    return device.open_device();
}

void close_outputs(OutputDevice &color_output, OutputDevice &ir_output, OutputDevice &depth_output) {
    color_output.close_device();
    ir_output.close_device();
    depth_output.close_device();
}

}  // namespace

int main(int argc, char *argv[]) {
    std::string config_path = "/etc/openkinect-v2/openkinect-v2.conf";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    Config config = load_config(config_path);
    if (!config.enable_color && !config.enable_ir && !config.enable_depth) {
        std::cerr << "At least one stream must be enabled in " << config_path << std::endl;
        return 1;
    }

    libfreenect2::Freenect2 freenect2;
    if (freenect2.enumerateDevices() == 0) {
        std::cerr << "No Kinect v2 device found" << std::endl;
        return 1;
    }

    const std::string serial = freenect2.getDefaultDeviceSerialNumber();
    auto pipeline = make_pipeline(config.pipeline);
    libfreenect2::PacketPipeline *pipeline_ptr = pipeline.release();
    libfreenect2::Freenect2Device *device = freenect2.openDevice(serial, pipeline_ptr);
    if (!device) {
        delete pipeline_ptr;
        std::cerr << "Failed to open Kinect device" << std::endl;
        return 1;
    }

    int frame_types = 0;
    if (config.enable_color) frame_types |= libfreenect2::Frame::Color;
    if (config.enable_ir) frame_types |= libfreenect2::Frame::Ir;
    if (config.enable_depth) frame_types |= libfreenect2::Frame::Depth;

    libfreenect2::SyncMultiFrameListener listener(frame_types);
    libfreenect2::FrameMap frames;
    if (config.enable_color) device->setColorFrameListener(&listener);
    if (config.enable_ir || config.enable_depth) device->setIrAndDepthFrameListener(&listener);

    if (!device->startStreams(config.enable_color, config.enable_ir || config.enable_depth)) {
        std::cerr << "Failed to start Kinect streams" << std::endl;
        device->close();
        return 1;
    }

    OutputDevice color_output{"color", config.color_label, config.color_device, config.color_width, config.color_height};
    OutputDevice ir_output{"ir", config.ir_label, config.ir_device, config.ir_width, config.ir_height};
    OutputDevice depth_output{"depth", config.depth_label, config.depth_device, config.depth_width, config.depth_height};

    if (config.enable_color && !configure_output(color_output)) {
        device->stop();
        device->close();
        return 1;
    }
    if (config.enable_ir && !configure_output(ir_output)) {
        close_outputs(color_output, ir_output, depth_output);
        device->stop();
        device->close();
        return 1;
    }
    if (config.enable_depth && !configure_output(depth_output)) {
        close_outputs(color_output, ir_output, depth_output);
        device->stop();
        device->close();
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    while (!stop_requested) {
        if (!listener.waitForNewFrame(frames, 10 * 1000)) {
            std::cerr << "Timeout waiting for frames" << std::endl;
            continue;
        }

        if (config.enable_color) {
            auto *frame = frames[libfreenect2::Frame::Color];
            rgb_to_yuyv(frame->data, color_output.buffer, config.color_width, config.color_height);
            color_output.write_frame();
        }

        if (config.enable_ir) {
            auto *frame = frames[libfreenect2::Frame::Ir];
            auto grayscale = normalize_ir_frame(frame);
            grayscale_to_yuyv(grayscale, ir_output.buffer);
            ir_output.write_frame();
        }

        if (config.enable_depth) {
            auto *frame = frames[libfreenect2::Frame::Depth];
            auto depth_bgrx = colorize_depth_frame(frame, config.depth_min_mm, config.depth_max_mm);
            rgb_to_yuyv(depth_bgrx.data(), depth_output.buffer, config.depth_width, config.depth_height);
            depth_output.write_frame();
        }

        listener.release(frames);
    }

    close_outputs(color_output, ir_output, depth_output);
    device->stop();
    device->close();
    return 0;
}
