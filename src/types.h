#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

using namespace std;

struct CellLte {
    int band = 0, ci = 0, earfcn = 0, pci = 0, tac = 0;
    int asu = 0, cqi = 0, rsrp = 0, rsrq = 0, rssi = 0, rssnr = 0, ta = 0;
    string mcc, mnc;
};

struct CellGsm {
    int ci = 0, bsic = 0, arfcn = 0, lac = 0, dbm = 0, rssi = 0, ta = 0;
    string mcc, mnc;
};

struct CellNr {
    int band = 0, pci = 0, nrarfcn = 0, tac = 0;
    int ss_rsrp = 0, ss_rsrq = 0, ss_sinr = 0, ta = 0;
    long long nci = 0;
    string mcc, mnc;
};

struct TelemetryData {
    double lat = 0, lon = 0;
    float  alt = 0, acc = 0;
    long long time = 0, total_tx = 0, total_rx = 0;
    vector<CellLte> lte;
    vector<CellGsm> gsm;
    vector<CellNr>  nr;
};

static const int MAX_PTS = 10000;

struct PciHistory {
    vector<float> t;
    vector<float> rsrp, rsrq, rssi, sinr;

    void push(float time, float _rsrp, float _rsrq, float _rssi, float _sinr) {
        auto add = [](vector<float>& v, float val) {
            v.push_back(val);
            if ((int)v.size() > MAX_PTS) v.erase(v.begin());
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
            if ((int)v.size() > MAX_PTS) v.erase(v.begin());
        };
        add(t,   time);
        add(dbm, _dbm);
    }
};

struct SignalHistory {
    vector<float>  t;
    vector<double> lat, lon;
    vector<float>  gsm_dbm;
    vector<float>  nr_rsrp;
    map<int, PciHistory> lte_by_pci;
    map<int, CiHistory>  gsm_by_ci;

    void push(float time, double _lat, double _lon,
              const vector<CellLte>& lte_cells,
              const vector<CellGsm>& gsm_cells,
              float dbm, float nrsrp)
    {
        auto addf = [](vector<float>& v, float val) {
            v.push_back(val);
            if ((int)v.size() > MAX_PTS) v.erase(v.begin());
        };
        auto addd = [](vector<double>& v, double val) {
            v.push_back(val);
            if ((int)v.size() > MAX_PTS) v.erase(v.begin());
        };

        addf(t, time);
        addd(lat, _lat);
        addd(lon, _lon);
        addf(gsm_dbm, dbm);
        addf(nr_rsrp, nrsrp);

        for (auto& c : lte_cells)
            lte_by_pci[c.pci].push(time,
                (float)c.rsrp, (float)c.rsrq,
                (float)c.rssi, (float)c.rssnr);

        for (auto& c : gsm_cells)
            gsm_by_ci[c.ci].push(time, (float)c.dbm);
    }

    void clear() {
        t.clear(); lat.clear(); lon.clear();
        gsm_dbm.clear(); nr_rsrp.clear();
        lte_by_pci.clear();
        gsm_by_ci.clear();
    }
};

extern atomic<bool>   g_running;
extern TelemetryData  g_data;
extern mutex          g_mtx;
extern SignalHistory  g_hist;
extern char           g_file_path[256];
extern string         g_load_status;
