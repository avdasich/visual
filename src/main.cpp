#include "types.h"
#include "server.h"
#include "gui.h"

#include <iostream>
#include <thread>

atomic<bool>  g_running{true};
TelemetryData g_data;
mutex         g_mtx;
SignalHistory  g_hist;
char          g_file_path[256] = "data.json";
string        g_load_status    = "";

int main() {
    cout << "Phone Monitor Starting\n";

    thread server_thread(run_server);

    run_gui();

    server_thread.join();

    cout << "Phone Monitor Stopped\n";
    return 0;
}
