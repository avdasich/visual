#pragma once
#include "types.h"
#include <string>
#include <vector>

string fval(const string& json, const string& key);
string block(const string& s, size_t from, char open, char close);
vector<string> split_arr(const string& arr);

float     fflt(const string& j, const string& k);
int       fint(const string& j, const string& k);
double    fdbl(const string& j, const string& k);
long long flng(const string& j, const string& k);

TelemetryData parse_telemetry(const string& raw);
