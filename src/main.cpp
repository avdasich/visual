#include "types.h"
#include "database.h"
#include "server.h"
#include "gui.h"

#include <iostream>
#include <thread>

using namespace std;

atomic<bool>  g_running{true};
TelemetryData g_data;
mutex         g_mtx;
SignalHistory g_hist;

char   g_file_path[256] = "data.json";
string g_load_status    = "";

int main() {

    cout << "Phone Monitor Starting\n";

    init_database();

    thread server_thread(run_server);

    run_gui();

    g_running = false;

    server_thread.join();

    if (db_conn)
        PQfinish(db_conn);

    return 0;
}