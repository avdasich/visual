#include "gui.h"
#include "types.h"
#include "json_parser.h"

#include <SDL.h>

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

#include <fstream>
#include <chrono>
#include <mutex>

using namespace std;

int g_view_mode = 0;

void draw_data_panel() {
    TelemetryData d;
    {
        lock_guard<mutex> lk(g_mtx);
        d = g_data;
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
                g_data = pd;
                float dbm   = pd.gsm.empty() ? 0.f : (float)pd.gsm[0].dbm;
                float nrsrp = pd.nr.empty()  ? 0.f : (float)pd.nr[0].ss_rsrp;
                g_hist.push(t, pd.lat, pd.lon, pd.lte, pd.gsm, dbm, nrsrp);
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

                if (!hist.lon.empty()) {
                    ImPlot::PlotLine("Track",
                        hist.lon.data(), hist.lat.data(),
                        (int)hist.lon.size());

                    ImPlot::PlotScatter(
    "Points",
    hist.lon.data(),
    hist.lat.data(),
    (int)hist.lon.size()
);

double sx = hist.lon.front();
double sy = hist.lat.front();

ImPlot::PlotScatter(
    "Start",
    &sx,
    &sy,
    1
);

double ex = hist.lon.back();
double ey = hist.lat.back();

ImPlot::PlotScatter(
    "End",
    &ex,
    &ey,
    1
);
                }

                ImPlot::EndPlot();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("LTE")) {
            if (ImPlot::BeginPlot("LTE RSRP", ImVec2(-1, 200))) {
                ImPlot::SetupAxes("t", "RSRP [dBm]");
                for (auto& [pci, ph] : hist.lte_by_pci) {
                    string label = "PCI " + to_string(pci);
                    ImPlot::PlotLine(label.c_str(),
                        ph.t.data(), ph.rsrp.data(), (int)ph.t.size());
                }
                ImPlot::EndPlot();
            }
            if (ImPlot::BeginPlot("LTE RSRQ", ImVec2(-1, 200))) {
                ImPlot::SetupAxes("t", "RSRQ [dB]");
                for (auto& [pci, ph] : hist.lte_by_pci) {
                    string label = "PCI " + to_string(pci);
                    ImPlot::PlotLine(label.c_str(),
                        ph.t.data(), ph.rsrq.data(), (int)ph.t.size());
                }
                ImPlot::EndPlot();
            }
            if (ImPlot::BeginPlot("LTE RSSI", ImVec2(-1, 200))) {
                ImPlot::SetupAxes("t", "RSSI [dBm]");
                for (auto& [pci, ph] : hist.lte_by_pci) {
                    string label = "PCI " + to_string(pci);
                    ImPlot::PlotLine(label.c_str(),
                        ph.t.data(), ph.rssi.data(), (int)ph.t.size());
                }
                ImPlot::EndPlot();
            }
            if (ImPlot::BeginPlot("LTE SINR", ImVec2(-1, 200))) {
                ImPlot::SetupAxes("t", "SINR [dB]");
                for (auto& [pci, ph] : hist.lte_by_pci) {
                    string label = "PCI " + to_string(pci);
                    ImPlot::PlotLine(label.c_str(),
                        ph.t.data(), ph.sinr.data(), (int)ph.t.size());
                }
                ImPlot::EndPlot();
            }
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
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
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