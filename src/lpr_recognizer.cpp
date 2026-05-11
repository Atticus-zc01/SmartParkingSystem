#include "lpr_recognizer.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>

LPRRecognizer& LPRRecognizer::instance() {
    static LPRRecognizer inst;
    return inst;
}

bool LPRRecognizer::load(const std::string& model_path) {
    try {
        net_ = cv::dnn::readNetFromONNX(model_path);
        net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        loaded_ = true;
        probed_ = false;

        // Get output layer names
        output_names_ = net_.getUnconnectedOutLayersNames();

        // Probe model dimensions
        if (!probeModel()) {
            std::cerr << "[LPR] Warning: model probe failed, using defaults\n";
        }

        std::cerr << "[LPR] Model loaded: " << model_path << "\n";
        std::cerr << "[LPR] Input: " << input_width_ << "x" << input_height_
                  << " Classes: " << num_classes_ << " Time steps: " << time_steps_ << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[LPR] Failed to load model: " << e.what() << "\n";
        loaded_ = false;
        return false;
    }
}

bool LPRRecognizer::probeModel() {
    if (!loaded_) return false;

    try {
        // Create dummy input matching expected input size
        cv::Mat dummy(input_height_, input_width_, CV_8UC3, cv::Scalar(128, 128, 128));
        cv::Mat blob = cv::dnn::blobFromImage(dummy, 1.0 / 255.0,
            cv::Size(input_width_, input_height_), cv::Scalar(), false);

        net_.setInput(blob);

        std::vector<cv::Mat> outputs;
        if (output_names_.empty()) {
            // Single output model
            cv::Mat out = net_.forward();
            outputs.push_back(out);
        } else {
            net_.forward(outputs, output_names_);
        }

        if (outputs.empty()) {
            std::cerr << "[LPR] probeModel: no outputs\n";
            return false;
        }

        const cv::Mat& out = outputs[0];
        std::cerr << "[LPR] probeModel: output dims=" << out.dims;

        if (out.dims == 3) {
            // Standard format: [batch, num_classes, time_steps]
            num_classes_ = out.size[1];
            time_steps_ = out.size[2];
            std::cerr << " shape=" << out.size[0] << "x"
                      << num_classes_ << "x" << time_steps_ << "\n";
        } else if (out.dims == 2) {
            // Could be [batch, classes*time] or already decoded
            int total = out.size[1];
            // Try to guess: typical LPRNet has 18-24 time steps
            // Heuristic: num_classes should be 68 or 71
            if (total % 68 == 0) {
                num_classes_ = 68;
                time_steps_ = total / 68;
            } else if (total % 71 == 0) {
                num_classes_ = 71;
                time_steps_ = total / 71;
            } else {
                num_classes_ = total;
                time_steps_ = 1;
            }
            std::cerr << " shape=" << out.size[0] << "x" << total
                      << " (interpreted as " << num_classes_
                      << " classes x " << time_steps_ << " steps)\n";
        } else {
            std::cerr << " (unexpected, total=" << out.total() << ")\n";
            num_classes_ = 68;
            time_steps_ = 23;
        }

        initCharset(num_classes_);
        probed_ = true;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[LPR] probeModel error: " << e.what() << "\n";
        // Default fallback
        num_classes_ = 68;
        time_steps_ = 23;
        initCharset(num_classes_);
        return false;
    }
}

void LPRRecognizer::initCharset(int num_classes) {
    // 31 Chinese province abbreviations
    static const std::string provinces[] = {
        "京", "沪", "津", "渝", "冀", "晋", "蒙", "辽", "吉", "黑",
        "苏", "浙", "皖", "闽", "赣", "鲁", "豫", "鄂", "湘", "粤",
        "桂", "琼", "川", "贵", "云", "藏", "陕", "甘", "青", "宁", "新"
    };
    // Digits
    static const char digits[] = "0123456789";
    // Letters (excluding I, O as per Chinese plate standard)
    static const char letters[] = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    // Special plate types
    static const std::string special[] = {
        "挂", "学", "警", "港", "澳", "使", "领"
    };

    charset_.clear();
    // Index 0 is CTC blank
    charset_.push_back("");

    int expected_67 = 31 + 10 + 24 + 2;   // 67 chars (+ blank = 68)
    int expected_70 = 31 + 10 + 24 + 5;   // 70 chars (+ blank = 71)
    int expected_72 = 31 + 10 + 24 + 7;   // 72 chars (+ blank = 73)

    // Add provinces
    for (const auto& p : provinces)
        charset_.push_back(p);

    // Add digits
    for (int i = 0; digits[i]; i++)
        charset_.push_back(std::string(1, digits[i]));

    // Add letters
    for (int i = 0; letters[i]; i++)
        charset_.push_back(std::string(1, letters[i]));

    // Add special chars based on available space
    // num_classes includes blank at index 0
    int chars_needed = num_classes - 1;
    int chars_added = (int)charset_.size() - 1; // minus blank

    if (chars_needed >= 68) {
        // Need at least 挂 学
        for (int i = 0; i < 2 && chars_added < chars_needed; i++) {
            charset_.push_back(special[i]);
            chars_added++;
        }
    }
    if (chars_needed >= 70) {
        // Also 警 港 澳
        for (int i = 2; i < 5 && chars_added < chars_needed; i++) {
            charset_.push_back(special[i]);
            chars_added++;
        }
    }
    if (chars_needed >= 72) {
        // Also 使 领
        for (int i = 5; i < 7 && chars_added < chars_needed; i++) {
            charset_.push_back(special[i]);
            chars_added++;
        }
    }

    // Pad with empty strings if needed (shouldn't happen)
    while ((int)charset_.size() < num_classes)
        charset_.push_back("?");

    std::cerr << "[LPR] Charset initialized: " << charset_.size()
              << " entries (blank + " << (charset_.size() - 1) << " chars)\n";
}

cv::Mat LPRRecognizer::preprocess(const cv::Mat& plate_bgr) {
    cv::Mat gray;
    if (plate_bgr.channels() == 3)
        cv::cvtColor(plate_bgr, gray, cv::COLOR_BGR2GRAY);
    else
        gray = plate_bgr.clone();

    // Resize to model input dimensions
    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(input_width_, input_height_));

    // LPRNet expects 3-channel input (RGB), replicate grayscale
    cv::Mat three_channel;
    cv::cvtColor(resized, three_channel, cv::COLOR_GRAY2BGR);

    // Create blob: scale 1/255, no mean subtraction
    cv::Mat blob = cv::dnn::blobFromImage(three_channel, 1.0 / 255.0,
        cv::Size(input_width_, input_height_), cv::Scalar(), false);

    return blob;
}

std::string LPRRecognizer::ctcDecode(const cv::Mat& output, double& confidence) {
    confidence = 0.0;

    if (output.empty() || charset_.empty())
        return "";

    int num_classes = output.size[1];
    int time_steps = output.size[2];

    // Ensure charset matches model output
    if (num_classes != (int)charset_.size()) {
        std::cerr << "[LPR] charset size mismatch: model=" << num_classes
                  << " expected=" << charset_.size() << "\n";
        // Try to fix by reinitializing
        initCharset(num_classes);
        if (num_classes != (int)charset_.size())
            return "";
    }

    const float* data = output.ptr<float>();
    std::string result;
    int prev_idx = -1;
    double total_prob = 0.0;
    int valid_steps = 0;

    for (int t = 0; t < time_steps; t++) {
        // Find argmax at this time step
        int max_idx = 0;
        float max_val = data[t];

        for (int c = 1; c < num_classes; c++) {
            float val = data[c * time_steps + t];
            if (val > max_val) {
                max_val = val;
                max_idx = c;
            }
        }

        // Skip blank (index 0)
        if (max_idx == 0)
            continue;

        // Collapse consecutive repeats (CTC merge)
        if (max_idx == prev_idx)
            continue;

        prev_idx = max_idx;

        // Map to character
        if (max_idx < (int)charset_.size() && !charset_[max_idx].empty()) {
            result += charset_[max_idx];
            total_prob += max_val;
            valid_steps++;
        }
    }

    // Confidence is average of argmax probabilities for non-blank steps
    if (valid_steps > 0)
        confidence = total_prob / valid_steps;

    return result;
}

LPRRecognizer::Result LPRRecognizer::recognize(const cv::Mat& plate_bgr) {
    Result result;

    if (!loaded_) {
        std::cerr << "[LPR] Model not loaded\n";
        return result;
    }

    if (!probed_) {
        // Try probing again
        probeModel();
    }

    // Preprocess
    cv::Mat blob = preprocess(plate_bgr);

    // Inference
    net_.setInput(blob);
    cv::Mat output;
    try {
        if (output_names_.empty()) {
            output = net_.forward();
        } else {
            std::vector<cv::Mat> outputs;
            net_.forward(outputs, output_names_);
            if (outputs.empty()) return result;
            output = outputs[0];
        }
    } catch (const std::exception& e) {
        std::cerr << "[LPR] Forward error: " << e.what() << "\n";
        return result;
    }

    // Check output format
    if (output.dims == 3) {
        // Standard format [1, num_classes, time_steps]
        result.plate = ctcDecode(output, result.confidence);
    } else if (output.dims == 4) {
        // Some models output [1, 1, num_classes, time_steps]
        cv::Mat squeezed;
        int sizes[3] = { output.size[0], output.size[1], output.size[2] };
        // Try to handle: take first channel
        std::cerr << "[LPR] 4D output, attempting to handle\n";
        // Could not decode reliably, return empty
    } else {
        std::cerr << "[LPR] Unexpected output dims: " << output.dims << "\n";
    }

    return result;
}
