#include "json_parser.h"
#include <iostream>

string fval(const string& json, const string& key) {
    string s = "\"" + key + "\":";
    size_t p = json.find(s);
    if (p == string::npos) return "0";
    size_t vs = p + s.size();
    if (vs < json.size() && json[vs] == '"') vs++;
    size_t ve = json.find_first_of("\",}]", vs);
    if (ve == string::npos) return "0";
    string r = json.substr(vs, ve - vs);
    for (auto& c : r) if (c == ',') c = '.';
    return r;
}

string block(const string& s, size_t from, char open, char close) {
    size_t start = s.find(open, from);
    if (start == string::npos) return "";
    int depth = 0;
    for (size_t i = start; i < s.size(); i++) {
        if (s[i] == open)  depth++;
        if (s[i] == close) if (--depth == 0) return s.substr(start, i - start + 1);
    }
    return "";
}

vector<string> split_arr(const string& arr) {
    vector<string> res;
    size_t i = 1;
    while (i < arr.size()) {
        string b = block(arr, i, '{', '}');
        if (b.empty()) break;
        res.push_back(b);
        size_t pos = arr.find(b, i);
        if (pos == string::npos) break;
        i = pos + b.size();
    }
    return res;
}

float     fflt(const string& j, const string& k) { try { return stof(fval(j,k));  } catch(...){ return 0.f;  } }
int       fint(const string& j, const string& k) { try { return stoi(fval(j,k));  } catch(...){ return 0;    } }
double    fdbl(const string& j, const string& k) { try { return stod(fval(j,k));  } catch(...){ return 0.0;  } }
long long flng(const string& j, const string& k) { try { return stoll(fval(j,k)); } catch(...){ return 0LL;  } }

TelemetryData parse_telemetry(const string& raw) {
    TelemetryData d;

    if (raw.find("\"telephony\"") != string::npos) {

        size_t lp = raw.find("\"location\":");
        if (lp != string::npos) {
            auto lb = block(raw, lp, '{', '}');
            d.lat  = fdbl(lb, "latitude");
            d.lon  = fdbl(lb, "longitude");
            d.alt  = fflt(lb, "altitude");
            d.acc  = fflt(lb, "accuracy");
            d.time = flng(lb, "time");
        }

        size_t nu_p = raw.find("\"networkUsage\":");
        if (nu_p != string::npos) {
            auto nu = block(raw, nu_p, '{', '}');
            d.total_rx = flng(nu, "totalBytesReceived");
            d.total_tx = flng(nu, "totalBytesSent");
        }

        size_t tel_p = raw.find("\"telephony\":");
        if (tel_p != string::npos) {
            auto arr = block(raw, tel_p, '[', ']');
            for (auto& entry : split_arr(arr)) {
                string type_val = fval(entry, "type");

                if (type_val == "LTE") {
                    CellLte x{};
                    size_t id_p = entry.find("\"CellIdentityLte\":");
                    if (id_p != string::npos) {
                        auto id_b = block(entry, id_p, '{', '}');
                        x.pci    = fint(id_b, "PCI");
                        x.tac    = fint(id_b, "TAC");
                        x.earfcn = fint(id_b, "EARFCN");
                        x.ci     = fint(id_b, "CellIdentity");
                        x.mcc    = fval(id_b, "MCC");
                        x.mnc    = fval(id_b, "MNC");
                        try { x.band = stoi(fval(id_b, "Band")); } catch(...) {}
                    }
                    size_t sig_p = entry.find("\"CellSignalStrengthLte\":");
                    if (sig_p != string::npos) {
                        auto sig_b = block(entry, sig_p, '{', '}');
                        x.rsrp  = fint(sig_b, "RSRP");
                        x.rsrq  = fint(sig_b, "RSRQ");
                        x.rssi  = fint(sig_b, "RSSI");
                        x.rssnr = fint(sig_b, "RSSNR");
                        x.asu   = fint(sig_b, "ASU_Level");
                        x.cqi   = fint(sig_b, "CQI");
                        x.ta    = fint(sig_b, "Timing_Advance");
                    }
                    d.lte.push_back(x);

                } else if (type_val == "GSM") {
                    CellGsm x{};
                    size_t id_p = entry.find("\"CellIdentityGSM\":");
                    if (id_p != string::npos) {
                        auto id_b = block(entry, id_p, '{', '}');
                        x.ci    = fint(id_b, "CellIdentity");
                        x.bsic  = fint(id_b, "BSIC");
                        x.arfcn = fint(id_b, "ARFCN");
                        x.lac   = fint(id_b, "LAC");
                        x.mcc   = fval(id_b, "MCC");
                        x.mnc   = fval(id_b, "MNC");
                    }
                    size_t sig_p = entry.find("\"CellSignalStrengthGsm\":");
                    if (sig_p != string::npos) {
                        auto sig_b = block(entry, sig_p, '{', '}');
                        x.dbm  = fint(sig_b, "Dbm");
                        x.rssi = fint(sig_b, "RSSI");
                        x.ta   = fint(sig_b, "Timing_Advance");
                    }
                    d.gsm.push_back(x);

                } else if (type_val == "NR") {
                    CellNr x{};
                    size_t id_p = entry.find("\"CellIdentityNr\":");
                    if (id_p != string::npos) {
                        auto id_b = block(entry, id_p, '{', '}');
                        x.pci     = fint(id_b, "PCI");
                        x.tac     = fint(id_b, "TAC");
                        x.nrarfcn = fint(id_b, "NrArfcn");
                        x.nci     = flng(id_b, "Nci");
                        x.mcc     = fval(id_b, "MCC");
                        x.mnc     = fval(id_b, "MNC");
                        try { x.band = stoi(fval(id_b, "Band")); } catch(...) {}
                    }
                    size_t sig_p = entry.find("\"CellSignalStrengthNr\":");
                    if (sig_p != string::npos) {
                        auto sig_b = block(entry, sig_p, '{', '}');
                        x.ss_rsrp = fint(sig_b, "SsRsrp");
                        x.ss_rsrq = fint(sig_b, "SsRsrq");
                        x.ss_sinr = fint(sig_b, "SsSinr");
                    }
                    d.nr.push_back(x);
                }
            }
        }
        return d;
    }

    d.lat  = fdbl(raw, "lat");
    d.lon  = fdbl(raw, "lon");
    d.alt  = fflt(raw, "alt");
    d.acc  = fflt(raw, "acc");
    d.time = flng(raw, "time");

    size_t lte_p = raw.find("\"lte\":");
    if (lte_p != string::npos) {
        for (auto& c : split_arr(block(raw, lte_p, '[', ']'))) {
            CellLte x{};
            x.band   = fint(c, "band");
            x.ci     = fint(c, "ci");
            x.earfcn = fint(c, "earfcn");
            x.pci    = fint(c, "pci");
            x.tac    = fint(c, "tac");
            x.rsrp   = fint(c, "rsrp");
            x.rsrq   = fint(c, "rsrq");
            x.rssi   = fint(c, "rssi");
            x.rssnr  = fint(c, "rssnr");
            x.ta     = fint(c, "ta");
            x.mcc    = fval(c, "mcc");
            x.mnc    = fval(c, "mnc");
            d.lte.push_back(x);
        }
    }

    size_t gsm_p = raw.find("\"gsm\":");
    if (gsm_p != string::npos) {
        for (auto& c : split_arr(block(raw, gsm_p, '[', ']'))) {
            CellGsm x{};
            x.ci    = fint(c, "ci");
            x.bsic  = fint(c, "bsic");
            x.arfcn = fint(c, "arfcn");
            x.lac   = fint(c, "lac");
            x.dbm   = fint(c, "dbm");
            x.ta    = fint(c, "ta");
            x.mcc   = fval(c, "mcc");
            x.mnc   = fval(c, "mnc");
            d.gsm.push_back(x);
        }
    }

    size_t nr_p = raw.find("\"nr\":");
    if (nr_p != string::npos) {
        for (auto& c : split_arr(block(raw, nr_p, '[', ']'))) {
            CellNr x{};
            x.nci     = flng(c, "nci");
            x.pci     = fint(c, "pci");
            x.band    = fint(c, "band");
            x.ss_rsrp = fint(c, "ss_rsrp");
            x.ss_rsrq = fint(c, "ss_rsrq");
            x.ss_sinr = fint(c, "ss_sinr");
            x.mcc     = fval(c, "mcc");
            x.mnc     = fval(c, "mnc");
            d.nr.push_back(x);
        }
    }

    size_t tp = raw.find("\"traffic\":");
    if (tp != string::npos) {
        auto tb = block(raw, tp, '{', '}');
        d.total_tx = flng(tb, "total_tx");
        d.total_rx = flng(tb, "total_rx");
    }
    return d;
}
