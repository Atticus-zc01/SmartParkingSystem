#include "plate_recognizer.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <limits>

PlateRecognizer& PlateRecognizer::instance() {
    static PlateRecognizer inst;
    return inst;
}

// ========== Base64 Decode ==========
static const std::string b64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::vector<uchar> PlateRecognizer::base64Decode(const std::string& data) {
    std::vector<uchar> out;
    std::string clean;
    for (char c : data) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
            clean += c;
    }

    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; ++i)
        T[b64_chars[i]] = i;

    int val = 0, bits = -8;
    for (char c : clean) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uchar>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// ========== Helpers for contour extraction ==========
static std::vector<cv::Point> extractMainContour(const cv::Mat& binary_img) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary_img.clone(), contours,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return {};

    return *std::max_element(contours.begin(), contours.end(),
        [](const auto& a, const auto& b) {
            return cv::contourArea(a) < cv::contourArea(b);
        });
}

// ========== Initialize templates ==========
void PlateRecognizer::initTemplates() {
    if (templates_initialized_) return;
    templates_initialized_ = true;

    int font = cv::FONT_HERSHEY_SIMPLEX;
    double scale = 0.9;
    int thickness = 2;

    // Digit templates (0-9)
    digit_templates_.resize(10);
    digit_contours_.resize(10);
    for (int d = 0; d < 10; ++d) {
        std::string text = std::to_string(d);
        int baseline;
        cv::Size sz = cv::getTextSize(text, font, scale, thickness, &baseline);
        cv::Mat tmpl(sz.height + baseline, sz.width, CV_8UC1, cv::Scalar(0));
        cv::putText(tmpl, text, cv::Point(0, sz.height), font, scale, cv::Scalar(255), thickness);
        cv::dilate(tmpl, tmpl, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2)));
        cv::dilate(tmpl, tmpl, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2)));
        cv::resize(tmpl, tmpl, cv::Size(28, 44));
        digit_templates_[d] = tmpl;
        digit_contours_[d] = extractMainContour(tmpl);
    }

    // Letter templates (A-Z)
    letter_templates_.resize(26);
    letter_contours_.resize(26);
    for (int i = 0; i < 26; ++i) {
        std::string text(1, 'A' + i);
        int baseline;
        cv::Size sz = cv::getTextSize(text, font, scale, thickness, &baseline);
        cv::Mat tmpl(sz.height + baseline, sz.width, CV_8UC1, cv::Scalar(0));
        cv::putText(tmpl, text, cv::Point(0, sz.height), font, scale, cv::Scalar(255), thickness);
        cv::dilate(tmpl, tmpl, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2)));
        cv::dilate(tmpl, tmpl, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2)));
        cv::resize(tmpl, tmpl, cv::Size(28, 44));
        letter_templates_[i] = tmpl;
        letter_contours_[i] = extractMainContour(tmpl);
    }
}

// ========== Image preprocessing for plate detection ==========
cv::Mat PlateRecognizer::preprocess(const cv::Mat& src) {
    cv::Mat gray, blurred, edges;

    if (src.channels() == 3)
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else
        gray = src.clone();

    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);

    cv::adaptiveThreshold(blurred, edges, 255,
        cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV, 11, 2);

    cv::Mat morph;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(edges, morph, cv::MORPH_CLOSE, kernel);

    return morph;
}

// ========== Detect plate candidates ==========
std::vector<cv::Rect> PlateRecognizer::detectPlateCandidates(const cv::Mat& src) {
    std::vector<cv::Rect> candidates;
    cv::Mat processed = preprocess(src);

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(processed.clone(), contours, hierarchy,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < 1000 || area > 50000) continue;

        cv::RotatedRect rect = cv::minAreaRect(contour);
        float width = rect.size.width;
        float height = rect.size.height;
        if (width < height) std::swap(width, height);

        double aspect = width / height;
        if (aspect < 1.8 || aspect > 4.5) continue;

        double rect_area = width * height;
        double extent = area / rect_area;
        if (extent < 0.4) continue;

        candidates.push_back(rect.boundingRect());
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const cv::Rect& a, const cv::Rect& b) {
            return a.area() > b.area();
        });

    if (candidates.empty()) {
        cv::Mat hsv;
        cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);

        cv::Mat blue_mask, green_mask;
        cv::inRange(hsv, cv::Scalar(100, 80, 60), cv::Scalar(124, 255, 255), blue_mask);
        cv::inRange(hsv, cv::Scalar(35, 80, 60), cv::Scalar(77, 255, 255), green_mask);

        cv::Mat color_mask = blue_mask | green_mask;
        cv::morphologyEx(color_mask, color_mask,
            cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));

        std::vector<std::vector<cv::Point>> color_contours;
        cv::findContours(color_mask.clone(), color_contours,
                         cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        for (const auto& cc : color_contours) {
            double area = cv::contourArea(cc);
            if (area < 1000 || area > 50000) continue;

            cv::RotatedRect rr = cv::minAreaRect(cc);
            float w = rr.size.width, h = rr.size.height;
            if (w < h) std::swap(w, h);

            double asp = w / h;
            if (asp < 1.8 || asp > 4.5) continue;

            candidates.push_back(rr.boundingRect());
        }
    }

    return candidates;
}

// ========== Analyze plate color ==========
std::string PlateRecognizer::analyzePlateColor(const cv::Mat& plate_region) {
    if (plate_region.empty()) return "unknown";

    cv::Mat hsv;
    cv::cvtColor(plate_region, hsv, cv::COLOR_BGR2HSV);

    cv::Mat blue_mask, green_mask, yellow_mask;
    cv::inRange(hsv, cv::Scalar(100, 80, 60), cv::Scalar(124, 255, 255), blue_mask);
    cv::inRange(hsv, cv::Scalar(35, 80, 60), cv::Scalar(77, 255, 255), green_mask);
    cv::inRange(hsv, cv::Scalar(15, 80, 100), cv::Scalar(35, 255, 255), yellow_mask);

    int total = plate_region.rows * plate_region.cols;
    double blue_pct = 100.0 * cv::countNonZero(blue_mask) / total;
    double green_pct = 100.0 * cv::countNonZero(green_mask) / total;
    double yellow_pct = 100.0 * cv::countNonZero(yellow_mask) / total;

    if (blue_pct > 15) return "blue";
    if (green_pct > 15) return "green";
    if (yellow_pct > 15) return "yellow";
    return "unknown";
}

// ========== Preprocess plate region for character recognition ==========
static cv::Mat preprocessPlateRegion(const cv::Mat& plate_color) {
    cv::Mat gray;
    if (plate_color.channels() == 3)
        cv::cvtColor(plate_color, gray, cv::COLOR_BGR2GRAY);
    else
        gray = plate_color.clone();

    int target_height = 80;
    double scale = target_height / (double)gray.rows;
    int target_width = (int)(gray.cols * scale);
    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(target_width, target_height));

    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(3.0, cv::Size(8, 8));
    cv::Mat enhanced;
    clahe->apply(resized, enhanced);

    cv::Mat blurred;
    cv::GaussianBlur(enhanced, blurred, cv::Size(3, 3), 0);

    return blurred;
}

// ========== Binarize plate image ==========
static cv::Mat binarizePlate(const cv::Mat& gray) {
    cv::Mat otsu_bin;
    cv::threshold(gray, otsu_bin, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

    cv::Mat adapt_bin;
    cv::adaptiveThreshold(gray, adapt_bin, 255,
        cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV, 15, 4);

    int otsu_pixels = cv::countNonZero(otsu_bin);
    int adapt_pixels = cv::countNonZero(adapt_bin);
    int total = gray.rows * gray.cols;
    double otsu_ratio = (double)otsu_pixels / total;
    double adapt_ratio = (double)adapt_pixels / total;

    cv::Mat best_bin;
    if (std::abs(otsu_ratio - 0.25) < std::abs(adapt_ratio - 0.25))
        best_bin = otsu_bin;
    else
        best_bin = adapt_bin;

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    cv::morphologyEx(best_bin, best_bin, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(best_bin, best_bin, cv::MORPH_CLOSE, kernel);

    return best_bin;
}

// ========== Count holes in a character (topological feature) ==========
static int countHoles(const cv::Mat& binary_char) {
    cv::Mat inverted;
    cv::bitwise_not(binary_char, inverted);

    // Fill from border to remove background
    int h = inverted.rows, w = inverted.cols;
    cv::Mat mask = cv::Mat::zeros(h + 2, w + 2, CV_8UC1);
    cv::floodFill(inverted, mask, cv::Point(0, 0), 0);

    // Remaining white regions are holes
    std::vector<std::vector<cv::Point>> hole_contours;
    cv::findContours(inverted.clone(), hole_contours,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int holes = 0;
    for (const auto& c : hole_contours) {
        double area = cv::contourArea(c);
        if (area > 5) holes++;
    }
    return holes;
}

// ========== Segment characters from plate ==========
std::vector<cv::Mat> PlateRecognizer::segmentCharacters(const cv::Mat& plate_img) {
    std::vector<cv::Mat> chars;
    if (plate_img.empty()) return chars;

    cv::Mat processed = preprocessPlateRegion(plate_img);
    cv::Mat binary = binarizePlate(processed);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary.clone(), contours,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<std::pair<cv::Rect, double>> valid_chars;
    int plate_height = binary.rows;
    int plate_width = binary.cols;

    for (size_t i = 0; i < contours.size(); ++i) {
        cv::Rect br = cv::boundingRect(contours[i]);

        if (br.width < 5 || br.height < plate_height / 4) continue;
        if (br.height > plate_height * 0.95) continue;
        if (br.width > plate_width / 2) continue;

        double char_aspect = (double)br.height / br.width;
        if (char_aspect < 0.8 || char_aspect > 6.0) continue;

        double area_ratio = (double)cv::contourArea(contours[i]) / br.area();
        if (area_ratio < 0.1) continue;

        valid_chars.push_back({br, (double)br.x});
    }

    std::sort(valid_chars.begin(), valid_chars.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    std::vector<cv::Rect> merged;
    for (const auto& vc : valid_chars) {
        cv::Rect r = vc.first;
        bool merged_flag = false;
        for (auto& m : merged) {
            cv::Rect inter = r & m;
            if (inter.width > 0 && inter.height > 0) {
                double overlap_r = (double)inter.area() / std::min(r.area(), m.area());
                if (overlap_r > 0.3) {
                    if (r.area() > m.area()) m = r;
                    merged_flag = true;
                    break;
                }
            }
        }
        if (!merged_flag)
            merged.push_back(r);
    }

    for (const auto& r : merged) {
        cv::Mat char_img = binary(r).clone();

        int top = 0, bottom = char_img.rows - 1;
        for (int y = 0; y < char_img.rows; ++y) {
            if (cv::countNonZero(char_img.row(y)) > 2) { top = y; break; }
        }
        for (int y = char_img.rows - 1; y >= 0; --y) {
            if (cv::countNonZero(char_img.row(y)) > 2) { bottom = y; break; }
        }

        if (bottom <= top) continue;

        cv::Mat cropped = char_img.rowRange(top, bottom + 1);

        // Add border to preserve shape info
        cv::Mat padded;
        int border = 4;
        cv::copyMakeBorder(cropped, padded, border, border, border, border,
                           cv::BORDER_CONSTANT, cv::Scalar(0));

        cv::Mat normalized;
        cv::resize(padded, normalized, cv::Size(28, 44));
        chars.push_back(normalized);
    }

    return chars;
}

// ========== Match a single character using Hu moments (shape matching) ==========
char PlateRecognizer::matchCharacter(const cv::Mat& char_img, double& conf) {
    conf = 0.0;

    if (!templates_initialized_)
        initTemplates();

    // Extract the contour of the input character
    std::vector<cv::Point> input_contour = extractMainContour(char_img);
    if (input_contour.empty()) return '?';

    // Count holes as a topological feature
    int input_holes = countHoles(char_img);

    double best_score = std::numeric_limits<double>::max();
    int best_idx = -1;
    char best_type = '?';

    // Compare against digit contours using Hu moments
    for (int d = 0; d < 10; ++d) {
        if (digit_contours_[d].empty()) continue;

        // Filter by hole count first (fast rejection)
        int tmpl_holes = countHoles(digit_templates_[d]);
        if (input_holes != tmpl_holes) continue;

        double score = cv::matchShapes(input_contour, digit_contours_[d],
                                        cv::CONTOURS_MATCH_I3, 0);
        if (score < best_score) {
            best_score = score;
            best_idx = d;
            best_type = 'd';
        }
    }

    // Compare against letter contours
    for (int l = 0; l < 26; ++l) {
        if (letter_contours_[l].empty()) continue;

        int tmpl_holes = countHoles(letter_templates_[l]);
        if (input_holes != tmpl_holes) continue;

        double score = cv::matchShapes(input_contour, letter_contours_[l],
                                        cv::CONTOURS_MATCH_I3, 0);
        if (score < best_score) {
            best_score = score;
            best_idx = l;
            best_type = 'l';
        }
    }

    // Convert matchShapes score to a confidence (0~1)
    // matchShapes returns 0 for perfect match, higher for worse match
    conf = std::max(0.0, 1.0 - best_score * 5.0);

    if (best_idx >= 0) {
        if (best_type == 'd') return '0' + best_idx;
        else return 'A' + best_idx;
    }
    return '?';
}

// ========== Recognize characters on plate ==========
std::string PlateRecognizer::recognizeCharacters(const cv::Mat& plate_img, double& confidence) {
    confidence = 0.0;

    std::vector<cv::Mat> chars = segmentCharacters(plate_img);
    if (chars.size() < 5) {
        return "";
    }

    std::string plate;
    double total_conf = 0.0;
    int valid_chars = 0;

    for (size_t i = 0; i < chars.size() && i < 8; ++i) {
        double char_conf = 0.0;
        char c = matchCharacter(chars[i], char_conf);
        plate += c;
        total_conf += char_conf;
        valid_chars++;
    }

    confidence = (valid_chars > 0) ? (total_conf / valid_chars) : 0.0;
    return plate;
}

// ========== Main recognize methods ==========
PlateRecognizer::RecognitionResult PlateRecognizer::recognize(const std::string& image_base64) {
    RecognitionResult result;

    std::vector<uchar> img_data = base64Decode(image_base64);
    if (img_data.empty()) {
        result.message = "图片数据为空或解码失败";
        return result;
    }

    cv::Mat img = cv::imdecode(img_data, cv::IMREAD_COLOR);
    if (img.empty()) {
        result.message = "图片解码失败";
        return result;
    }

    return recognize(img);
}

PlateRecognizer::RecognitionResult PlateRecognizer::recognize(const cv::Mat& frame) {
    RecognitionResult result;

    if (!templates_initialized_)
        initTemplates();

    std::vector<cv::Rect> candidates = detectPlateCandidates(frame);

    if (candidates.empty()) {
        result.message = "未检测到车牌区域，请确保车牌清晰可见";
        return result;
    }

    cv::Rect plate_rect = candidates[0];

    int margin_x = static_cast<int>(plate_rect.width * 0.05);
    int margin_y = static_cast<int>(plate_rect.height * 0.05);
    plate_rect.x = std::max(0, plate_rect.x - margin_x);
    plate_rect.y = std::max(0, plate_rect.y - margin_y);
    plate_rect.width = std::min(frame.cols - plate_rect.x, plate_rect.width + 2 * margin_x);
    plate_rect.height = std::min(frame.rows - plate_rect.y, plate_rect.height + 2 * margin_y);

    cv::Mat plate_region = frame(plate_rect).clone();

    result.plate_color = analyzePlateColor(plate_region);

    double confidence = 0.0;
    std::string raw_plate = recognizeCharacters(plate_region, confidence);

    if (raw_plate.empty()) {
        result.message = "检测到车牌区域但无法识别字符";
        return result;
    }

    std::string clean_plate;
    for (size_t i = 0; i < raw_plate.size(); ++i) {
        char c = raw_plate[i];
        if (c == '?' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            clean_plate += c;
        }
    }

    if (clean_plate.size() < 5) {
        result.message = "识别结果不完整: " + raw_plate;
        result.plate_number = raw_plate;
        result.confidence = confidence;
        return result;
    }

    if (clean_plate.size() >= 6 && clean_plate[0] >= 'A' && clean_plate[0] <= 'Z') {
        clean_plate = "?" + clean_plate;
    }

    result.plate_number = clean_plate;
    result.confidence = confidence;
    result.message = "识别成功";
    return result;
}
