#include "curl_func.h"

#include <curl/curl.h>

static size_t curl_write_cb(void* data, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto& blob = *static_cast<std::vector<unsigned char>*>(userp);
    auto* ptr  = static_cast<unsigned char*>(data);
    blob.insert(blob.end(), ptr, ptr + total);
    return total;
}

bool curl_download_to_memory(const std::string& url,
                             std::vector<unsigned char>& out,
                             std::string& error) {
    out.clear();
    error.clear();
    out.reserve(32 * 1024);

    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "PhoneMonitor/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,     1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &out);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        error = curl_easy_strerror(rc);
        out.clear();
        return false;
    }
    if (out.empty()) {
        error = "empty response";
        return false;
    }

    return true;
}
