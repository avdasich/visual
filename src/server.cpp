#include "server.h"
#include "types.h"
#include "json_parser.h"
#include "database.h"

#include <zmq.hpp>
#include <fstream>
#include <iostream>
#include <chrono>

using namespace std;

void run_server() {
    zmq::context_t ctx;
    zmq::socket_t  sock(ctx, zmq::socket_type::rep);
    sock.set(zmq::sockopt::rcvtimeo, 500);
    sock.bind("tcp://*:5555");
    cout << "[SERVER] ZMQ server started on port 5555\n";

    auto t0 = chrono::steady_clock::now();

    while (g_running) {
        zmq::message_t msg;
        auto result = sock.recv(msg, zmq::recv_flags::none);
        if (!result) continue;

        string raw(static_cast<char*>(msg.data()), msg.size());

        {
            ofstream out("data.json", ios::app);
            if (out.is_open()) out << raw << "\n";
        }

        TelemetryData d = parse_telemetry(raw);
        insert_to_db(raw, d);

        float now = chrono::duration<float>(chrono::steady_clock::now() - t0).count();
        {
            lock_guard<mutex> lk(g_mtx);
            g_data = d;
            float dbm   = d.gsm.empty() ? 0.f : (float)d.gsm[0].dbm;
            float nrsrp = d.nr.empty()  ? 0.f : (float)d.nr[0].ss_rsrp;
           if (d.lat != 0.0 && d.lon != 0.0) {

            g_hist.push(
                now,
                d.lat,
                d.lon,
                d.lte,
                d.gsm,
                dbm,
                nrsrp
            );
};
        }

        sock.send(zmq::buffer("OK"), zmq::send_flags::none);

        cout << "[SERVER] lat=" << d.lat
             << " lon=" << d.lon
             << " lte=" << d.lte.size()
             << " gsm=" << d.gsm.size()
             << " nr="  << d.nr.size()  << "\n";
    }
    cout << "[SERVER] Stopped\n";
}
