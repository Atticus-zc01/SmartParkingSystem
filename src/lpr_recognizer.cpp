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

        initCharset();

        std::cerr << "[LPR] HyperLPR3 model loaded: " << model_path << "\n";
        std::cerr << "[LPR] Input: " << input_width_ << "x" << input_height_
                  << " Classes: " << num_classes_ << " Time steps: " << time_steps_ << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[LPR] Failed to load model: " << e.what() << "\n";
        loaded_ = false;
        return false;
    }
}

void LPRRecognizer::initCharset() {
    // HyperLPR3 rpv3_mdict character set (77 tokens + 1 padding = 78, index 0 = CTC blank)
    // Token list from hyperlpr3.common.tokenize
    charset_ = {
        "blank", "'",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "A", "B", "C", "D", "E", "F", "G", "H", "J",
        "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
        "U", "V", "W", "X", "Y", "Z",
        "云", "京", "冀", "吉", "学", "宁",
        "川", "挂", "新", "晋", "桂", "民", "沪", "津", "浙", "渝",
        "港", "湘", "琼", "甘", "皖", "粤", "航", "苏", "蒙", "藏",
        "警", "豫",
        "贵", "赣", "辽", "鄂", "闽", "陕", "青", "鲁", "黑",
        "领", "使", "澳",
        " "  // padding to match model output (77 tokens + 1 pad = 78 classes)
    };

    blank_idx_ = 0;

    std::cerr << "[LPR] Charset initialized: " << charset_.size()
              << " entries, blank_idx=" << blank_idx_ << "\n";
}

cv::Mat LPRRecognizer::preprocess(const cv::Mat& plate_bgr) {
    // HyperLPR3 preprocessing:
    // 1. CLAHE contrast enhancement
    // 2. Resize keeping aspect ratio, height=48, width proportional (max 160)
    // 3. Pad to 160x48 with zeros on the right
    // 4. Normalize: (val - 127.5) / 127.5
    // Note: model was trained on BGR images (no color conversion to RGB)

    cv::Mat enhanced = plate_bgr.clone();

    // Apply CLAHE to improve contrast for low-light/washed-out crops
    if (plate_bgr.channels() == 3) {
        cv::Mat lab;
        cv::cvtColor(plate_bgr, lab, cv::COLOR_BGR2Lab);
        std::vector<cv::Mat> lab_channels;
        cv::split(lab, lab_channels);
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
        clahe->apply(lab_channels[0], lab_channels[0]);
        cv::merge(lab_channels, lab);
        cv::cvtColor(lab, enhanced, cv::COLOR_Lab2BGR);
    }

    int h = enhanced.rows;
    int w = enhanced.cols;

    // Compute new width preserving aspect ratio
    double h_target = static_cast<double>(input_height_);
    int new_w = static_cast<int>(std::round(h_target * w / h));
    new_w = std::min(new_w, input_width_);
    new_w = std::max(new_w, 1);

    // Resize keeping aspect ratio (height fixed to 48)
    cv::Mat resized;
    cv::resize(enhanced, resized, cv::Size(new_w, input_height_));

    // Pad to uniform width: create 160x48 zeros, copy resized to left
    cv::Mat padded(input_height_, input_width_, CV_8UC3, cv::Scalar(0, 0, 0));
    resized.copyTo(padded(cv::Rect(0, 0, new_w, input_height_)));

    // blobFromImage: (image - mean) * scale
    // With scale=1/127.5, mean=127.5: blob = (image - 127.5) / 127.5
    cv::Mat blob = cv::dnn::blobFromImage(padded, 1.0 / 127.5,
        cv::Size(input_width_, input_height_), cv::Scalar(127.5, 127.5, 127.5), false);

    return blob;
}

std::string LPRRecognizer::ctcDecode(const cv::Mat& output, double& confidence) {
    confidence = 0.0;

    if (output.empty() || charset_.empty())
        return "";

    // Expected shape: [1, time_steps, num_classes] or [time_steps, num_classes]
    int T, C;
    const float* data = output.ptr<float>();

    if (output.dims == 3) {
        T = output.size[1];  // time_steps (20)
        C = output.size[2];  // num_classes (78)
    } else if (output.dims == 2) {
        T = output.size[0];
        C = output.size[1];
    } else {
        std::cerr << "[LPR] Unexpected output dims: " << output.dims << "\n";
        return "";
    }

    if (C != (int)charset_.size()) {
        std::cerr << "[LPR] Class count mismatch: model=" << C
                  << " expected=" << charset_.size() << "\n";
        return "";
    }

    // CTC greedy decoding — collect character indices for validation
    std::vector<int> kept_indices;
    double total_max = 0.0;
    int prev_idx = -1;

    for (int t = 0; t < T; t++) {
        // Argmax at this time step
        int max_idx = 0;
        float max_val = data[t * C];

        for (int c = 1; c < C; c++) {
            float val = data[t * C + c];
            if (val > max_val) {
                max_val = val;
                max_idx = c;
            }
        }

        // Skip CTC blank (index 0), reset consecutive tracking
        // (blank between identical chars separates them in CTC)
        if (max_idx == blank_idx_) {
            prev_idx = -1;
            continue;
        }

        // Standard CTC: collapse consecutive identical non-blank chars
        // This handles model stutter (e.g., 川,川 → 川) while blanks
        // between identical chars preserve valid repeats (7,_,7 → 77)
        if (max_idx == prev_idx)
            continue;

        kept_indices.push_back(max_idx);
        prev_idx = max_idx;
        total_max += max_val;
    }

    if (kept_indices.empty())
        return "";

    confidence = total_max / kept_indices.size();

    // --- Chinese plate format validation ---
    // Position 0: must be province character (charset indices 37-76)
    // Positions 1-N: must be digit (2-11) or letter (12-36)
    // Total: 7 or 8 characters (standard plate or new energy plate)

    int n = (int)kept_indices.size();
    if (n < 7 || n > 8) {
        std::cerr << "[LPR] Rejected (len=" << n << "): need 7 or 8\n";
        confidence = 0.0;
        return "";
    }

    // Check position 0: province character (Chinese character)
    int first = kept_indices[0];
    if (first < 37 || first > 76) {
        std::cerr << "[LPR] Rejected: pos0 not province (idx=" << first << ")\n";
        confidence = 0.0;
        return "";
    }

    // Check positions 1+: digit or letter only (no Chinese chars, no apostrophe)
    for (size_t i = 1; i < kept_indices.size(); i++) {
        int idx = kept_indices[i];
        bool is_digit = (idx >= 2 && idx <= 11);
        bool is_letter = (idx >= 12 && idx <= 36);
        if (!is_digit && !is_letter) {
            std::cerr << "[LPR] Rejected: pos" << i << " invalid (idx=" << idx << ")\n";
            confidence = 0.0;
            return "";
        }
    }

    // Build result string from validated indices
    std::string result;
    for (int idx : kept_indices)
        result += charset_[idx];

    std::cerr << "[LPR] Decoded: '" << result << "' (" << kept_indices.size()
              << " chars, conf=" << confidence << ")\n";
    return result;
}

LPRRecognizer::Result LPRRecognizer::recognize(const cv::Mat& plate_bgr) {
    Result result;

    if (!loaded_) {
        std::cerr << "[LPR] Model not loaded\n";
        return result;
    }

    cv::Mat blob = preprocess(plate_bgr);

    cv::Mat output;
    try {
        std::lock_guard<std::mutex> lock(net_mutex_);
        net_.setInput(blob);
        output = net_.forward();
    } catch (const std::exception& e) {
        std::cerr << "[LPR] Forward error: " << e.what() << "\n";
        return result;
    }

    if (output.empty()) {
        std::cerr << "[LPR] Empty output\n";
        return result;
    }

    result.plate = ctcDecode(output, result.confidence);
    return result;
}
