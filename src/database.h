#pragma once

#include "types.h"
#include <libpq-fe.h>

extern PGconn* db_conn;

void init_database();
void insert_to_db(const string& raw_json, const TelemetryData& d);