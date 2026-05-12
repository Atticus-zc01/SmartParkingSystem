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

    // Per-character decoded info for post-processing
    struct DecodedChar {
        int char_idx;      // index into charset_
        int time_step;     // time step in model output (for score lookup)
        double prob;       // softmax probability at this step
    };

private:
    LPRRecognizer() = default;

    // Probe model to determine input/output dimensions
    bool probeModel();

    // Preprocess plate region: resize to 94x24, normalize
    cv::Mat preprocess(const cv::Mat& plate_bgr);

    // CTC greedy decoding
    std::string ctcDecode(const cv::Mat& output, double& confidence);

    // Post-process plate string with format constraints
    std::string applyPlateFormat(std::vector<DecodedChar>& decoded,
                                  const float* raw_data,
                                  double& confidence);

    cv::dnn::Net net_;
    std::mutex net_mutex_;   // serializes concurrent inference (DNN not thread-safe)
    bool loaded_ = false;
    bool probed_ = false;

    // Model dimensions (auto-detected)
    int input_width_ = 94;
    int input_height_ = 24;
    int num_classes_ = 0;       // including CTC blank
    int time_steps_ = 0;

    // Character set (populated based on num_classes)
    std::vector<std::string> charset_;
    int blank_idx_ = -1;       // index of CTC blank (last class)
    void initCharset(int num_classes);

    // Buffer for detecting output shape
    std::vector<std::string> output_names_;
};
