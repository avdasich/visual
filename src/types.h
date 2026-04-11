#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

using namespace std;

struct CellLte {
    int band = 0;
    int ci = 0;
    int earfcn = 0;
    int pci = 0;
    int tac = 0;

    int asu = 0;
    int cqi = 0;

    int rsrp = 0;
    int rsrq = 0;
    int rssi = 0;
    int rssnr = 0;

    int ta = 0;

    string mcc;
    string mnc;
};

struct CellGsm {
    int ci = 0;
    int bsic = 0;
    int arfcn = 0;
    int lac = 0;

    int dbm = 0;
    int rssi = 0;
    int ta = 0;

    string mcc;
    string mnc;
};

struct CellNr {
    int band = 0;
    int pci = 0;
    int nrarfcn = 0;
    int tac = 0;

    int ss_rsrp = 0;
    int ss_rsrq = 0;
    int ss_sinr = 0;

    int ta = 0;

    long long nci = 0;

    string mcc;
    string mnc;
};

struct TelemetryData {
    double lat = 0.0;
    double lon = 0.0;

    float alt = 0.f;
    float acc = 0.f;

    long long time = 0;

    long long total_tx = 0;
    long long total_rx = 0;

    vector<CellLte> lte;
    vector<CellGsm> gsm;
    vector<CellNr>  nr;
};

static const int MAX_PTS = 10000;

struct PciHistory {
    vector<float> t;

    vector<float> rsrp;
    vector<float> rsrq;
    vector<float> rssi;
    vector<float> sinr;

    void push(
        float time,
        float _rsrp,
        float _rsrq,
        float _rssi,
        float _sinr
    ) {
        auto add = [](vector<float>& v, float val) {
            v.push_back(val);

            if ((int)v.size() > MAX_PTS)
                v.erase(v.begin());
        };

        add(t,    time);
        add(rsrp, _rsrp);
        add(rsrq, _rsrq);
        add(rssi, _rssi);
        add(sinr, _sinr);
    }
};

struct CiHistory {
    vector<float> t;
    vector<float> dbm;

    void push(float time, float _dbm) {
        auto add = [](vector<float>& v, float val) {
            v.push_back(val);

            if ((int)v.size() > MAX_PTS)
                v.erase(v.begin());
        };

        add(t, time);
        add(dbm, _dbm);
    }
};

struct SignalHistory {

    vector<float>  t;

    vector<double> lat;
    vector<double> lon;

    vector<float> gsm_dbm;
    vector<float> nr_rsrp;

    map<int, PciHistory> lte_by_pci;

    map<int, CiHistory> gsm_by_ci;

    void push(
        float time,
        double _lat,
        double _lon,
        const vector<CellLte>& lte_cells,
        const vector<CellGsm>& gsm_cells,
        float dbm,
        float nrsrp
    ) {

        auto addf = [](vector<float>& v, float val) {
            v.push_back(val);

            if ((int)v.size() > MAX_PTS)
                v.erase(v.begin());
        };

        auto addd = [](vector<double>& v, double val) {
            v.push_back(val);

            if ((int)v.size() > MAX_PTS)
                v.erase(v.begin());
        };

        addf(t, time);

        addd(lat, _lat);
        addd(lon, _lon);

        addf(gsm_dbm, dbm);
        addf(nr_rsrp, nrsrp);

        
        for (auto& c : lte_cells) {

            lte_by_pci[c.pci].push(
                time,
                (float)c.rsrp,
                (float)c.rsrq,
                (float)c.rssi,
                (float)c.rssnr
            );
        }

        for (auto& c : gsm_cells) {

            gsm_by_ci[c.ci].push(
                time,
                (float)c.dbm
            );
        }
    }

    void clear() {

        t.clear();

        lat.clear();
        lon.clear();

        gsm_dbm.clear();
        nr_rsrp.clear();

        lte_by_pci.clear();
        gsm_by_ci.clear();
    }
};

extern atomic<bool>  g_running;

extern TelemetryData g_data;

extern mutex         g_mtx;

extern SignalHistory g_hist;

extern char          g_file_path[256];

extern string        g_load_status;