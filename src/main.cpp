#include <iostream>
#include <thread>
#include <mutex>
#include <fstream>
#include <string>

#include <zmq.hpp>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <OpenGL/gl3.h>

struct CellInfo {
    int pci = 0;
    int tac = 0;
    int earfcn = 0;
    int rsrp = 0;
    int rsrq = 0;
    int rssi = 0;
    int sinr = 0;
};

struct Location {
    float latitude = 0.0f;
    float longitude = 0.0f;
    float altitude = 0.0f;
    float accuracy = 0.0f;

    std::string currentTime;

    CellInfo lte;

    bool updated = false;
};

std::mutex g_mutex;

float parseField(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return 0.0f;

    pos += key.size() + 3;

    return std::stof(json.substr(pos));
}

int parseIntField(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return 0;

    pos += key.size() + 3;

    return std::stoi(json.substr(pos));
}

std::string parseStringField(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\":\"");
    if (pos == std::string::npos) return "";

    pos += key.size() + 4;

    auto end = json.find("\"", pos);
    if (end == std::string::npos) return "";

    return json.substr(pos, end - pos);
}

void run_server(Location* loc) {
    zmq::context_t ctx;
    zmq::socket_t sock(ctx, zmq::socket_type::rep);

    sock.bind("tcp://*:2222");

    std::cout << "[SERVER] Started on port 2222";

    while (true) {
        zmq::message_t msg;
        auto res = sock.recv(msg, zmq::recv_flags::none);

        std::string data(static_cast<char*>(msg.data()), msg.size());

        std::cout << "[SERVER] Got: " << data << "\n";

        float lat = parseField(data, "latitude");
        float lon = parseField(data, "longitude");
        float alt = parseField(data, "altitude");
        float acc = parseField(data, "accuracy");

        int pci = parseIntField(data, "pci");
        int tac = parseIntField(data, "tac");
        int earfcn = parseIntField(data, "earfcn");

        int rsrp = parseIntField(data, "rsrp");
        int rsrq = parseIntField(data, "rsrq");
        int rssi = parseIntField(data, "rssi");
        int sinr = parseIntField(data, "sinr");

        std::string currentTime = parseStringField(data, "time");

        std::ofstream file("location.json", std::ios::app);
        file << data << "\n";
        file.close();

        {
            std::lock_guard<std::mutex> lock(g_mutex);

            loc->latitude = lat;
            loc->longitude = lon;
            loc->altitude = alt;
            loc->accuracy = acc;

            loc->currentTime = currentTime;

            loc->lte.pci = pci;
            loc->lte.tac = tac;
            loc->lte.earfcn = earfcn;

            loc->lte.rsrp = rsrp;
            loc->lte.rsrq = rsrq;
            loc->lte.rssi = rssi;
            loc->lte.sinr = sinr;

            loc->updated = true;
        }

        sock.send(zmq::buffer(std::string("OK")), zmq::send_flags::none);
    }
}

void run_gui(Location* loc) {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_Window* window = SDL_CreateWindow(
        "Phone Monitor",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        900,
        600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 150");

    bool running = true;

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT)
                running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        float lat;
        float lon;
        float alt;
        float acc;

        bool updated;

        CellInfo cell;
        std::string currentTime;

        {
            std::lock_guard<std::mutex> lock(g_mutex);

            lat = loc->latitude;
            lon = loc->longitude;
            alt = loc->altitude;
            acc = loc->accuracy;

            currentTime = loc->currentTime;

            cell = loc->lte;

            updated = loc->updated;
        }

        ImGui::Begin("Location Monitor");

        if (updated) {
            ImGui::Text("Latitude: %.6f", lat);
            ImGui::Text("Longitude: %.6f", lon);
            ImGui::Text("Altitude: %.2f", alt);
            ImGui::Text("Accuracy: %.2f", acc);
            ImGui::Text("Time: %s", currentTime.c_str());

            ImGui::Separator();

            ImGui::Text("LTE INFO");
            ImGui::Text("PCI: %d", cell.pci);
            ImGui::Text("TAC: %d", cell.tac);
            ImGui::Text("EARFCN: %d", cell.earfcn);

            ImGui::Separator();

            ImGui::Text("SIGNAL");
            ImGui::Text("RSRP: %d", cell.rsrp);
            ImGui::Text("RSRQ: %d", cell.rsrq);
            ImGui::Text("RSSI: %d", cell.rssi);
            ImGui::Text("SINR: %d", cell.sinr);
        }
        else {
            ImGui::Text("Waiting for data from Android...");
        }

        ImGui::End();

        ImGui::Render();

        glViewport(0, 0, 900, 600);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int main() {
    static Location locationInfo;

    std::thread server_thread(run_server, &locationInfo);

    run_gui(&locationInfo);

    server_thread.join();

    return 0;
}