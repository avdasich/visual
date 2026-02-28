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

struct Location {
    float latitude  = 0.0f;
    float longitude = 0.0f;
    float altitude  = 0.0f;
    bool  updated   = false;
};

std::mutex g_mutex;

float parseField(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return 0.0f;
    pos += key.size() + 3;
    return std::stof(json.substr(pos));
}

void run_server(Location* loc) {
    zmq::context_t ctx;
    zmq::socket_t  sock(ctx, zmq::socket_type::rep);
    sock.bind("tcp://*:2222");
    std::cout << "[SERVER] Started on port 2222\n";

    while (true) {
        zmq::message_t msg;
        auto res = sock.recv(msg, zmq::recv_flags::none);

        std::string data(static_cast<char*>(msg.data()), msg.size());
        std::cout << "[SERVER] Got: " << data << "\n";

        float lat = parseField(data, "latitude");
        float lon = parseField(data, "longitude");
        float alt = parseField(data, "altitude");

        std::ofstream file("location.json", std::ios::app);
        file << data << "\n";
        file.close();

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            loc->latitude  = lat;
            loc->longitude = lon;
            loc->altitude  = alt;
            loc->updated   = true;
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
        "Location Viewer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        600, 400,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        std::cout << "GL Context error: " << SDL_GetError() << "\n";
        return;
    }
    SDL_GL_MakeCurrent(window, gl_context);

    std::cout << "GL version: " << glGetString(GL_VERSION) << "\n";
    std::cout << "GLSL version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 410 core");

    ImGui::StyleColorsDark();

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        float lat, lon, alt;
        bool updated;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            lat     = loc->latitude;
            lon     = loc->longitude;
            alt     = loc->altitude;
            updated = loc->updated;
        }

        ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_Always);
        ImGui::Begin("Location Info");

        if (updated) {
            ImGui::Text("Latitude:  %.6f", lat);
            ImGui::Text("Longitude: %.6f", lon);
            ImGui::Text("Altitude:  %.2f m", alt);
        } else {
            ImGui::Text("Waiting for data from Android...");
        }

        ImGui::End();

        ImGui::Render();
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

int main(int argc, char* argv[]) {
    static Location locationInfo;

    std::thread server_thread(run_server, &locationInfo);
    run_gui(&locationInfo);
    server_thread.join();

    return 0;
}