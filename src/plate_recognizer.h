#pragma once
#include <string>
#include <vector>
#include <opencv2/core.hpp>

class PlateRecognizer {
public:
    static PlateRecognizer& instance();

    struct RecognitionResult {
        std::string plate_number;
        double confidence = 0.0;
        std::string plate_color;
        std::string message;
    };

    RecognitionResult recognize(const std::string& image_base64);
    RecognitionResult recognize(const cv::Mat& frame);

private:
    PlateRecognizer() = default;

    // Image preprocessing
    cv::Mat preprocess(const cv::Mat& src);

    // Plate region detection
    std::vector<cv::Rect> detectPlateCandidates(const cv::Mat& src);

    // Plate color analysis
    std::string analyzePlateColor(const cv::Mat& plate_region);

    // Character recognition pipeline
    std::string recognizeCharacters(const cv::Mat& plate_img, double& confidence);
    std::vector<cv::Mat> segmentCharacters(const cv::Mat& plate_img);
    char matchCharacter(const cv::Mat& char_img, double& conf);

    // Template management
    void initTemplates();
    std::vector<cv::Mat> digit_templates_;
    std::vector<cv::Mat> letter_templates_;
    std::vector<std::vector<cv::Point>> digit_contours_;
    std::vector<std::vector<cv::Point>> letter_contours_;
    bool templates_initialized_ = false;

    // Base64 helper
    static std::vector<uchar> base64Decode(const std::string& data);
};
