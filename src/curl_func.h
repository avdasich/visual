#pragma once

#include <string>
#include <vector>

bool curl_download_to_memory(const std::string& url,
                             std::vector<unsigned char>& out,
                             std::string& error);
