#pragma once

#include <string>
#include <vector>

enum class HeatmapCriterion {
    RSRP,
    RSRQ,
    RSSI,
    Altitude
};

struct HeatmapPoint {
    double lat = 0.0;
    double lon = 0.0;
    float value = 0.f;
};

struct HeatmapBounds {
    double lat_min = 0.0;
    double lat_max = 0.0;
    double lon_min = 0.0;
    double lon_max = 0.0;
};

struct HeatmapImage {
    int w = 0;
    int h = 0;
    std::vector<unsigned char> rgba;
    std::string file_path;
};

const char* heatmap_criterion_name(HeatmapCriterion criterion);

HeatmapImage heatmap_build_idw(const std::vector<HeatmapPoint>& points,
                               HeatmapBounds bounds,
                               HeatmapCriterion criterion,
                               int pci,
                               int earfcn,
                               float radius_m,
                               int image_w,
                               int image_h);
