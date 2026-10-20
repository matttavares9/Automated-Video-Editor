#include "Clip.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void write_fixture(const fs::path& path, const std::vector<std::pair<int, bool>>& sections) {
    cv::VideoWriter writer(path.string(), cv::VideoWriter::fourcc('M','J','P','G'), 30, {320,240});
    require(writer.isOpened(), "Unable to write test fixture");
    cv::Mat sharp(240, 320, CV_8UC3), blurry(240, 320, CV_8UC3, cv::Scalar(120,120,120));
    cv::RNG rng(42);
    rng.fill(sharp, cv::RNG::UNIFORM, 0, 256);
    for (const auto& [frames, clear] : sections)
        for (int n = 0; n < frames; ++n) writer.write(clear ? sharp : blurry);
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Supply fixture output directory");
        const fs::path dir(argv[1]);
        fs::create_directories(dir);
        const auto path = dir / "blur-sequence.avi";
        int id = 0;

        // Initial clear footage is too brief; the first sustained main shot
        // wins, even when more sharp footage follows the closing blur.
        write_fixture(path, {{30,true},{30,false},{150,true},{30,false},{150,true}});
        Clip default_settings(path.string(), id, "");
        require(default_settings.getBlurThreshold() == 5, "Default blur threshold must tolerate more blur");
        Clip first(path.string(), id, "");
        first.Create();
        require(first.getStart() >= 2 && first.getStart() <= 2.1, "Kept short opening or skipped main-shot start");
        require(first.getEnd() == 7, "Main shot must stop at the closing blurry section");

        // EOF must preserve the start after blur, not reset to source time zero.
        write_fixture(path, {{60,false},{180,true}});
        Clip eof(path.string(), id, "");
        eof.Create();
        require(eof.getStart() >= 2 && eof.getEnd() == 8, "EOF lost clear-shot boundaries");

        write_fixture(path, {{60,false},{90,true},{30,false}});
        Clip too_short(path.string(), id, "");
        too_short.Create();
        require(too_short.getEnd() == 0, "A three-second shot must not pass the original four-second minimum");
        too_short.Create(8, 2);
        require(too_short.getStart() >= 2 && too_short.getEnd() == 5, "Explicit two-second minimum did not select the shorter shot");

        write_fixture(path, {{180,false}});
        Clip all_blurry(path.string(), id, "");
        all_blurry.Create();
        require(all_blurry.getEnd() == 0, "All-blurry footage must not become an arbitrary highlight");
        std::cout << "Original blur-boundary selection regression checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
