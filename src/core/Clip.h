#pragma once
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <math.h>
#include <cmath>
#include <opencv2/videoio.hpp>
#include <string>
#include <vector>
#ifndef TEST_H
#define TEST_H

class Clip {
public:
    static constexpr double DEFAULT_BLUR_THRESHOLD = 5.0;
    struct BlurSample { int frame; double variance; bool blurry; };
    // Constructor
    Clip(std::string clip_name, int& clip_num, std::string path, double max_length = 8, double min_length = 4);

    // Video Processing Methods
    void WriteTo(cv::VideoWriter output);
    void FacialRecognition(int& start);
    void Display();
    void Create(double max_length = 8, double min_length = 4);
    bool FindNotBlurry(double video_length = 4);

    // Set Functions
    void setStart(float start);
    void setEnd(float end);

    // Get Functions
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    double getFPS() const { return fps; }
    double getLength() const { return video_length; }
    double getStart() const { return start_timestamp; }
    double getEnd() const { return end_timestamp; }
    const std::vector<BlurSample>& getBlurSamples() const { return blur_samples; }
    void setFaceCascade(const std::string& path) { face_cascade_path = path; }
    void setBlurThreshold(double threshold) { blur_threshold = threshold; }
    double getBlurThreshold() const { return blur_threshold; }

    int id;
    Clip* next = nullptr;
    Clip* prev = nullptr;

private:
    // Member Variables
    double video_length;
    double start_timestamp = 0;
    double end_timestamp = 0;
    std::string video_path;
    int width;
    int height;
    double fps;
    cv::VideoWriter slice;
    std::vector<BlurSample> blur_samples;
    std::string face_cascade_path;
    double blur_threshold = DEFAULT_BLUR_THRESHOLD;
};

#endif // HEADER_FILE_NAME_H
