#include "plate_recognizer.h"
#include "lpr_recognizer.h"
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

static std::string lpr_model_path;

// Count UTF-8 characters in a string (province chars are 3 bytes each)
static int utf8_chars(const std::string& s) {
    int count = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if ((s[i] & 0xC0) != 0x80)  // not a continuation byte
            count++;
    }
    return count;
}

void PlateRecognizer::setLPRModelPath(const std::string& path) {
    lpr_model_path = path;
    if (!path.empty()) {
        bool ok = LPRRecognizer::instance().load(path);
        std::cerr << "[PlateRecognizer] LPR model " << (ok ? "loaded" : "FAILED")
                  << ": " << path << "\n";
    }
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
        if (T[c] == -1) {
            // Handle padding '=' — stop processing this group
            break;
        }
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

// Forward declarations for static helpers used by initTemplates
static int countHoles(const cv::Mat& binary_char);

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
        cv::threshold(tmpl, tmpl, 128, 255, cv::THRESH_BINARY);
        digit_templates_[d] = tmpl;
        digit_contours_[d] = extractMainContour(tmpl);
    }

    // Precompute digit features
    digit_holes_.resize(10);
    digit_aspects_.resize(10);
    digit_fills_.resize(10);
    for (int d = 0; d < 10; ++d) {
        digit_holes_[d] = countHoles(digit_templates_[d]);
        digit_aspects_[d] = (double)digit_templates_[d].rows / digit_templates_[d].cols;
        digit_fills_[d] = (double)cv::countNonZero(digit_templates_[d]) /
                          (digit_templates_[d].rows * digit_templates_[d].cols);
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
        cv::threshold(tmpl, tmpl, 128, 255, cv::THRESH_BINARY);
        letter_templates_[i] = tmpl;
        letter_contours_[i] = extractMainContour(tmpl);
    }

    // Precompute letter features
    letter_holes_.resize(26);
    letter_aspects_.resize(26);
    letter_fills_.resize(26);
    for (int i = 0; i < 26; ++i) {
        letter_holes_[i] = countHoles(letter_templates_[i]);
        letter_aspects_[i] = (double)letter_templates_[i].rows / letter_templates_[i].cols;
        letter_fills_[i] = (double)cv::countNonZero(letter_templates_[i]) /
                           (letter_templates_[i].rows * letter_templates_[i].cols);
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
    // === Primary: HSV color detection (blue/green plates) ===
    cv::Mat hsv;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);

    // Fixed thresholds — proven balance between detection rate and false positives.
    // H ranges slightly widened to handle lighting-induced hue shift.
    cv::Mat blue_mask, green_mask;
    cv::inRange(hsv, cv::Scalar(95, 60, 50), cv::Scalar(130, 255, 255), blue_mask);
    cv::inRange(hsv, cv::Scalar(30, 50, 40), cv::Scalar(85, 255, 255), green_mask);

    cv::Mat color_mask = blue_mask | green_mask;

    // Morphological cleanup: connect nearby blue/green regions
    cv::Mat kernel5 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(color_mask, color_mask, cv::MORPH_CLOSE, kernel5);
    cv::morphologyEx(color_mask, color_mask, cv::MORPH_OPEN, kernel5);

    std::vector<std::vector<cv::Point>> color_contours;
    cv::findContours(color_mask.clone(), color_contours,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // Score each HSV candidate by how closely it matches a real plate
    struct ScoredRect {
        cv::Rect rect;
        double score;   // higher = more plate-like
        double aspect;  // aspect ratio for debugging
    };
    std::vector<ScoredRect> hsv_scored;

    for (const auto& cc : color_contours) {
        double area = cv::contourArea(cc);
        if (area < 1500 || area > 60000) continue;

        cv::RotatedRect rr = cv::minAreaRect(cc);
        float w = rr.size.width, h = rr.size.height;
        if (w < h) std::swap(w, h);

        double asp = w / h;
        if (asp < 1.5 || asp > 5.0) continue;

        // Score: prefer aspect ratio close to standard plate (3.14:1)
        double aspect_score = 1.0 - std::min(1.0, std::abs(asp - 3.14) / 2.0);

        cv::Rect br = rr.boundingRect() & cv::Rect(0, 0, src.cols, src.rows);
        double score = std::log(area) * 10.0 + aspect_score * 5.0;
        hsv_scored.push_back({br, score, asp});
    }

    // Sort HSV candidates by score descending
    std::sort(hsv_scored.begin(), hsv_scored.end(),
        [](const ScoredRect& a, const ScoredRect& b) { return a.score > b.score; });

    std::vector<cv::Rect> hsv_candidates;
    for (auto& s : hsv_scored) {
        hsv_candidates.push_back(s.rect);
        std::cerr << "[DBG]   HSV candidate: " << s.rect.width << "x" << s.rect.height
                  << " asp=" << s.aspect << " score=" << s.score << "\n";
    }

    std::cerr << "[DBG] detectPlateCandidates: " << hsv_candidates.size()
              << " HSV candidates\n";

    // === Edge detection: only used when HSV finds nothing ===
    // Edge detection catches plates regardless of color (yellow, white plates),
    // but generates many false positives, so use only as last resort.
    std::vector<cv::Rect> candidates = hsv_candidates;

    if (hsv_candidates.empty()) {
        cv::Mat processed = preprocess(src);
        std::vector<std::vector<cv::Point>> edge_contours;
        cv::findContours(processed.clone(), edge_contours,
                         cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        std::vector<cv::Rect> edge_candidates;
        for (const auto& contour : edge_contours) {
            double area = cv::contourArea(contour);
            if (area < 2500 || area > 50000) continue;

            cv::RotatedRect rect = cv::minAreaRect(contour);
            float width = rect.size.width;
            float height = rect.size.height;
            if (width < height) std::swap(width, height);

            double aspect = width / height;
            if (aspect < 2.5 || aspect > 4.0) continue;

            double rect_area = width * height;
            double extent = area / rect_area;
            if (extent < 0.4) continue;

            cv::Rect br = rect.boundingRect() & cv::Rect(0, 0, src.cols, src.rows);

            edge_candidates.push_back(br);
        }

        std::sort(edge_candidates.begin(), edge_candidates.end(),
            [](const cv::Rect& a, const cv::Rect& b) { return a.area() > b.area(); });

        std::cerr << "[DBG] detectPlateCandidates: " << edge_candidates.size()
                  << " edge candidates (HSV empty, last resort)\n";
        candidates = edge_candidates;
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

    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(10, 10));
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
        cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV, 15, 6);

    int otsu_pixels = cv::countNonZero(otsu_bin);
    int adapt_pixels = cv::countNonZero(adapt_bin);
    int total = gray.rows * gray.cols;
    double otsu_ratio = (double)otsu_pixels / total;
    double adapt_ratio = (double)adapt_pixels / total;

    cv::Mat best_bin;
    if (std::abs(otsu_ratio - 0.25) < std::abs(adapt_ratio - 0.25)) {
        best_bin = otsu_bin;
        std::cerr << "[DBG] binarizePlate: chose OTSU (ratio=" << otsu_ratio << ")\n";
    } else {
        best_bin = adapt_bin;
        std::cerr << "[DBG] binarizePlate: chose ADAPTIVE (ratio=" << adapt_ratio << ")\n";
    }

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(best_bin, best_bin, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(best_bin, best_bin, cv::MORPH_CLOSE, kernel);

    std::cerr << "[DBG] binarizePlate: output " << cv::countNonZero(best_bin) << "/" << total
              << " white pixels (" << (100.0*cv::countNonZero(best_bin)/total) << "%)\n";

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

    // 确保白色文字在黑色背景上（THRESH_BINARY_INV 对蓝色车牌产生黑字白底）
    // findContours 查找白色前景，需要文字为白色、背景为黑色
    int n_white = cv::countNonZero(binary);
    std::cerr << "[DBG] segmentCharacters: binary has " << n_white << "/" << (binary.rows*binary.cols)
              << " white pixels before polarity fix\n";
    if (n_white > binary.rows * binary.cols * 0.5) {
        cv::bitwise_not(binary, binary);
        std::cerr << "[DBG] segmentCharacters: polarity inverted\n";
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary.clone(), contours,
                     cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<std::pair<cv::Rect, double>> valid_chars;
    int plate_height = binary.rows;
    int plate_width = binary.cols;

    std::cerr << "[DBG] segmentCharacters: " << contours.size() << " contours found\n";

    for (size_t i = 0; i < contours.size(); ++i) {
        cv::Rect br = cv::boundingRect(contours[i]);

        if (br.width < 5 || br.height < plate_height / 4) continue;
        if (br.height > plate_height * 0.95) continue;
        if (br.width > plate_width / 2) continue;

        double char_aspect = (double)br.height / br.width;
        if (char_aspect < 0.8 || char_aspect > 12.0) continue;

        double area_ratio = (double)cv::contourArea(contours[i]) / br.area();
        if (area_ratio < 0.12) continue;

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

    std::cerr << "[DBG] segmentCharacters: " << merged.size() << " chars after merge\n";
    for (size_t i = 0; i < merged.size(); ++i)
        std::cerr << "[DBG]   char " << i << ": " << merged[i].width << "x" << merged[i].height
                  << " at (" << merged[i].x << "," << merged[i].y << ")\n";

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

// ========== Match a single character using multi-feature fusion ==========
char PlateRecognizer::matchCharacter(const cv::Mat& char_img, double& conf) {
    conf = 0.0;

    if (!templates_initialized_)
        initTemplates();

    // Threshold to pure binary to remove resize interpolation artifacts
    cv::Mat work;
    cv::threshold(char_img, work, 100, 255, cv::THRESH_BINARY);

    // Normalize polarity: templates are white-on-black (character=255, bg=0).
    // THRESH_BINARY_INV for blue plates produces black-on-white → need inversion.
    // A properly cropped char fills ~20-45% in white-on-black mode, so if
    // white pixels exceed 55%, the polarity is inverted.
    if (cv::countNonZero(work) > work.rows * work.cols * 0.55)
        cv::bitwise_not(work, work);

    // Extract the contour of the input character
    std::vector<cv::Point> input_contour = extractMainContour(work);
    if (input_contour.empty()) return '?';

    // Compute input features
    int input_holes = countHoles(work);
    double input_aspect = (double)work.rows / work.cols;
    double input_fill = (double)cv::countNonZero(work) /
                        (work.rows * work.cols);

    struct Candidate {
        int idx;
        char type;   // 'd' = digit, 'l' = letter
        double shape_score;
        double ncc_score;
        int hole_diff;
        double aspect_diff;
        double fill_diff;
    };
    std::vector<Candidate> candidates;

    // Compare against digit templates
    for (int d = 0; d < 10; ++d) {
        if (digit_contours_[d].empty()) continue;

        double shape_score = cv::matchShapes(input_contour, digit_contours_[d],
                                              cv::CONTOURS_MATCH_I3, 0);

        cv::Mat ncc_result;
        cv::matchTemplate(work, digit_templates_[d], ncc_result, cv::TM_CCOEFF_NORMED);
        double ncc_score = ncc_result.at<float>(0, 0);

        candidates.push_back({d, 'd', shape_score, ncc_score,
                             abs(input_holes - digit_holes_[d]),
                             std::abs(input_aspect - digit_aspects_[d]),
                             std::abs(input_fill - digit_fills_[d])});
    }

    // Compare against letter templates
    for (int l = 0; l < 26; ++l) {
        if (letter_contours_[l].empty()) continue;

        double shape_score = cv::matchShapes(input_contour, letter_contours_[l],
                                              cv::CONTOURS_MATCH_I3, 0);

        cv::Mat ncc_result;
        cv::matchTemplate(work, letter_templates_[l], ncc_result, cv::TM_CCOEFF_NORMED);
        double ncc_score = ncc_result.at<float>(0, 0);

        candidates.push_back({l, 'l', shape_score, ncc_score,
                             abs(input_holes - letter_holes_[l]),
                             std::abs(input_aspect - letter_aspects_[l]),
                             std::abs(input_fill - letter_fills_[l])});
    }

    if (candidates.empty()) return '?';

    // Rank-based fusion: for each metric, rank all candidates (lower rank = better)
    auto rankBy = [&](auto proj) -> std::vector<int> {
        std::vector<int> idx(candidates.size());
        for (size_t i = 0; i < candidates.size(); ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            return proj(candidates[a]) < proj(candidates[b]);
        });
        std::vector<int> ranks(candidates.size());
        for (size_t i = 0; i < idx.size(); ++i)
            ranks[idx[i]] = (int)i;
        return ranks;
    };

    auto shape_ranks  = rankBy([](const Candidate& c) { return c.shape_score; });
    auto ncc_ranks    = rankBy([](const Candidate& c) { return -c.ncc_score; }); // higher NCC = better
    auto hole_ranks   = rankBy([](const Candidate& c) { return (double)c.hole_diff; });
    auto aspect_ranks = rankBy([](const Candidate& c) { return c.aspect_diff; });
    auto fill_ranks   = rankBy([](const Candidate& c) { return c.fill_diff; });

    // Weighted rank sum: shape=0.20, NCC=0.35, hole=0.20, aspect=0.10, fill=0.15
    int best_idx = -1;
    double best_score = std::numeric_limits<double>::max();
    size_t n = candidates.size();

    for (size_t i = 0; i < n; ++i) {
        double combined =
            0.20 * shape_ranks[i] / n +
            0.35 * ncc_ranks[i] / n +
            0.20 * hole_ranks[i] / n +
            0.10 * aspect_ranks[i] / n +
            0.15 * fill_ranks[i] / n;

        if (combined < best_score) {
            best_score = combined;
            best_idx = (int)i;
        }
    }

    if (best_idx < 0) return '?';

    // Convert best_score (0~1, lower=better) to confidence (0~1, higher=better)
    conf = std::max(0.0, 1.0 - best_score * 1.5);

    const auto& best = candidates[best_idx];
    if (best.type == 'd') return '0' + best.idx;
    else return 'A' + best.idx;
}

// ========== Recognize characters on plate ==========
std::string PlateRecognizer::recognizeCharacters(const cv::Mat& plate_img, double& confidence) {
    confidence = 0.0;

    std::vector<cv::Mat> chars = segmentCharacters(plate_img);
    if (chars.size() < 3) {
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

    // Try each candidate in order of score.
    // Accept the first one with LPRNet confidence >= MIN_CONFIDENCE
    // AND exactly 7 or 8 characters (standard Chinese plate format).
    // Low confidence threshold: format validation (applyPlateFormat) is the primary gatekeeper.
    // Only accepts 7/8 char plates with correct province+letter structure.
    const double MIN_CONFIDENCE = 0.3;
    double confidence = 0.0;
    std::string raw_plate;
    cv::Rect best_rect;

    for (size_t ci = 0; ci < candidates.size(); ++ci) {
        cv::Rect plate_rect = candidates[ci];

        // Add 15% margin for LPRNet context (single expansion here only)
        int margin_x = static_cast<int>(plate_rect.width * 0.15);
        int margin_y = static_cast<int>(plate_rect.height * 0.15);
        plate_rect.x = std::max(0, plate_rect.x - margin_x);
        plate_rect.y = std::max(0, plate_rect.y - margin_y);
        plate_rect.width = std::min(frame.cols - plate_rect.x, plate_rect.width + 2 * margin_x);
        plate_rect.height = std::min(frame.rows - plate_rect.y, plate_rect.height + 2 * margin_y);

        cv::Mat plate_region = frame(plate_rect).clone();

        std::cerr << "[DBG] Candidate " << ci << ": " << plate_region.cols << "x"
                  << plate_region.rows << " at (" << plate_rect.x << "," << plate_rect.y << ")\n";

        // Use LPRNet deep learning model if available
        if (LPRRecognizer::instance().isLoaded()) {
            LPRRecognizer::Result lpr_result = LPRRecognizer::instance().recognize(plate_region);
            std::cerr << "[DBG]   LPRNet: '" << lpr_result.plate << "' (conf="
                      << lpr_result.confidence << ", len=" << lpr_result.plate.size() << ")\n";

            // Only accept standard-length plates: 7 (blue) or 8 (green new energy)
            int plen = utf8_chars(lpr_result.plate);
            bool valid_len = (plen == 7 || plen == 8);

            if (valid_len && lpr_result.confidence >= MIN_CONFIDENCE) {
                raw_plate = lpr_result.plate;
                confidence = lpr_result.confidence;
                best_rect = candidates[ci];
                std::cerr << "[DBG]   -> accepted\n";
                break;
            }
        } else if (ci == 0) {
            // No LPRNet model: fall back to template matching on first candidate only
            raw_plate = recognizeCharacters(plate_region, confidence);
            if (raw_plate.size() >= 7 && raw_plate.size() <= 8 && confidence >= MIN_CONFIDENCE) {
                best_rect = candidates[ci];
                break;
            }
        }
    }

    // Set result color from the winning candidate
    if (best_rect.area() > 0) {
        cv::Mat plate_region = frame(best_rect).clone();
        result.plate_color = analyzePlateColor(plate_region);
    }

    if (raw_plate.empty()) {
        result.message = "检测到车牌区域但无法识别字符（所有候选置信度均低于阈值）";
        return result;
    }

    result.plate_number = raw_plate;
    result.confidence = confidence;
    result.message = "识别成功";
    return result;
}
