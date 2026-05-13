#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

class LPRRecognizer {
public:
    static LPRRecognizer& instance();

    struct Result {
        std::string plate;
        double confidence = 0.0;
    };

    // Load model from path. Call once at startup.
    bool load(const std::string& model_path);

    // Recognize license plate from cropped plate image
    Result recognize(const cv::Mat& plate_bgr);

    // Check if model is loaded
    bool isLoaded() const { return loaded_; }

private:
    LPRRecognizer() = default;

    // Preprocess plate region: resize keeping aspect ratio, pad to 160x48,
    // normalize to [-1, 1]: (val - 127.5) / 127.5
    cv::Mat preprocess(const cv::Mat& plate_bgr);

    // CTC greedy decoding for (1, time_steps, num_classes) output
    std::string ctcDecode(const cv::Mat& output, double& confidence);

    cv::dnn::Net net_;
    std::mutex net_mutex_;
    bool loaded_ = false;

    // HyperLPR3 rpv3_mdict_160_r3 dimensions
    int input_width_ = 160;
    int input_height_ = 48;
    int num_classes_ = 78;
    int time_steps_ = 20;

    // Character set (78 tokens, index 0 = CTC blank)
    std::vector<std::string> charset_;
    int blank_idx_ = 0;
    void initCharset();
};
