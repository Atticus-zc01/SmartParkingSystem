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
    // LPRNet_Pytorch character set (also used by RKNN model zoo).
    // NO blank at index 0 — the last class (dash '-' at 67) is the CTC blank.
    // Order: 31 provinces, 10 digits, 24 letters + I + O + dash.
    static const std::string LPR_CHARS[] = {
        "京", "沪", "津", "渝", "冀", "晋", "蒙", "辽", "吉", "黑",
        "苏", "浙", "皖", "闽", "赣", "鲁", "豫", "鄂", "湘", "粤",
        "桂", "琼", "川", "贵", "云", "藏", "陕", "甘", "青", "宁", "新",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "A", "B", "C", "D", "E", "F", "G", "H", "J", "K",
        "L", "M", "N", "P", "Q", "R", "S", "T", "U", "V",
        "W", "X", "Y", "Z", "I", "O", "-"
    };
    static const int LPR_NUM = sizeof(LPR_CHARS) / sizeof(LPR_CHARS[0]);

    charset_.clear();
    for (int i = 0; i < LPR_NUM; i++) {
        charset_.push_back(LPR_CHARS[i]);
    }

    // Pad with spaces if the model has more classes (e.g., 71 or 73)
    while ((int)charset_.size() < num_classes)
        charset_.push_back(" ");

    // CTC blank is always the last class in LPRNet models
    blank_idx_ = num_classes - 1;

    std::cerr << "[LPR] Charset initialized: " << charset_.size()
              << " entries, blank_idx=" << blank_idx_ << "\n";
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

    // Create blob: normalize to [-1, 1] as expected by LPRNet: (img - 127.5) / 127.5
    cv::Mat blob = cv::dnn::blobFromImage(three_channel, 1.0 / 127.5,
        cv::Size(input_width_, input_height_), cv::Scalar(127.5, 127.5, 127.5), false);

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
        initCharset(num_classes);
        if (num_classes != (int)charset_.size())
            return "";
    }

    const float* data = output.ptr<float>();
    std::vector<DecodedChar> decoded;
    int prev_idx = -1;

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

        // Compute softmax probability for the predicted class
        double exp_sum = 0.0;
        for (int c = 0; c < num_classes; c++) {
            exp_sum += std::exp(data[c * time_steps + t] - max_val);
        }
        double softmax_prob = std::exp(data[max_idx * time_steps + t] - max_val) / exp_sum;

        // CORRECTED ORDER: CTC greedy decoding
        // Step 1: Collapse consecutive repeats FIRST (regardless of blank)
        if (max_idx == prev_idx)
            continue;
        prev_idx = max_idx;

        // Step 2: THEN skip blanks
        // This allows blanks between identical characters to properly separate them
        if (max_idx == blank_idx_)
            continue;

        decoded.push_back({max_idx, t, softmax_prob});
    }

    if (decoded.empty())
        return "";

    // Apply plate format post-processing
    return applyPlateFormat(decoded, data, confidence);
}

/// Validate decoded chars as a 7-char Chinese plate.
/// Returns the plate string if valid, empty string otherwise.
/// Modifies decoded in-place (position 1 digit→letter correction).
static bool validate7CharPlate(
    std::vector<LPRRecognizer::DecodedChar>& decoded,
    const float* raw_data,
    int time_steps,
    const std::vector<std::string>& charset,
    double& out_confidence)
{
    // Position 0: province (index 0-30)
    if (decoded[0].char_idx < 0 || decoded[0].char_idx > 30) return false;
    if (decoded[0].prob < 0.15) return false;

    // Position 1: must be letter (41-66), with digit→letter correction
    {
        int idx1 = decoded[1].char_idx;
        if (idx1 >= 31 && idx1 <= 40) {
            int ts = decoded[1].time_step;
            int best_letter = -1;
            float best_val = -1e10f;
            for (int c = 41; c <= 66; c++) {
                float val = raw_data[c * time_steps + ts];
                if (val > best_val) { best_val = val; best_letter = c; }
            }
            if (best_letter < 0) return false;
            decoded[1].char_idx = best_letter;
        } else if (idx1 < 41 || idx1 > 66) {
            return false;
        }
    }

    // Positions 2-6: digit (31-40) or letter (41-66)
    for (int i = 2; i < 7; i++) {
        int idx = decoded[i].char_idx;
        if (idx < 31 || idx > 66) return false;
    }

    // Build confidence from first 7 chars
    double total_prob = 0.0;
    for (int i = 0; i < 7; i++)
        total_prob += decoded[i].prob;
    out_confidence = total_prob / 7.0;
    return true;
}

std::string LPRRecognizer::applyPlateFormat(
    std::vector<DecodedChar>& decoded,
    const float* raw_data,
    double& confidence)
{
    int n = (int)decoded.size();

    // Calculate baseline confidence from per-step probabilities
    double total_prob = 0.0;
    for (auto& d : decoded)
        total_prob += d.prob;
    confidence = n > 0 ? total_prob / n : 0.0;

    // Build debug string
    std::string debug_str;
    for (auto& d : decoded)
        debug_str += charset_[d.char_idx];
    std::cerr << "[LPR] Raw decode: '" << debug_str << "' (" << n << " chars, conf=" << confidence << ")\n";

    // --- Strict format enforcement ---
    // Chinese standard blue plate:  [province][letter][5 digits/letters] = exactly 7 chars
    // Position 0: province character (charset index 0-30)
    // Position 1: letter (charset index 41-66)
    // Positions 2-6: digit (31-40) or letter (41-66) only

    // ---- Strategy 1: exactly 7 chars ----
    if (n == 7) {
        double vconf = confidence;
        std::vector<DecodedChar> copy = decoded;
        if (validate7CharPlate(copy, raw_data, time_steps_, charset_, vconf)) {
            decoded = copy;
            confidence = vconf;
            std::string result;
            for (auto& d : decoded) result += charset_[d.char_idx];
            std::cerr << "[LPR] Final: '" << result << "' (conf=" << confidence << ")\n";
            return result;
        }

        // ---- Salvage: try to fix common issues ----

        // Salvage A: position 0 is not a province → find best province from model raw output
        if (decoded[0].char_idx < 0 || decoded[0].char_idx > 30) {
            int ts = decoded[0].time_step;
            int best_province = -1;
            float best_val = -1e10f;
            for (int c = 0; c <= 30; c++) {
                float val = raw_data[c * time_steps_ + ts];
                if (val > best_val) { best_val = val; best_province = c; }
            }
            if (best_province >= 0) {
                std::cerr << "[LPR]   salvage: pos0 " << charset_[decoded[0].char_idx]
                          << "→" << charset_[best_province] << "\n";
                std::vector<DecodedChar> copy = decoded;
                copy[0].char_idx = best_province;
                double vconf = confidence;
                if (validate7CharPlate(copy, raw_data, time_steps_, charset_, vconf)) {
                    decoded = copy;
                    confidence = vconf * 0.92;
                    std::string result;
                    for (auto& d : decoded) result += charset_[d.char_idx];
                    std::cerr << "[LPR]   salvaged (province): '" << result << "' (conf=" << confidence << ")\n";
                    return result;
                }
            }
        }

        // Salvage B: last char is letter O → try digit 0 (common OCR confusion)
        {
            int last = 6;
            int idx = decoded[last].char_idx;
            if (idx == 65) {  // letter O → try digit 0
                std::vector<DecodedChar> copy = decoded;
                copy[last].char_idx = 31;  // digit 0
                double vconf = confidence;
                if (validate7CharPlate(copy, raw_data, time_steps_, charset_, vconf)) {
                    decoded = copy;
                    confidence = vconf * 0.95;
                    std::string result;
                    for (auto& d : decoded) result += charset_[d.char_idx];
                    std::cerr << "[LPR]   salvaged (O→0): '" << result << "' (conf=" << confidence << ")\n";
                    return result;
                }
            }
        }

        std::cerr << "[LPR]   rejected: 7-char validation failed\n";
        confidence = 0.0;
        return "";
    }

    // ---- Strategy 2: 8 chars - try ALL 7-char subsequences ----
    // The model sometimes outputs an extra spurious character (often a digit
    // between province and letter, or at start/end). Try dropping each position
    // and pick the subsequence that yields the highest confidence plate.
    if (n == 8) {
        std::cerr << "[LPR]   trying 8→7 salvage (all subsequences)...\n";

        int best_drop = -1;
        double best_sub_conf = 0.0;
        std::vector<DecodedChar> best_sub;

        for (int drop = 0; drop < 8; drop++) {
            std::vector<DecodedChar> sub;
            for (int i = 0; i < 8; i++) {
                if (i != drop) sub.push_back(decoded[i]);
            }
            double sub_conf = confidence;
            if (validate7CharPlate(sub, raw_data, time_steps_, charset_, sub_conf)) {
                if (sub_conf > best_sub_conf) {
                    best_sub_conf = sub_conf;
                    best_drop = drop;
                    best_sub = sub;
                }
            }
        }

        if (best_drop >= 0) {
            decoded = best_sub;
            confidence = best_sub_conf * 0.92;
            std::string result;
            for (auto& d : decoded) result += charset_[d.char_idx];
            std::cerr << "[LPR]   salvaged (drop pos " << best_drop << "): '"
                      << result << "' (conf=" << confidence << ")\n";
            return result;
        }

        std::cerr << "[LPR]   rejected: 8-char salvage failed\n";
        confidence = 0.0;
        return "";
    }

    // ---- Strategy 3: 6 chars - accept if province at pos0 is valid ----
    if (n == 6) {
        if (decoded[0].char_idx >= 0 && decoded[0].char_idx <= 30
            && decoded[0].prob >= 0.15) {
            bool valid = true;
            for (int i = 1; i < 6; i++) {
                int idx = decoded[i].char_idx;
                if (idx < 31 || idx > 66) { valid = false; break; }
            }
            if (valid) {
                std::string result;
                for (auto& d : decoded) result += charset_[d.char_idx];
                confidence *= 0.85;  // penalty for partial plate
                std::cerr << "[LPR]   accepted (6-char): '" << result << "' (conf=" << confidence << ")\n";
                return result;
            }
        }
        std::cerr << "[LPR]   rejected: 6-char validation failed\n";
        confidence = 0.0;
        return "";
    }

    // ---- All other lengths: reject ----
    std::cerr << "[LPR]   rejected: length " << n << " (only 6, 7, or 8 supported)\n";
    confidence = 0.0;
    return "";
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

    // Inference (serialized — DNN net is not thread-safe)
    cv::Mat output;
    try {
        std::lock_guard<std::mutex> lock(net_mutex_);
        net_.setInput(blob);
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
