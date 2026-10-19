#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <nlohmann/json.hpp>
#include "Clip.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

json read_json(const fs::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Unable to open JSON file: " + path.string());
    json value;
    input >> value;
    return value;
}

void write_json(const fs::path& path, const json& value) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Unable to write JSON file: " + path.string());
    output << value.dump(2) << '\n';
}

json analyze(const json& job) {
    const auto input_path = job.at("input_path").get<std::string>();
    const double min_clip = job.value("min_clip_seconds", 4.0);
    const double max_total = job.value("max_total_duration_seconds", 8.0);
    const double blur_threshold = job.value("blur_threshold", Clip::DEFAULT_BLUR_THRESHOLD);
    if (!std::isfinite(min_clip) || !std::isfinite(max_total) || !std::isfinite(blur_threshold) ||
        min_clip <= 0 || max_total < min_clip || blur_threshold <= 0)
        throw std::runtime_error("Require positive blur_threshold and 0 < min_clip_seconds <= max_total_duration_seconds");

    // Matthew's original Clip class owns blur-boundary selection. Do not pass
    // the requested output limit to Create: the original desktop helper makes
    // a random centered trim when a shot exceeds that limit. Analysis must
    // return the complete clear shot so the API can apply a deterministic
    // output-duration cap after blur detection.
    int clip_id = 0;
    Clip clip(input_path, clip_id, "");
    clip.setFaceCascade(job.value("face_cascade_path", std::string()));
    clip.setBlurThreshold(blur_threshold);
    clip.Create(clip.getLength(), min_clip);

    json samples = json::array();
    for (const auto& sample : clip.getBlurSamples()) {
        samples.push_back({
            {"time_seconds", sample.frame / clip.getFPS()},
            {"frame_index", sample.frame},
            {"sharpness", sample.variance},
            {"keep", !sample.blurry}
        });
    }
    json cuts = json::array();
    if (clip.getEnd() > clip.getStart()) {
        cuts.push_back({
            {"start_seconds", clip.getStart()}, {"end_seconds", clip.getEnd()},
            {"score", 0.0},
            {"reason", "First continuous non-blurry main shot lasting the minimum duration, bounded by blurry sections"}
        });
    }
    return {
        {"status", "ok"}, {"input_path", input_path},
        {"analysis", {
            {"algorithm", "original_contiguous_clear_shot"},
            {"duration_seconds", clip.getLength()}, {"fps", clip.getFPS()},
            {"sample_count", samples.size()}, {"sharpness_threshold", blur_threshold},
            {"sample_interval_seconds", std::max(1, static_cast<int>(clip.getFPS()/2)) / clip.getFPS()},
            {"samples", samples},
            {"selection_status", cuts.empty() ? "no_qualifying_clear_shot" : "selected"}
        }},
        {"cuts", cuts}
    };
}

json render(const json& job) {
    const auto output_path = job.at("output_path").get<std::string>();
    const json sources = job.contains("sources")
        ? job.at("sources")
        : json::array({{{"input_path", job.at("input_path")}, {"cuts", job.at("cuts")}}});
    if (sources.empty()) throw std::runtime_error("Render job requires at least one source");

    fs::path output(output_path);
    if (!output.parent_path().empty()) fs::create_directories(output.parent_path());

    int written = 0;
    double output_fps = 0;
    cv::Size output_size;
    cv::VideoWriter writer;
    for (const auto& source : sources) {
        const auto input_path = source.at("input_path").get<std::string>();
        const auto cuts = source.at("cuts");
        if (cuts.empty()) continue;

        cv::VideoCapture input(input_path);
        if (!input.isOpened()) throw std::runtime_error("Unable to open input video: " + input_path);
        const double fps = input.get(cv::CAP_PROP_FPS);
        const cv::Size size(
            static_cast<int>(input.get(cv::CAP_PROP_FRAME_WIDTH)),
            static_cast<int>(input.get(cv::CAP_PROP_FRAME_HEIGHT)));
        if (fps <= 0 || size.width <= 0 || size.height <= 0)
            throw std::runtime_error("Invalid video dimensions or frame rate");
        if (!writer.isOpened()) {
            output_fps = fps;
            output_size = size;
            writer.open(output.string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'), output_fps, output_size);
            if (!writer.isOpened()) throw std::runtime_error("Unable to create export: " + output.string());
        }

        cv::Mat frame;
        for (const auto& cut : cuts) {
            const int begin = std::max(0, static_cast<int>(std::floor(cut.at("start_seconds").get<double>() * fps)));
            const int end = std::max(begin, static_cast<int>(std::ceil(cut.at("end_seconds").get<double>() * fps)));
            input.set(cv::CAP_PROP_POS_FRAMES, begin);
            for (int index = begin; index < end && input.read(frame); ++index) {
                if (size != output_size) cv::resize(frame, frame, output_size);
                writer.write(frame);
                ++written;
            }
        }
    }
    if (!writer.isOpened() || written == 0) throw std::runtime_error("Render job has no frames to write");
    return {{"status", "ok"}, {"output_path", output.string()}, {"frames_written", written}, {"fps", output_fps}};
}

void usage() {
    std::cerr << "Usage: video_engine <analyze|render> --job job.json --output result.json\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 6 || std::string(argv[2]) != "--job" || std::string(argv[4]) != "--output") {
            usage();
            return 2;
        }
        const std::string command = argv[1];
        const auto job = read_json(argv[3]);
        json result;
        if (command == "analyze") {
            result = analyze(job);
        } else if (command == "render") {
            result = render(job);
        } else {
            throw std::runtime_error("Unknown command: " + command);
        }
        write_json(argv[5], result);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "video_engine error: " << error.what() << '\n';
        return 1;
    }
}
