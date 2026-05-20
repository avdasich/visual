#include "gui.h"
#include "types.h"
#include "json_parser.h"
#include "database.h"

#include <SDL.h>
#include <cmath>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#include <GL/gl.h>
#endif

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "implot.h"

#include "osm_map.h"
#include "heatmap.h"

#include <fstream>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <future>
#include <sstream>
#include <iomanip>
#include <set>
#include <map>
#include <climits>

using namespace std;

static bool valid_geo(double lat, double lon) {
    return !(lat == 0.0 && lon == 0.0) &&
           lat >= -90.0 && lat <= 90.0 &&
           lon >= -180.0 && lon <= 180.0;
}

static void upload_rgba_texture(GLuint& tex, const HeatmapImage& image) {
    if (image.rgba.empty())
        return;

    if (tex == 0)
        glGenTextures(1, &tex);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.w, image.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
}

static void setup_route_limits_once(const SignalHistory& hist, size_t& fitted_count) {
    if (hist.lon.empty() || fitted_count == hist.lon.size())
        return;

    double lat_min = *min_element(hist.lat.begin(), hist.lat.end());
    double lat_max = *max_element(hist.lat.begin(), hist.lat.end());
    double lon_min = *min_element(hist.lon.begin(), hist.lon.end());
    double lon_max = *max_element(hist.lon.begin(), hist.lon.end());
    double pad_lat = max(0.002, (lat_max - lat_min) * 0.15);
    double pad_lon = max(0.002, (lon_max - lon_min) * 0.15);
    ImPlot::SetupAxisLimits(ImAxis_X1, lon_min - pad_lon, lon_max + pad_lon, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, lat_min - pad_lat, lat_max + pad_lat, ImGuiCond_Always);
    fitted_count = hist.lon.size();
}

static void draw_route_items(const SignalHistory& hist, bool points) {
    if (hist.lon.empty())
        return;

    ImPlot::PlotLine("Track", hist.lon.data(), hist.lat.data(), (int)hist.lon.size());
    if (points)
        ImPlot::PlotScatter("Points", hist.lon.data(), hist.lat.data(), (int)hist.lon.size());

    double sx = hist.lon.front();
    double sy = hist.lat.front();
    double ex = hist.lon.back();
    double ey = hist.lat.back();
    ImPlot::PlotScatter("Start", &sx, &sy, 1);
    ImPlot::PlotScatter("End", &ex, &ey, 1);
}

static void draw_plot_tooltip(int zoom) {
    if (!ImPlot::IsPlotHovered())
        return;

    ImPlotPoint mp = ImPlot::GetPlotMousePos();
    ImGui::BeginTooltip();
    ImGui::Text("Lat %.6f  Lon %.6f  z=%d", mp.y, mp.x, zoom);
    ImGui::EndTooltip();
}

static void draw_lte_metric_plot(const char* title,
                                 const char* y_label,
                                 const map<int, PciHistory>& data,
                                 const vector<float> PciHistory::*values) {
    if (ImPlot::BeginPlot(title, ImVec2(-1, 200))) {
        ImPlot::SetupAxes("t", y_label);
        for (const auto& [pci, ph] : data) {
            string label = "PCI " + to_string(pci);
            const auto& ys = ph.*values;
            ImPlot::PlotLine(label.c_str(), ph.t.data(), ys.data(), (int)ys.size());
        }
        ImPlot::EndPlot();
    }
}

static int busiest_pci(const SignalHistory& hist) {
    int best = INT_MIN;
    size_t best_count = 0;
    for (const auto& [pci, ph] : hist.lte_by_pci) {
        if (ph.rsrp.size() > best_count) {
            best = pci;
            best_count = ph.rsrp.size();
        }
    }
    return best;
}

static int busiest_earfcn(const PciHistory& ph) {
    map<int, int> counts;
    for (int v : ph.earfcn)
        counts[v]++;

    int best = 0;
    int best_count = -1;
    for (const auto& [earfcn, count] : counts) {
        if (count > best_count) {
            best = earfcn;
            best_count = count;
        }
    }
    return best;
}

static vector<int> earfcn_list(const PciHistory& ph) {
    set<int> values(ph.earfcn.begin(), ph.earfcn.end());
    return vector<int>(values.begin(), values.end());
}

static vector<int> earfcn_list_all(const SignalHistory& hist) {
    set<int> values;
    for (const auto& [pci, ph] : hist.lte_by_pci)
        values.insert(ph.earfcn.begin(), ph.earfcn.end());
    return vector<int>(values.begin(), values.end());
}

static int busiest_earfcn_all(const SignalHistory& hist) {
    map<int, int> counts;
    for (const auto& [pci, ph] : hist.lte_by_pci)
        for (int v : ph.earfcn)
            counts[v]++;

    int best = 0;
    int best_count = -1;
    for (const auto& [earfcn, count] : counts) {
        if (count > best_count) {
            best = earfcn;
            best_count = count;
        }
    }
    return best;
}

static vector<HeatmapPoint> collect_heatmap_points(const PciHistory& ph, int earfcn, HeatmapCriterion criterion) {
    vector<HeatmapPoint> points;
    size_t n = min({ph.lat.size(), ph.lon.size(), ph.earfcn.size(), ph.rsrp.size(), ph.rsrq.size(), ph.rssi.size(), ph.alt.size()});
    points.reserve(n);

    for (size_t i = 0; i < n; i++) {
        if (ph.earfcn[i] != earfcn)
            continue;

        float value = 0.f;
        switch (criterion) {
            case HeatmapCriterion::RSRP: value = ph.rsrp[i]; break;
            case HeatmapCriterion::RSRQ: value = ph.rsrq[i]; break;
            case HeatmapCriterion::RSSI: value = ph.rssi[i]; break;
            case HeatmapCriterion::Altitude: value = ph.alt[i]; break;
        }

        if (criterion != HeatmapCriterion::Altitude && value == 0.f)
            continue;
        points.push_back({ ph.lat[i], ph.lon[i], value });
    }
    return points;
}

static vector<HeatmapPoint> collect_heatmap_points_all(const SignalHistory& hist, int earfcn, HeatmapCriterion criterion) {
    vector<HeatmapPoint> points;
    for (const auto& [pci, ph] : hist.lte_by_pci) {
        vector<HeatmapPoint> part = collect_heatmap_points(ph, earfcn, criterion);
        points.insert(points.end(), part.begin(), part.end());
    }
    return points;
}

static string heatmap_request_key(HeatmapCriterion criterion, int pci, int earfcn, float radius, HeatmapBounds b) {
    ostringstream ss;
    ss << heatmap_criterion_name(criterion) << '|'
       << pci << '|'
       << earfcn << '|'
       << fixed << setprecision(1) << radius << '|'
       << setprecision(5)
       << b.lat_min << '|'
       << b.lat_max << '|'
       << b.lon_min << '|'
       << b.lon_max;
    return ss.str();
}

static void draw_heatmap_legend() {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = min(360.f, ImGui::GetContentRegionAvail().x);
    float h = 14.f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int steps = 64;
    for (int i = 0; i < steps; i++) {
        float t0 = (float)i / (float)steps;
        float t1 = (float)(i + 1) / (float)steps;
        ImU32 col;
        if (t0 < 0.25f) col = IM_COL32(0, (int)(120.f * t0 / 0.25f), 255, 210);
        else if (t0 < 0.50f) col = IM_COL32((int)(45.f * (t0 - 0.25f) / 0.25f), 210, (int)(255.f - 165.f * (t0 - 0.25f) / 0.25f), 210);
        else if (t0 < 0.75f) col = IM_COL32((int)(45.f + 210.f * (t0 - 0.50f) / 0.25f), 210, (int)(90.f - 45.f * (t0 - 0.50f) / 0.25f), 210);
        else col = IM_COL32(255, (int)(210.f - 162.f * (t0 - 0.75f) / 0.25f), (int)(45.f - 17.f * (t0 - 0.75f) / 0.25f), 210);
        dl->AddRectFilled(ImVec2(pos.x + w * t0, pos.y), ImVec2(pos.x + w * t1 + 1.f, pos.y + h), col);
    }
    ImGui::InvisibleButton("##heatmap_legend", ImVec2(w, h));
    ImGui::TextUnformatted("Poor/weak");
    ImGui::SameLine(w - 68.f);
    ImGui::TextUnformatted("Excellent");
}

void draw_data_panel() {
    TelemetryData d;
    {
        lock_guard<mutex> lk(g_mtx);
        d = g_data;

        bool db_ok = (db_conn != nullptr && PQstatus(db_conn) == CONNECTION_OK);

        ImGui::TextColored(
            db_ok ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1),
            "DB: %s",
            db_ok ? "Connected" : "Disconnected"
        );
    }

    ImGui::Text("Location");
    ImGui::Separator();
    ImGui::Text("Lat:      %.6f", d.lat);
    ImGui::Text("Lon:      %.6f", d.lon);
    ImGui::Text("Alt:      %.2f m", d.alt);
    ImGui::Text("Accuracy: %.2f m", d.acc);

    if (!d.lte.empty()) {
        ImGui::Spacing();
        ImGui::Text("LTE");
        ImGui::Separator();
        const auto& c = d.lte[0];
        ImGui::Text("PCI:    %d", c.pci);
        ImGui::Text("EARFCN: %d", c.earfcn);
        ImGui::Text("Band:   %d", c.band);
        ImGui::Text("TAC:    %d", c.tac);
        ImGui::Text("MCC/MNC: %s/%s", c.mcc.c_str(), c.mnc.c_str());
        ImGui::Text("RSRP:  %d dBm", c.rsrp);
        ImGui::Text("RSRQ:  %d dB",  c.rsrq);
        ImGui::Text("RSSI:  %d dBm", c.rssi);
        ImGui::Text("SINR:  %d dB",  c.rssnr);
        ImGui::Text("TA:    %d",      c.ta);
    }

    if (!d.gsm.empty()) {
        ImGui::Spacing();
        ImGui::Text("GSM");
        ImGui::Separator();
        const auto& c = d.gsm[0];
        ImGui::Text("CI:    %d", c.ci);
        ImGui::Text("ARFCN: %d", c.arfcn);
        ImGui::Text("LAC:   %d", c.lac);
        ImGui::Text("Dbm:   %d", c.dbm);
    }

    if (!d.nr.empty()) {
        ImGui::Spacing();
        ImGui::Text("NR (5G)");
        ImGui::Separator();
        const auto& c = d.nr[0];
        ImGui::Text("PCI:     %d", c.pci);
        ImGui::Text("SS-RSRP: %d dBm", c.ss_rsrp);
        ImGui::Text("SS-RSRQ: %d dB",  c.ss_rsrq);
        ImGui::Text("SS-SINR: %d dB",  c.ss_sinr);
    }

    ImGui::Spacing();
    ImGui::Text("Traffic");
    ImGui::Separator();
    ImGui::Text("TX: %lld bytes", d.total_tx);
    ImGui::Text("RX: %lld bytes", d.total_rx);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Load from file:");
    ImGui::InputText("##path", g_file_path, sizeof(g_file_path));
    if (ImGui::Button("Load JSON")) {
        ifstream f(g_file_path);
        if (!f.is_open()) {
            g_load_status = "Cannot open file";
        } else {
            lock_guard<mutex> lk(g_mtx);
            g_hist.clear();
            string line;
            float t = 0.f;
            int cnt = 0;
            while (getline(f, line)) {
                if (line.empty()) continue;
                TelemetryData pd = parse_telemetry(line);
                if (!valid_geo(pd.lat, pd.lon)) { cnt++; continue; }
                g_data = pd;
                float dbm   = pd.gsm.empty() ? 0.f : (float)pd.gsm[0].dbm;
                float nrsrp = pd.nr.empty()  ? 0.f : (float)pd.nr[0].ss_rsrp;
                g_hist.push(t, pd.lat, pd.lon, pd.alt, pd.lte, pd.gsm, dbm, nrsrp);
                t += 1.f;
                cnt++;
            }
            g_load_status = "Loaded " + to_string(cnt) + " records";
        }
    }
    if (!g_load_status.empty())
        ImGui::TextUnformatted(g_load_status.c_str());
}

void draw_plots_panel() {
    SignalHistory hist;
    {
        lock_guard<mutex> lk(g_mtx);
        hist = g_hist;
    }

    if (ImGui::BeginTabBar("plots_tabs")) {

        if (ImGui::BeginTabItem("Route")) {
            if (ImPlot::BeginPlot("Route", ImVec2(-1, -1))) {
                ImPlot::SetupAxes("Longitude", "Latitude");
                draw_route_items(hist, true);
                ImPlot::EndPlot();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("LTE")) {
            draw_lte_metric_plot("LTE RSRP", "RSRP [dBm]", hist.lte_by_pci, &PciHistory::rsrp);
            draw_lte_metric_plot("LTE RSRQ", "RSRQ [dB]", hist.lte_by_pci, &PciHistory::rsrq);
            draw_lte_metric_plot("LTE RSSI", "RSSI [dBm]", hist.lte_by_pci, &PciHistory::rssi);
            draw_lte_metric_plot("LTE SINR", "SINR [dB]", hist.lte_by_pci, &PciHistory::sinr);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("GSM")) {
            if (ImPlot::BeginPlot("GSM Dbm", ImVec2(-1, 300))) {
                ImPlot::SetupAxes("t", "Dbm");
                for (auto& [ci, ch] : hist.gsm_by_ci) {
                    string label = "CI " + to_string(ci);
                    ImPlot::PlotLine(label.c_str(),
                        ch.t.data(), ch.dbm.data(), (int)ch.t.size());
                }
                ImPlot::EndPlot();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("NR")) {
            if (ImPlot::BeginPlot("NR SS-RSRP", ImVec2(-1, 300))) {
                ImPlot::SetupAxes("t", "SS-RSRP [dBm]");
                if (!hist.nr_rsrp.empty())
                    ImPlot::PlotLine("SS-RSRP",
                        hist.t.data(), hist.nr_rsrp.data(), (int)hist.t.size());
                ImPlot::EndPlot();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Map")) {
        static size_t s_last_fit_count = 0;

        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.53f, 0.80f, 0.98f, 1.f));

        if (ImPlot::BeginPlot("##OSMMap", avail)) {
        ImPlot::SetupAxes("Longitude", "Latitude",
            ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        setup_route_limits_once(hist, s_last_fit_count);

        ImPlotRect lims = ImPlot::GetPlotLimits();

        int zoom = osm_auto_zoom(lims.X.Max - lims.X.Min, avail.x);
        zoom = max(1, min(17, zoom));

        osm_draw(lims.Y.Min, lims.Y.Max, lims.X.Min, lims.X.Max, zoom);
        draw_route_items(hist, true);
        draw_plot_tooltip(zoom);

        ImPlot::EndPlot();
        }

        ImPlot::PopStyleColor();
        ImGui::EndTabItem();
    }

        if (ImGui::BeginTabItem("Heat map")) {
        static int s_selected_pci = INT_MIN;
        static int s_selected_earfcn = 0;
        static int s_criterion_idx = 0;
        static float s_radius_m = 30.f;
        static bool s_all_pci = false;
        static size_t s_last_fit_count = 0;
        static GLuint s_heat_tex = 0;
        static HeatmapBounds s_heat_bounds{};
        static HeatmapBounds s_pending_bounds{};
        static string s_loaded_key;
        static string s_pending_key;
        static future<HeatmapImage> s_future;
        static bool s_running = false;

        if (!hist.lte_by_pci.empty()) {
            if (s_selected_pci == INT_MIN || hist.lte_by_pci.find(s_selected_pci) == hist.lte_by_pci.end())
                s_selected_pci = busiest_pci(hist);

            if (s_all_pci) {
                vector<int> earfcns = earfcn_list_all(hist);
                if (earfcns.empty()) {
                    s_selected_earfcn = 0;
                } else if (find(earfcns.begin(), earfcns.end(), s_selected_earfcn) == earfcns.end()) {
                    s_selected_earfcn = busiest_earfcn_all(hist);
                }
            } else if (auto selected_it = hist.lte_by_pci.find(s_selected_pci); selected_it != hist.lte_by_pci.end()) {
                vector<int> earfcns = earfcn_list(selected_it->second);
                if (earfcns.empty()) {
                    s_selected_earfcn = 0;
                } else if (find(earfcns.begin(), earfcns.end(), s_selected_earfcn) == earfcns.end()) {
                    s_selected_earfcn = busiest_earfcn(selected_it->second);
                }
            }
        }

        const char* criteria[] = { "RSRP", "RSRQ", "RSSI", "Altitude" };
        ImGui::PushItemWidth(220.f);
        ImGui::Combo("Criterion", &s_criterion_idx, criteria, 4);
        ImGui::SliderFloat("Radius m", &s_radius_m, 10.f, 40.f, "%.0f");
        ImGui::PopItemWidth();

        ImGui::BeginChild("PCI toolkit", ImVec2(180, 96), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        bool all_checked = s_all_pci;
        if (ImGui::Checkbox("All PCI", &all_checked)) {
            s_all_pci = all_checked;
            if (s_all_pci)
                s_selected_earfcn = busiest_earfcn_all(hist);
            s_loaded_key.clear();
        }
        ImGui::Separator();
        for (const auto& [pci, ph] : hist.lte_by_pci) {
            bool checked = s_all_pci || pci == s_selected_pci;
            string label = "PCI " + to_string(pci) + " (" + to_string(ph.rsrp.size()) + ")";
            if (ImGui::Checkbox(label.c_str(), &checked) && checked) {
                s_all_pci = false;
                s_selected_pci = pci;
                s_selected_earfcn = busiest_earfcn(ph);
                s_loaded_key.clear();
            }
        }
        ImGui::EndChild();

        ImGui::BeginGroup();
        auto selected_it = hist.lte_by_pci.find(s_selected_pci);
        if (s_all_pci || selected_it != hist.lte_by_pci.end()) {
            vector<int> earfcns = s_all_pci ? earfcn_list_all(hist) : earfcn_list(selected_it->second);
            string current = to_string(s_selected_earfcn);
            ImGui::PushItemWidth(220.f);
            if (ImGui::BeginCombo("EARFCN", current.c_str())) {
                for (int earfcn : earfcns) {
                    bool selected = earfcn == s_selected_earfcn;
                    string label = to_string(earfcn);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        s_selected_earfcn = earfcn;
                        s_loaded_key.clear();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            size_t sample_count = s_all_pci
                ? collect_heatmap_points_all(hist, s_selected_earfcn, (HeatmapCriterion)s_criterion_idx).size()
                : collect_heatmap_points(selected_it->second, s_selected_earfcn, (HeatmapCriterion)s_criterion_idx).size();
            ImGui::Text("Samples: %zu", sample_count);
        } else {
            ImGui::TextUnformatted("No LTE PCI data");
        }
        draw_heatmap_legend();
        ImGui::EndGroup();

        if (s_running && s_future.wait_for(chrono::milliseconds(0)) == future_status::ready) {
            HeatmapImage image = s_future.get();
            s_running = false;
            if (!image.rgba.empty()) {
                upload_rgba_texture(s_heat_tex, image);
                s_heat_bounds = s_pending_bounds;
                s_loaded_key = s_pending_key;
            }
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.53f, 0.80f, 0.98f, 1.f));

        if (ImPlot::BeginPlot("##HeatMap", avail)) {
        ImPlot::SetupAxes("Longitude", "Latitude",
            ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        setup_route_limits_once(hist, s_last_fit_count);

        ImPlotRect lims = ImPlot::GetPlotLimits();
        int zoom = osm_auto_zoom(lims.X.Max - lims.X.Min, avail.x);
        zoom = max(1, min(17, zoom));

        osm_draw(lims.Y.Min, lims.Y.Max, lims.X.Min, lims.X.Max, zoom);

        HeatmapBounds bounds{ lims.Y.Min, lims.Y.Max, lims.X.Min, lims.X.Max };
        HeatmapCriterion criterion = (HeatmapCriterion)s_criterion_idx;
        int heatmap_pci = s_all_pci ? -1 : s_selected_pci;
        string key = heatmap_request_key(criterion, heatmap_pci, s_selected_earfcn, s_radius_m, bounds);

        if (!s_running && key != s_loaded_key && (s_all_pci || selected_it != hist.lte_by_pci.end())) {
            vector<HeatmapPoint> points = s_all_pci
                ? collect_heatmap_points_all(hist, s_selected_earfcn, criterion)
                : collect_heatmap_points(selected_it->second, s_selected_earfcn, criterion);
            if (!points.empty()) {
                s_pending_bounds = bounds;
                s_pending_key = key;
                s_running = true;
                s_future = async(launch::async, [points = std::move(points), bounds, criterion, pci = heatmap_pci, earfcn = s_selected_earfcn, radius = s_radius_m] {
                    return heatmap_build_idw(points, bounds, criterion, pci, earfcn, radius, 320, 320);
                });
            }
        }

        if (s_heat_tex != 0 && !s_loaded_key.empty()) {
            ImPlot::PlotImage("##Heat",
                (ImTextureID)(intptr_t)s_heat_tex,
                ImPlotPoint{ s_heat_bounds.lon_min, s_heat_bounds.lat_min },
                ImPlotPoint{ s_heat_bounds.lon_max, s_heat_bounds.lat_max },
                ImVec2{ 0.f, 0.f },
                ImVec2{ 1.f, 1.f },
                ImVec4{ 1.f, 1.f, 1.f, 1.f });
        }

        draw_plot_tooltip(zoom);

        ImPlot::EndPlot();
        }

        ImPlot::PopStyleColor();
        ImGui::EndTabItem();
    }

        ImGui::EndTabBar();
    }
}

void run_gui() {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_Window* window = SDL_CreateWindow(
        "Phone Monitor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1400, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

#ifndef __APPLE__
    glewInit();
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 150");

    ImGui::StyleColorsDark();

    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                g_running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE)
                g_running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int win_w, win_h;
        SDL_GetWindowSize(window, &win_w, &win_h);

        float panel_w = 300.f;

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panel_w, (float)win_h), ImGuiCond_Always);
        ImGui::Begin("Data", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        draw_data_panel();
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(panel_w, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)win_w - panel_w, (float)win_h), ImGuiCond_Always);
        ImGui::Begin("Plots", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_AlwaysVerticalScrollbar);
        draw_plots_panel();
        ImGui::End();

        ImGui::Render();

        glViewport(0, 0, win_w, win_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
