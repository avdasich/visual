#include "heatmap.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

using namespace std;
namespace fs = std::filesystem;

static constexpr double K_PI = 3.14159265358979323846;
static constexpr double K_RAD = K_PI / 180.0;
static constexpr double K_EARTH_M = 6371000.0;

struct HeatColor {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 0;
};

const char* heatmap_criterion_name(HeatmapCriterion criterion) {
    switch (criterion) {
        case HeatmapCriterion::RSRP: return "RSRP";
        case HeatmapCriterion::RSRQ: return "RSRQ";
        case HeatmapCriterion::RSSI: return "RSSI";
        case HeatmapCriterion::Altitude: return "Altitude";
    }
    return "RSRP";
}

static HeatColor lerp(HeatColor a, HeatColor b, double t) {
    t = clamp(t, 0.0, 1.0);
    return {
        (unsigned char)lround((double)a.r + ((double)b.r - (double)a.r) * t),
        (unsigned char)lround((double)a.g + ((double)b.g - (double)a.g) * t),
        (unsigned char)lround((double)a.b + ((double)b.b - (double)a.b) * t),
        (unsigned char)lround((double)a.a + ((double)b.a - (double)a.a) * t)
    };
}

static HeatColor ramp(double t) {
    static const HeatColor c0{ 0, 0, 80, 150 };
    static const HeatColor c1{ 0, 120, 255, 165 };
    static const HeatColor c2{ 45, 210, 90, 175 };
    static const HeatColor c3{ 255, 210, 45, 185 };
    static const HeatColor c4{ 255, 48, 28, 200 };

    t = clamp(t, 0.0, 1.0);
    if (t < 0.25) return lerp(c0, c1, t / 0.25);
    if (t < 0.50) return lerp(c1, c2, (t - 0.25) / 0.25);
    if (t < 0.75) return lerp(c2, c3, (t - 0.50) / 0.25);
    return lerp(c3, c4, (t - 0.75) / 0.25);
}

static double normalized_value(double value, HeatmapCriterion criterion, double min_v, double max_v) {
    switch (criterion) {
        case HeatmapCriterion::RSRP:
            if (value <= -110.0) return -1.0;
            return clamp((value + 110.0) / 30.0, 0.0, 1.0);
        case HeatmapCriterion::RSRQ:
            if (value <= -20.0) return 0.0;
            return clamp((value + 20.0) / 10.0, 0.0, 1.0);
        case HeatmapCriterion::RSSI:
            if (value <= -110.0) return 0.0;
            return clamp((value + 110.0) / 60.0, 0.0, 1.0);
        case HeatmapCriterion::Altitude:
            if (max_v <= min_v) return 0.5;
            return clamp((value - min_v) / (max_v - min_v), 0.0, 1.0);
    }
    return 0.0;
}

static string output_name(HeatmapCriterion criterion, int pci, int earfcn) {
    string name = "heatmap_";
    name += heatmap_criterion_name(criterion);
    name += pci < 0 ? "_pci_all" : "_pci_" + to_string(pci);
    name += "_earfcn_";
    name += to_string(earfcn);
    name += ".png";
    return name;
}

struct ProjectedPoint {
    double x = 0.0;
    double y = 0.0;
    float value = 0.f;
};

static long long cell_key(int x, int y) {
    return ((long long)x << 32) ^ (unsigned int)y;
}

HeatmapImage heatmap_build_idw(const vector<HeatmapPoint>& points,
                               HeatmapBounds bounds,
                               HeatmapCriterion criterion,
                               int pci,
                               int earfcn,
                               float radius_m,
                               int image_w,
                               int image_h) {
    HeatmapImage image;
    image.w = image_w;
    image.h = image_h;
    image.rgba.assign((size_t)image_w * (size_t)image_h * 4, 0);

    if (points.empty() || bounds.lat_max <= bounds.lat_min || bounds.lon_max <= bounds.lon_min)
        return image;

    double min_v = points.front().value;
    double max_v = points.front().value;
    for (const auto& p : points) {
        min_v = min(min_v, (double)p.value);
        max_v = max(max_v, (double)p.value);
    }

    double radius = clamp((double)radius_m, 10.0, 40.0);
    double lat_mid = (bounds.lat_min + bounds.lat_max) * 0.5;
    double lon_scale = cos(lat_mid * K_RAD) * K_EARTH_M * K_RAD;
    double lat_scale = K_EARTH_M * K_RAD;
    double width_m = max(1.0, (bounds.lon_max - bounds.lon_min) * lon_scale);
    double height_m = max(1.0, (bounds.lat_max - bounds.lat_min) * lat_scale);
    double cell_m = radius;

    vector<ProjectedPoint> projected;
    projected.reserve(points.size());
    unordered_map<long long, vector<size_t>> grid;

    for (const auto& p : points) {
        double x = (p.lon - bounds.lon_min) * lon_scale;
        double y = (p.lat - bounds.lat_min) * lat_scale;
        if (x < -radius || x > width_m + radius || y < -radius || y > height_m + radius)
            continue;
        size_t idx = projected.size();
        projected.push_back({ x, y, p.value });
        int cx = (int)floor(x / cell_m);
        int cy = (int)floor(y / cell_m);
        grid[cell_key(cx, cy)].push_back(idx);
    }

    if (projected.empty())
        return image;

    for (int y = 0; y < image_h; y++) {
        double py = image_h <= 1 ? 0.0 : height_m * (1.0 - (double)y / (double)(image_h - 1));
        int cy = (int)floor(py / cell_m);
        for (int x = 0; x < image_w; x++) {
            double px = image_w <= 1 ? 0.0 : width_m * (double)x / (double)(image_w - 1);
            int cx = (int)floor(px / cell_m);

            double weighted = 0.0;
            double weights = 0.0;
            bool exact = false;
            double exact_value = 0.0;

            for (int gy = cy - 1; gy <= cy + 1 && !exact; gy++) {
                for (int gx = cx - 1; gx <= cx + 1 && !exact; gx++) {
                    auto it = grid.find(cell_key(gx, gy));
                    if (it == grid.end())
                        continue;
                    for (size_t idx : it->second) {
                        const auto& p = projected[idx];
                        double dx = px - p.x;
                        double dy = py - p.y;
                        double d2 = dx * dx + dy * dy;
                        if (d2 < 0.0625) {
                            exact = true;
                            exact_value = p.value;
                            break;
                        }
                        if (d2 > radius * radius)
                            continue;
                        double w = 1.0 / d2;
                        weighted += w * (double)p.value;
                        weights += w;
                    }
                }
            }

            if (!exact && weights <= 0.0)
                continue;

            double value = exact ? exact_value : weighted / weights;
            double norm = normalized_value(value, criterion, min_v, max_v);
            if (norm < 0.0)
                continue;

            HeatColor c = ramp(norm);
            size_t idx = ((size_t)y * (size_t)image_w + (size_t)x) * 4;
            image.rgba[idx + 0] = c.r;
            image.rgba[idx + 1] = c.g;
            image.rgba[idx + 2] = c.b;
            image.rgba[idx + 3] = c.a;
        }
    }

    image.file_path = output_name(criterion, pci, earfcn);
    stbi_write_png(image.file_path.c_str(), image.w, image.h, 4, image.rgba.data(), image.w * 4);
    return image;
}
