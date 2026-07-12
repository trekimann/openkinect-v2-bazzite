#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultSpeedOfSound = 343.0;
constexpr int kDefaultSampleRate = 16000;
constexpr int kDefaultInputChannels = 4;
constexpr double kDefaultMicSpacingMm = 75.0;
constexpr size_t kStateWriteIntervalMs = 100;

volatile sig_atomic_t g_stop_requested = 0;

void handle_signal(int) {
    g_stop_requested = 1;
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

double parse_double(const std::string &value, double default_value) {
    try {
        return std::stod(trim(value));
    } catch (...) {
        return default_value;
    }
}

struct Config {
    bool enable_audio = false;
    std::string audio_backend = "pipewire";
    int audio_frame_ms = 20;
    bool audio_agc_enable = true;
    bool audio_noise_reduction_enable = false;
    std::string audio_beamform_mode = "delay-and-sum";
    std::string audio_steering_mode = "auto";
    std::string audio_output_mode = "focused-mono";
    bool audio_expose_raw_source = true;
    bool audio_expose_stereo_source = false;
    bool audio_expose_focused_mono_source = true;
    std::string audio_input_match = "Xbox NUI Sensor";
    std::string audio_input_node_name;
    std::string audio_focused_node_name = "openkinect.focused_mono";
    std::string audio_focused_node_description = "Kinect Focused Mono";
    std::string audio_state_path;
    int audio_sample_rate = kDefaultSampleRate;
    int audio_input_channels = kDefaultInputChannels;
    double audio_mic_spacing_mm = kDefaultMicSpacingMm;
    double audio_speed_of_sound_mps = kDefaultSpeedOfSound;
    double audio_vad_threshold = 0.0008;
    double audio_confidence_threshold = 0.2;
    double audio_max_abs_azimuth_deg = 65.0;
    double audio_manual_azimuth_deg = 0.0;
    double audio_steering_smoothing = 0.18;
    double audio_agc_target_rms = 0.12;
    double audio_max_gain = 12.0;
};

std::string default_state_path() {
    const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        return std::string(runtime_dir) + "/openkinect-v2/audio-state.json";
    }
    return "/tmp/openkinect-v2/audio-state.json";
}

Config load_config(const std::string &path) {
    Config config;
    config.audio_state_path = default_state_path();

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

        if (key == "ENABLE_AUDIO") config.enable_audio = parse_bool(value, config.enable_audio);
        else if (key == "AUDIO_BACKEND") config.audio_backend = value;
        else if (key == "AUDIO_FRAME_MS") config.audio_frame_ms = parse_int(value, config.audio_frame_ms);
        else if (key == "AUDIO_AGC_ENABLE") config.audio_agc_enable = parse_bool(value, config.audio_agc_enable);
        else if (key == "AUDIO_NOISE_REDUCTION_ENABLE") config.audio_noise_reduction_enable = parse_bool(value, config.audio_noise_reduction_enable);
        else if (key == "AUDIO_BEAMFORM_MODE") config.audio_beamform_mode = value;
        else if (key == "AUDIO_STEERING_MODE") config.audio_steering_mode = value;
        else if (key == "AUDIO_OUTPUT_MODE") config.audio_output_mode = value;
        else if (key == "AUDIO_EXPOSE_RAW_SOURCE") config.audio_expose_raw_source = parse_bool(value, config.audio_expose_raw_source);
        else if (key == "AUDIO_EXPOSE_STEREO_SOURCE") config.audio_expose_stereo_source = parse_bool(value, config.audio_expose_stereo_source);
        else if (key == "AUDIO_EXPOSE_FOCUSED_MONO_SOURCE") config.audio_expose_focused_mono_source = parse_bool(value, config.audio_expose_focused_mono_source);
        else if (key == "AUDIO_INPUT_MATCH") config.audio_input_match = value;
        else if (key == "AUDIO_INPUT_NODE_NAME") config.audio_input_node_name = value;
        else if (key == "AUDIO_FOCUSED_MONO_NODE_NAME") config.audio_focused_node_name = value;
        else if (key == "AUDIO_FOCUSED_MONO_DESCRIPTION") config.audio_focused_node_description = value;
        else if (key == "AUDIO_STATE_PATH") config.audio_state_path = value;
        else if (key == "AUDIO_SAMPLE_RATE") config.audio_sample_rate = parse_int(value, config.audio_sample_rate);
        else if (key == "AUDIO_INPUT_CHANNELS") config.audio_input_channels = parse_int(value, config.audio_input_channels);
        else if (key == "AUDIO_MIC_SPACING_MM") config.audio_mic_spacing_mm = parse_double(value, config.audio_mic_spacing_mm);
        else if (key == "AUDIO_SPEED_OF_SOUND_MPS") config.audio_speed_of_sound_mps = parse_double(value, config.audio_speed_of_sound_mps);
        else if (key == "AUDIO_VAD_THRESHOLD") config.audio_vad_threshold = parse_double(value, config.audio_vad_threshold);
        else if (key == "AUDIO_CONFIDENCE_THRESHOLD") config.audio_confidence_threshold = parse_double(value, config.audio_confidence_threshold);
        else if (key == "AUDIO_MAX_ABS_AZIMUTH_DEG") config.audio_max_abs_azimuth_deg = parse_double(value, config.audio_max_abs_azimuth_deg);
        else if (key == "AUDIO_MANUAL_AZIMUTH_DEG") config.audio_manual_azimuth_deg = parse_double(value, config.audio_manual_azimuth_deg);
        else if (key == "AUDIO_STEERING_SMOOTHING") config.audio_steering_smoothing = parse_double(value, config.audio_steering_smoothing);
        else if (key == "AUDIO_AGC_TARGET_RMS") config.audio_agc_target_rms = parse_double(value, config.audio_agc_target_rms);
        else if (key == "AUDIO_MAX_GAIN") config.audio_max_gain = parse_double(value, config.audio_max_gain);
    }

    return config;
}

std::string extract_quoted_value(const std::string &line) {
    const auto first_quote = line.find('"');
    if (first_quote == std::string::npos) {
        return "";
    }
    const auto second_quote = line.find('"', first_quote + 1);
    if (second_quote == std::string::npos) {
        return "";
    }
    return line.substr(first_quote + 1, second_quote - first_quote - 1);
}

struct PipeWireNodeInfo {
    std::string node_name;
    std::string node_description;
    std::string media_class;
};

std::vector<PipeWireNodeInfo> list_pipewire_nodes() {
    std::vector<PipeWireNodeInfo> nodes;
    FILE *pipe = popen("pw-cli list-objects 2>/dev/null", "r");
    if (pipe == nullptr) {
        return nodes;
    }

    PipeWireNodeInfo current;
    bool have_block = false;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line = trim(buffer);
        if (line.rfind("id ", 0) == 0) {
            if (have_block && !current.node_name.empty()) {
                nodes.push_back(current);
            }
            current = PipeWireNodeInfo{};
            have_block = true;
            continue;
        }
        if (!have_block) {
            continue;
        }
        if (line.find("node.name") != std::string::npos) {
            current.node_name = extract_quoted_value(line);
        } else if (line.find("node.description") != std::string::npos) {
            current.node_description = extract_quoted_value(line);
        } else if (line.find("media.class") != std::string::npos) {
            current.media_class = extract_quoted_value(line);
        }
    }

    if (have_block && !current.node_name.empty()) {
        nodes.push_back(current);
    }

    pclose(pipe);
    return nodes;
}

std::optional<std::string> discover_input_node_name(const Config &config) {
    const auto nodes = list_pipewire_nodes();
    for (const auto &node : nodes) {
        if (node.media_class != "Audio/Source") {
            continue;
        }
        if (!config.audio_input_node_name.empty() && node.node_name == config.audio_input_node_name) {
            return node.node_name;
        }
    }

    for (const auto &node : nodes) {
        if (node.media_class != "Audio/Source") {
            continue;
        }
        const bool matches_description = node.node_description.find(config.audio_input_match) != std::string::npos;
        const bool matches_name = node.node_name.find(config.audio_input_match) != std::string::npos;
        if ((matches_description || matches_name) && node.node_name.find("alsa_input") != std::string::npos) {
            return node.node_name;
        }
    }

    return std::nullopt;
}

uint64_t monotonic_millis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

double compute_rms(const float *samples, size_t frame_count, int channels) {
    double sum = 0.0;
    const size_t total = frame_count * static_cast<size_t>(channels);
    for (size_t i = 0; i < total; ++i) {
        const double sample = samples[i];
        sum += sample * sample;
    }
    if (total == 0) {
        return 0.0;
    }
    return std::sqrt(sum / static_cast<double>(total));
}

struct DirectionEstimate {
    double azimuth_deg = 0.0;
    double confidence = 0.0;
};

DirectionEstimate estimate_direction_gcc_phat(const float *samples,
                                              size_t frame_count,
                                              int channels,
                                              int sample_rate,
                                              double mic_spacing_mm,
                                              double speed_of_sound_mps,
                                              double max_abs_azimuth_deg) {
    DirectionEstimate estimate;
    if (channels < 2 || frame_count < 8) {
        return estimate;
    }

    const int left_channel = 0;
    const int right_channel = channels - 1;
    const size_t fft_size = frame_count;
    std::vector<double> left(fft_size, 0.0);
    std::vector<double> right(fft_size, 0.0);

    double left_mean = 0.0;
    double right_mean = 0.0;
    for (size_t i = 0; i < fft_size; ++i) {
        left_mean += samples[i * static_cast<size_t>(channels) + static_cast<size_t>(left_channel)];
        right_mean += samples[i * static_cast<size_t>(channels) + static_cast<size_t>(right_channel)];
    }
    left_mean /= static_cast<double>(fft_size);
    right_mean /= static_cast<double>(fft_size);

    for (size_t i = 0; i < fft_size; ++i) {
        left[i] = samples[i * static_cast<size_t>(channels) + static_cast<size_t>(left_channel)] - left_mean;
        right[i] = samples[i * static_cast<size_t>(channels) + static_cast<size_t>(right_channel)] - right_mean;
    }

    std::vector<std::complex<double>> cross_spectrum(fft_size, std::complex<double>(0.0, 0.0));
    for (size_t k = 0; k < fft_size; ++k) {
        std::complex<double> left_bin(0.0, 0.0);
        std::complex<double> right_bin(0.0, 0.0);
        for (size_t n = 0; n < fft_size; ++n) {
            const double angle = -2.0 * kPi * static_cast<double>(k * n) / static_cast<double>(fft_size);
            const std::complex<double> basis(std::cos(angle), std::sin(angle));
            left_bin += left[n] * basis;
            right_bin += right[n] * basis;
        }
        const std::complex<double> value = left_bin * std::conj(right_bin);
        const double magnitude = std::max(std::abs(value), 1e-9);
        cross_spectrum[k] = value / magnitude;
    }

    const double aperture_m = std::max(1e-6, (static_cast<double>(channels) - 1.0) * mic_spacing_mm / 1000.0);
    const int max_lag = std::max(1, static_cast<int>(std::ceil(
        std::sin(max_abs_azimuth_deg * kPi / 180.0) * aperture_m * static_cast<double>(sample_rate) / speed_of_sound_mps)));

    double best_value = -1.0;
    double mean_value = 0.0;
    int best_lag = 0;
    int samples_seen = 0;

    for (int lag = -max_lag; lag <= max_lag; ++lag) {
        const size_t output_index = lag >= 0
            ? static_cast<size_t>(lag)
            : fft_size - static_cast<size_t>(-lag);

        std::complex<double> correlation(0.0, 0.0);
        for (size_t k = 0; k < fft_size; ++k) {
            const double angle = 2.0 * kPi * static_cast<double>(k * output_index) / static_cast<double>(fft_size);
            correlation += cross_spectrum[k] * std::complex<double>(std::cos(angle), std::sin(angle));
        }

        const double value = std::abs(correlation) / static_cast<double>(fft_size);
        mean_value += value;
        ++samples_seen;
        if (value > best_value) {
            best_value = value;
            best_lag = lag;
        }
    }

    if (samples_seen == 0 || best_value <= 0.0) {
        return estimate;
    }

    mean_value /= static_cast<double>(samples_seen);
    const double time_delay_s = static_cast<double>(best_lag) / static_cast<double>(sample_rate);
    const double normalized = std::clamp(time_delay_s * speed_of_sound_mps / aperture_m, -1.0, 1.0);
    estimate.azimuth_deg = std::asin(normalized) * 180.0 / kPi;
    const double ratio = best_value / std::max(mean_value, 1e-6);
    estimate.confidence = std::clamp((ratio - 1.0) / 4.0, 0.0, 1.0);
    return estimate;
}

float interpolate_channel_sample(const float *samples,
                                 size_t frame_count,
                                 int channels,
                                 int channel_index,
                                 double sample_index) {
    if (frame_count == 0) {
        return 0.0f;
    }
    if (sample_index <= 0.0) {
        return samples[channel_index];
    }
    if (sample_index >= static_cast<double>(frame_count - 1)) {
        return samples[(frame_count - 1) * static_cast<size_t>(channels) + static_cast<size_t>(channel_index)];
    }

    const size_t base_index = static_cast<size_t>(sample_index);
    const size_t next_index = std::min(base_index + 1, frame_count - 1);
    const double fraction = sample_index - static_cast<double>(base_index);
    const float first = samples[base_index * static_cast<size_t>(channels) + static_cast<size_t>(channel_index)];
    const float second = samples[next_index * static_cast<size_t>(channels) + static_cast<size_t>(channel_index)];
    return static_cast<float>(first + (second - first) * fraction);
}

std::vector<float> beamform_to_mono(const float *samples,
                                    size_t frame_count,
                                    int channels,
                                    int sample_rate,
                                    double mic_spacing_mm,
                                    double speed_of_sound_mps,
                                    double azimuth_deg) {
    std::vector<float> mono(frame_count, 0.0f);
    if (channels <= 0 || frame_count == 0) {
        return mono;
    }

    const double angle = std::clamp(azimuth_deg, -90.0, 90.0) * kPi / 180.0;
    const double spacing_m = mic_spacing_mm / 1000.0;
    const double center = (static_cast<double>(channels) - 1.0) / 2.0;

    for (size_t sample = 0; sample < frame_count; ++sample) {
        double mixed = 0.0;
        for (int channel = 0; channel < channels; ++channel) {
            const double position = (static_cast<double>(channel) - center) * spacing_m;
            const double delay_samples = -(position * std::sin(angle) * static_cast<double>(sample_rate)) / speed_of_sound_mps;
            mixed += interpolate_channel_sample(samples, frame_count, channels, channel, static_cast<double>(sample) + delay_samples);
        }
        mono[sample] = static_cast<float>(mixed / static_cast<double>(channels));
    }

    return mono;
}

void apply_agc(std::vector<float> &samples, double target_rms, double max_gain) {
    if (samples.empty()) {
        return;
    }

    double energy = 0.0;
    for (float sample : samples) {
        energy += static_cast<double>(sample) * static_cast<double>(sample);
    }

    const double rms = std::sqrt(energy / static_cast<double>(samples.size()));
    if (rms <= 1e-6) {
        return;
    }

    const double gain = std::clamp(target_rms / rms, 1.0, max_gain);
    for (float &sample : samples) {
        sample = static_cast<float>(std::clamp(static_cast<double>(sample) * gain, -0.98, 0.98));
    }
}

std::string json_escape(const std::string &value) {
    std::ostringstream escaped;
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default: escaped << ch; break;
        }
    }
    return escaped.str();
}

class AudioDaemon {
public:
    explicit AudioDaemon(Config config)
        : config_(std::move(config)),
          effective_output_mode_(config_.audio_output_mode == "raw" ? "raw" : "focused-mono"),
          smoothed_azimuth_deg_(config_.audio_manual_azimuth_deg) {
        capture_events_.version = PW_VERSION_STREAM_EVENTS;
        capture_events_.process = &AudioDaemon::on_capture_process;
        output_events_.version = PW_VERSION_STREAM_EVENTS;
        output_events_.process = &AudioDaemon::on_output_process;
    }

    ~AudioDaemon() {
        cleanup();
    }

    bool initialize(int *argc, char ***argv) {
        if (config_.audio_backend != "pipewire") {
            std::cerr << "Unsupported AUDIO_BACKEND=" << config_.audio_backend << std::endl;
            return false;
        }

        if (config_.audio_input_channels < 2) {
            std::cerr << "AUDIO_INPUT_CHANNELS must be at least 2" << std::endl;
            return false;
        }

        if (effective_output_mode_ != config_.audio_output_mode) {
            std::cerr << "AUDIO_OUTPUT_MODE=" << config_.audio_output_mode
                      << " is not implemented yet; using focused-mono" << std::endl;
        }
        if (config_.audio_beamform_mode != "delay-and-sum") {
            std::cerr << "AUDIO_BEAMFORM_MODE=" << config_.audio_beamform_mode
                      << " is not implemented yet; using delay-and-sum" << std::endl;
        }
        if (config_.audio_noise_reduction_enable) {
            std::cerr << "AUDIO_NOISE_REDUCTION_ENABLE is reserved but not implemented in this milestone" << std::endl;
        }

        const auto discovered = discover_input_node_name(config_);
        if (!discovered) {
            std::cerr << "Unable to find a PipeWire Audio/Source that matches " << config_.audio_input_match << std::endl;
            return false;
        }
        input_node_name_ = *discovered;

        std::filesystem::path state_parent(config_.audio_state_path);
        state_parent = state_parent.parent_path();
        if (!state_parent.empty()) {
            std::error_code mkdir_error;
            std::filesystem::create_directories(state_parent, mkdir_error);
        }

        pw_init(argc, argv);
        loop_ = pw_main_loop_new(nullptr);
        if (loop_ == nullptr) {
            std::cerr << "Failed to create PipeWire main loop" << std::endl;
            return false;
        }

        if (!connect_capture_stream()) {
            return false;
        }
        if (should_publish_focused_source() && !connect_output_stream()) {
            return false;
        }

        write_state(true);
        return true;
    }

    void run() {
        if (loop_ != nullptr) {
            pw_main_loop_run(loop_);
        }
    }

    void stop() {
        if (loop_ != nullptr) {
            pw_main_loop_quit(loop_);
        }
    }

private:
    static void on_capture_process(void *userdata) {
        auto *self = static_cast<AudioDaemon *>(userdata);
        self->process_capture();
    }

    static void on_output_process(void *userdata) {
        auto *self = static_cast<AudioDaemon *>(userdata);
        self->process_output();
    }

    bool connect_capture_stream() {
        capture_stream_ = pw_stream_new_simple(
            pw_main_loop_get_loop(loop_),
            "openkinect-audio-capture",
            pw_properties_new(
                PW_KEY_MEDIA_TYPE, "Audio",
                PW_KEY_MEDIA_CATEGORY, "Capture",
                PW_KEY_MEDIA_ROLE, "Communication",
                PW_KEY_NODE_NAME, "openkinect.audio.capture",
                PW_KEY_TARGET_OBJECT, input_node_name_.c_str(),
                nullptr),
            &capture_events_,
            this);
        if (capture_stream_ == nullptr) {
            std::cerr << "Failed to create capture stream" << std::endl;
            return false;
        }

        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = static_cast<uint32_t>(config_.audio_sample_rate);
        info.channels = static_cast<uint32_t>(config_.audio_input_channels);
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
        info.position[2] = SPA_AUDIO_CHANNEL_FC;
        info.position[3] = SPA_AUDIO_CHANNEL_LFE;

        uint8_t buffer[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const spa_pod *params[1];
        params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

        const int result = pw_stream_connect(
            capture_stream_,
            PW_DIRECTION_INPUT,
            SPA_ID_INVALID,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                         PW_STREAM_FLAG_MAP_BUFFERS |
                                         PW_STREAM_FLAG_RT_PROCESS),
            params,
            1);
        if (result < 0) {
            std::cerr << "Failed to connect capture stream: " << spa_strerror(result) << std::endl;
            return false;
        }
        return true;
    }

    bool connect_output_stream() {
        output_stream_ = pw_stream_new_simple(
            pw_main_loop_get_loop(loop_),
            "openkinect-audio-output",
            pw_properties_new(
                PW_KEY_MEDIA_TYPE, "Audio",
                PW_KEY_MEDIA_CATEGORY, "Source",
                PW_KEY_MEDIA_ROLE, "Communication",
                PW_KEY_NODE_NAME, config_.audio_focused_node_name.c_str(),
                PW_KEY_NODE_DESCRIPTION, config_.audio_focused_node_description.c_str(),
                "media.class", "Audio/Source",
                nullptr),
            &output_events_,
            this);
        if (output_stream_ == nullptr) {
            std::cerr << "Failed to create output stream" << std::endl;
            return false;
        }

        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = static_cast<uint32_t>(config_.audio_sample_rate);
        info.channels = 1;
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;

        uint8_t buffer[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const spa_pod *params[1];
        params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

        const int result = pw_stream_connect(
            output_stream_,
            PW_DIRECTION_OUTPUT,
            SPA_ID_INVALID,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_DRIVER |
                                         PW_STREAM_FLAG_MAP_BUFFERS |
                                         PW_STREAM_FLAG_RT_PROCESS),
            params,
            1);
        if (result < 0) {
            std::cerr << "Failed to connect focused-mono output stream: " << spa_strerror(result) << std::endl;
            return false;
        }
        return true;
    }

    bool should_publish_focused_source() const {
        return effective_output_mode_ == "focused-mono" && config_.audio_expose_focused_mono_source;
    }

    void process_capture() {
        if (g_stop_requested != 0) {
            stop();
            return;
        }
        if (capture_stream_ == nullptr) {
            return;
        }

        pw_buffer *buffer = pw_stream_dequeue_buffer(capture_stream_);
        if (buffer == nullptr || buffer->buffer == nullptr || buffer->buffer->n_datas == 0) {
            return;
        }

        spa_buffer *spa_buffer = buffer->buffer;
        spa_data &data = spa_buffer->datas[0];
        if (data.data == nullptr || data.chunk == nullptr) {
            pw_stream_queue_buffer(capture_stream_, buffer);
            return;
        }

        const size_t offset = static_cast<size_t>(data.chunk->offset);
        const size_t bytes = data.chunk->size > 0 ? static_cast<size_t>(data.chunk->size) : static_cast<size_t>(data.maxsize);
        if (bytes < sizeof(float) * static_cast<size_t>(config_.audio_input_channels)) {
            pw_stream_queue_buffer(capture_stream_, buffer);
            return;
        }

        const auto *samples = reinterpret_cast<const float *>(static_cast<uint8_t *>(data.data) + offset);
        const size_t frame_count = bytes / (sizeof(float) * static_cast<size_t>(config_.audio_input_channels));
        const double rms = compute_rms(samples, frame_count, config_.audio_input_channels);
        const bool voice_active = rms >= config_.audio_vad_threshold;

        DirectionEstimate estimate{};
        if (config_.audio_steering_mode == "manual") {
            estimate.azimuth_deg = config_.audio_manual_azimuth_deg;
            estimate.confidence = 1.0;
        } else if (voice_active) {
            estimate = estimate_direction_gcc_phat(samples,
                                                  frame_count,
                                                  config_.audio_input_channels,
                                                  config_.audio_sample_rate,
                                                  config_.audio_mic_spacing_mm,
                                                  config_.audio_speed_of_sound_mps,
                                                  config_.audio_max_abs_azimuth_deg);
        }

        if (config_.audio_steering_mode == "manual") {
            smoothed_azimuth_deg_ = config_.audio_manual_azimuth_deg;
        } else if (voice_active && estimate.confidence >= config_.audio_confidence_threshold) {
            smoothed_azimuth_deg_ = (1.0 - config_.audio_steering_smoothing) * smoothed_azimuth_deg_ +
                                    config_.audio_steering_smoothing * estimate.azimuth_deg;
        }

        last_voice_active_ = voice_active;
        last_confidence_ = estimate.confidence;
        last_rms_ = rms;
        last_reported_azimuth_deg_ = smoothed_azimuth_deg_;
        last_update_ms_ = monotonic_millis();

        if (should_publish_focused_source()) {
            auto mono = beamform_to_mono(samples,
                                         frame_count,
                                         config_.audio_input_channels,
                                         config_.audio_sample_rate,
                                         config_.audio_mic_spacing_mm,
                                         config_.audio_speed_of_sound_mps,
                                         smoothed_azimuth_deg_);
            if (config_.audio_agc_enable) {
                apply_agc(mono, config_.audio_agc_target_rms, config_.audio_max_gain);
            }

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                const size_t max_buffered_samples = static_cast<size_t>(config_.audio_sample_rate) * 2U;
                mono_queue_.insert(mono_queue_.end(), mono.begin(), mono.end());
                while (mono_queue_.size() > max_buffered_samples) {
                    mono_queue_.pop_front();
                }
            }
        }

        write_state(false);
        pw_stream_queue_buffer(capture_stream_, buffer);
    }

    void process_output() {
        if (g_stop_requested != 0) {
            stop();
            return;
        }
        if (output_stream_ == nullptr) {
            return;
        }

        pw_buffer *buffer = pw_stream_dequeue_buffer(output_stream_);
        if (buffer == nullptr || buffer->buffer == nullptr || buffer->buffer->n_datas == 0) {
            return;
        }

        spa_buffer *spa_buffer = buffer->buffer;
        spa_data &data = spa_buffer->datas[0];
        if (data.data == nullptr || data.chunk == nullptr) {
            pw_stream_queue_buffer(output_stream_, buffer);
            return;
        }

        const size_t frame_capacity = static_cast<size_t>(data.maxsize) / sizeof(float);
        auto *output = reinterpret_cast<float *>(data.data);
        std::fill(output, output + frame_capacity, 0.0f);

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            const size_t samples_to_copy = std::min(frame_capacity, mono_queue_.size());
            for (size_t i = 0; i < samples_to_copy; ++i) {
                output[i] = mono_queue_.front();
                mono_queue_.pop_front();
            }
        }

        data.chunk->offset = 0;
        data.chunk->stride = sizeof(float);
        data.chunk->size = static_cast<uint32_t>(frame_capacity * sizeof(float));
        pw_stream_queue_buffer(output_stream_, buffer);
    }

    void write_state(bool force) {
        const uint64_t now = monotonic_millis();
        if (!force && now - last_state_write_ms_ < kStateWriteIntervalMs) {
            return;
        }
        last_state_write_ms_ = now;

        const std::filesystem::path state_path(config_.audio_state_path);
        const std::filesystem::path tmp_path = state_path.string() + ".tmp";
        std::ofstream output(tmp_path);
        if (!output.is_open()) {
            return;
        }

        output << "{\n"
               << "  \"backend\": \"" << json_escape(config_.audio_backend) << "\",\n"
               << "  \"input_node\": \"" << json_escape(input_node_name_) << "\",\n"
               << "  \"raw_source_available\": " << (config_.audio_expose_raw_source ? "true" : "false") << ",\n"
               << "  \"focused_source_enabled\": " << (should_publish_focused_source() ? "true" : "false") << ",\n"
               << "  \"requested_output_mode\": \"" << json_escape(config_.audio_output_mode) << "\",\n"
               << "  \"effective_output_mode\": \"" << json_escape(effective_output_mode_) << "\",\n"
               << "  \"focused_node_name\": \"" << json_escape(config_.audio_focused_node_name) << "\",\n"
               << "  \"steering_mode\": \"" << json_escape(config_.audio_steering_mode) << "\",\n"
               << "  \"azimuth_deg\": " << std::fixed << std::setprecision(2) << last_reported_azimuth_deg_ << ",\n"
               << "  \"confidence\": " << std::fixed << std::setprecision(3) << last_confidence_ << ",\n"
               << "  \"voice_active\": " << (last_voice_active_ ? "true" : "false") << ",\n"
               << "  \"input_rms\": " << std::fixed << std::setprecision(6) << last_rms_ << ",\n"
               << "  \"updated_monotonic_ms\": " << last_update_ms_ << "\n"
               << "}\n";
        output.close();

        std::error_code rename_error;
        std::filesystem::rename(tmp_path, state_path, rename_error);
        if (rename_error) {
            std::filesystem::copy_file(tmp_path, state_path, std::filesystem::copy_options::overwrite_existing, rename_error);
            std::filesystem::remove(tmp_path, rename_error);
        }
    }

    void cleanup() {
        if (output_stream_ != nullptr) {
            pw_stream_destroy(output_stream_);
            output_stream_ = nullptr;
        }
        if (capture_stream_ != nullptr) {
            pw_stream_destroy(capture_stream_);
            capture_stream_ = nullptr;
        }
        if (loop_ != nullptr) {
            pw_main_loop_destroy(loop_);
            loop_ = nullptr;
        }
        pw_deinit();
    }

    Config config_;
    std::string effective_output_mode_;
    std::string input_node_name_;
    pw_main_loop *loop_ = nullptr;
    pw_stream *capture_stream_ = nullptr;
    pw_stream *output_stream_ = nullptr;
    pw_stream_events capture_events_{};
    pw_stream_events output_events_{};
    std::mutex queue_mutex_;
    std::deque<float> mono_queue_;
    double smoothed_azimuth_deg_ = 0.0;
    double last_reported_azimuth_deg_ = 0.0;
    double last_confidence_ = 0.0;
    double last_rms_ = 0.0;
    bool last_voice_active_ = false;
    uint64_t last_update_ms_ = 0;
    uint64_t last_state_write_ms_ = 0;
};

}  // namespace

int main(int argc, char *argv[]) {
    std::string config_path = "/etc/openkinect-v2/openkinect-v2.conf";
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    Config config = load_config(config_path);
    if (!config.enable_audio) {
        std::cout << "Audio is disabled in " << config_path << std::endl;
        return 0;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    AudioDaemon daemon(std::move(config));
    if (!daemon.initialize(&argc, &argv)) {
        return 1;
    }

    if (g_stop_requested != 0) {
        daemon.stop();
    }

    daemon.run();
    return 0;
}
