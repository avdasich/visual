
#include "database.h"

#include <iostream>

using namespace std;

PGconn* db_conn = nullptr;

static void db_create_table() {

    if (!db_conn)
        return;

    const char* sql =
        "CREATE TABLE IF NOT EXISTS telemetry ("
        "  id        SERIAL PRIMARY KEY,"
        "  received  TIMESTAMP DEFAULT NOW(),"

        "  lat       DOUBLE PRECISION,"
        "  lon       DOUBLE PRECISION,"
        "  alt       REAL,"
        "  accuracy  REAL,"
        "  gps_time  BIGINT,"

        "  total_tx  BIGINT,"
        "  total_rx  BIGINT,"

        "  gsm_ci    INTEGER,"
        "  gsm_dbm   INTEGER,"
        "  gsm_mcc   TEXT,"
        "  gsm_mnc   TEXT,"

        "  lte_pci   INTEGER,"
        "  lte_rsrp  INTEGER,"
        "  lte_rsrq  INTEGER,"
        "  lte_rssi  INTEGER,"
        "  lte_sinr  INTEGER,"
        "  lte_band  INTEGER,"

        "  nr_pci    INTEGER,"
        "  nr_rsrp   INTEGER,"
        "  nr_sinr   INTEGER,"

        "  raw_data  JSONB"
        ");";

    PGresult* res = PQexec(db_conn, sql);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {

        cerr << "[DB] CREATE TABLE error:\n"
             << PQresultErrorMessage(res)
             << "\n";

    } else {

        cout << "[DB] Table telemetry ready\n";
    }

    PQclear(res);
}

void init_database() {

    db_conn = PQconnectdb(
        "dbname=phone_monitor "
    );

    if (PQstatus(db_conn) != CONNECTION_OK) {

        cerr << "[DB] Connection failed:\n"
             << PQerrorMessage(db_conn)
             << "\n";

        PQfinish(db_conn);

        db_conn = nullptr;

        return;
    }

    cout << "[DB] Connected to PostgreSQL\n";

    db_create_table();
}

void insert_to_db(
    const string& raw_json,
    const TelemetryData& d
) {

    if (!db_conn)
        return;

    if (PQstatus(db_conn) != CONNECTION_OK) {

        cerr << "[DB] Connection lost. Reconnecting...\n";

        PQreset(db_conn);

        if (PQstatus(db_conn) != CONNECTION_OK) {

            cerr << "[DB] Reconnect failed\n";

            return;
        }
    }

    int lte_pci   = 0;
    int lte_rsrp  = 0;
    int lte_rsrq  = 0;
    int lte_rssi  = 0;
    int lte_sinr  = 0;
    int lte_band  = 0;

    if (!d.lte.empty()) {

        const auto& c = d.lte[0];

        lte_pci  = c.pci;
        lte_rsrp = c.rsrp;
        lte_rsrq = c.rsrq;
        lte_rssi = c.rssi;
        lte_sinr = c.rssnr;
        lte_band = c.band;
    }

    int gsm_ci   = 0;
    int gsm_dbm  = 0;

    string gsm_mcc;
    string gsm_mnc;

    if (!d.gsm.empty()) {

        const auto& c = d.gsm[0];

        gsm_ci  = c.ci;
        gsm_dbm = c.dbm;

        gsm_mcc = c.mcc;
        gsm_mnc = c.mnc;
    }

    int nr_pci  = 0;
    int nr_rsrp = 0;
    int nr_sinr = 0;

    if (!d.nr.empty()) {

        const auto& c = d.nr[0];

        nr_pci  = c.pci;
        nr_rsrp = c.ss_rsrp;
        nr_sinr = c.ss_sinr;
    }

    string s_lat   = to_string(d.lat);
    string s_lon   = to_string(d.lon);
    string s_alt   = to_string(d.alt);
    string s_acc   = to_string(d.acc);

    string s_time  = to_string(d.time);

    string s_tx    = to_string(d.total_tx);
    string s_rx    = to_string(d.total_rx);

    string s_gci   = to_string(gsm_ci);
    string s_gdbm  = to_string(gsm_dbm);

    string s_lpci  = to_string(lte_pci);
    string s_lrsrp = to_string(lte_rsrp);
    string s_lrsrq = to_string(lte_rsrq);
    string s_lrssi = to_string(lte_rssi);
    string s_lsinr = to_string(lte_sinr);
    string s_lband = to_string(lte_band);

    string s_npci  = to_string(nr_pci);
    string s_nrsrp = to_string(nr_rsrp);
    string s_nsinr = to_string(nr_sinr);

    const char* values[19] = {

        s_lat.c_str(),
        s_lon.c_str(),
        s_alt.c_str(),
        s_acc.c_str(),
        s_time.c_str(),

        s_tx.c_str(),
        s_rx.c_str(),

        s_gci.c_str(),
        s_gdbm.c_str(),
        gsm_mcc.c_str(),
        gsm_mnc.c_str(),

        s_lpci.c_str(),
        s_lrsrp.c_str(),
        s_lrsrq.c_str(),
        s_lrssi.c_str(),
        s_lsinr.c_str(),
        s_lband.c_str(),

        s_npci.c_str(),
        s_nrsrp.c_str()
    };

    string sql =
        "INSERT INTO telemetry ("

        "lat, lon, alt, accuracy, gps_time, "

        "total_tx, total_rx, "

        "gsm_ci, gsm_dbm, gsm_mcc, gsm_mnc, "

        "lte_pci, lte_rsrp, lte_rsrq, "
        "lte_rssi, lte_sinr, lte_band, "

        "nr_pci, nr_rsrp, "

        "raw_data"

        ") VALUES ("

        "$1,$2,$3,$4,$5,"
        "$6,$7,"
        "$8,$9,$10,$11,"
        "$12,$13,$14,$15,$16,$17,"
        "$18,$19,"
        "$20"

        ");";

    const char* params[20] = {

        values[0],
        values[1],
        values[2],
        values[3],
        values[4],

        values[5],
        values[6],

        values[7],
        values[8],
        values[9],
        values[10],

        values[11],
        values[12],
        values[13],
        values[14],
        values[15],
        values[16],

        values[17],
        values[18],

        raw_json.c_str()
    };

    PGresult* res = PQexecParams(
        db_conn,
        sql.c_str(),
        20,
        nullptr,
        params,
        nullptr,
        nullptr,
        0
    );

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {

        cerr << "[DB] INSERT error:\n"
             << PQresultErrorMessage(res)
             << "\n";
    }

    PQclear(res);
}