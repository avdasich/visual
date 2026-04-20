#include "osm_map.h"
#include "curl_func.h"

#include <cmath>
#include <sstream>
#include <iostream>
#include <numbers>
#include <algorithm>
#include <chrono>
#include <filesystem>

#include <curl/curl.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "implot.h"

using namespace std;
namespace fs = std::filesystem;

static constexpr double K_PI  = numbers::pi_v<double>;
static constexpr double K_PI2 = K_PI * 2.0;
static constexpr double K_RAD = K_PI / 180.0;
static constexpr double K_DEG = 180.0 / K_PI;

double osm_lon2x(double lon, int z) {
    return (lon + 180.0) / 360.0 * (double)(1 << z);
}

double osm_lat2y(double lat, int z) {
    return (1.0 - asinh(tan(lat * K_RAD)) / K_PI) / 2.0 * (double)(1 << z);
}

double osm_x2lon(double x, int z) {
    return x / (double)(1 << z) * 360.0 - 180.0;
}

double osm_y2lat(double y, int z) {
    double n = K_PI - K_PI2 * y / (double)(1 << z);
    return K_DEG * atan(0.5 * (exp(n) - exp(-n)));
}

int osm_auto_zoom(double lon_range, float plot_px_w) {
    if (lon_range <= 0.0) return 2;
    double tiles_needed = (double)plot_px_w / 256.0;
    double z = log2(tiles_needed * 360.0 / lon_range);
    return max(0, min(17, (int)floor(z)));
}

string get_cache_path(int z, int x, int y) {
    fs::path dir = fs::path(to_string(z)) / to_string(x);
    fs::create_directories(dir);
    return (dir / (to_string(y) + ".png")).string();
}

OsmTile::~OsmTile() {
    if (pixels) {
        stbi_image_free(pixels);
        pixels = nullptr;
    }
    if (tex_id) {
        glDeleteTextures(1, &tex_id);
        tex_id = 0;
    }
}

static void fetch_tile(OsmTile* tile) {
    const char* subs[] = {"a", "b", "c"};
    const char* sub = subs[(tile->key.x + tile->key.y) % 3];

    ostringstream url;
    url << "https://" << sub << ".tile.openstreetmap.org/"
        << tile->key.z << '/'
        << tile->key.x << '/'
        << tile->key.y << ".png";

    vector<unsigned char> raw;
    string error;
    if (!curl_download_to_memory(url.str(), raw, error)) {
        cerr << "[OSM] curl failed z=" << tile->key.z
             << " x=" << tile->key.x
             << " y=" << tile->key.y
             << " : " << error << "\n";
        tile->state.store(OsmTile::State::Error, memory_order_release);
        return;
    }

    string cache_path = get_cache_path(tile->key.z, tile->key.x, tile->key.y);
    FILE* f = fopen(cache_path.c_str(), "wb");
    if (f) {
        fwrite(raw.data(), 1, raw.size(), f);
        fclose(f);
    }

    int w, h, ch;
    unsigned char* px = stbi_load_from_memory(
        raw.data(), (int)raw.size(),
        &w, &h, &ch, STBI_rgb_alpha);

    if (!px) {
        cerr << "[OSM] stbi failed z=" << tile->key.z
             << " x=" << tile->key.x << " y=" << tile->key.y << "\n";
        tile->state.store(OsmTile::State::Error, memory_order_release);
        return;
    }

    {
        lock_guard<mutex> lk(tile->pixels_mtx);
        tile->pixels = px;
        tile->w = w;
        tile->h = h;
    }

    tile->state.store(OsmTile::State::Decoded, memory_order_release);
    cout << "[OSM] decoded z=" << tile->key.z
         << " x=" << tile->key.x << " y=" << tile->key.y << "\n";
}

TileLoader::TileLoader(int num_threads) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    for (int i = 0; i < num_threads; i++)
        threads_.emplace_back(&TileLoader::worker, this);
}

TileLoader::~TileLoader() {
    stop();
    curl_global_cleanup();
}

void TileLoader::enqueue(OsmTile* tile) {
    {
        lock_guard<mutex> lk(queue_mtx_);
        queue_.push(tile);
    }
    cv_.notify_one();
}

void TileLoader::stop() {
    stopping_.store(true, memory_order_relaxed);
    cv_.notify_all();
    for (auto& t : threads_)
        if (t.joinable()) t.join();
    threads_.clear();
}

void TileLoader::worker() {
    while (true) {
        OsmTile* tile = nullptr;

        {
            unique_lock<mutex> lk(queue_mtx_);
            cv_.wait(lk, [this] {
                return stopping_.load(memory_order_relaxed) || !queue_.empty();
            });

            if (stopping_.load(memory_order_relaxed) && queue_.empty())
                return;

            if (!queue_.empty()) {
                tile = queue_.front();
                queue_.pop();
            }
        }

        if (!tile) continue;

        OsmTile::State expected = OsmTile::State::Queued;
        if (!tile->state.compare_exchange_strong(
                expected, OsmTile::State::Loading,
                memory_order_acq_rel))
        {
            continue;
        }

        fetch_tile(tile);
    }
}

OsmTileCache::OsmTileCache() : loader_(6) {}

OsmTileCache::~OsmTileCache() {
    loader_.stop();
    for (auto& [k, t] : cache_) delete t;
}

OsmTile* OsmTileCache::get(int z, int x, int y) {
    int max_tile = (1 << z);
    if (x < 0 || x >= max_tile || y < 0 || y >= max_tile)
        return nullptr;

    TileKey key{z, x, y};
    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second;

    string path = get_cache_path(z, x, y);
    if (fs::exists(path)) {
        vector<unsigned char> raw;
        FILE* f = fopen(path.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            fseek(f, 0, SEEK_SET);
            raw.resize(len);
            fread(raw.data(), 1, len, f);
            fclose(f);
        }
        if (!raw.empty()) {
            OsmTile* tile = new OsmTile();
            tile->key = key;
            int w, h, ch;
            unsigned char* px = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &ch, STBI_rgb_alpha);
            if (px) {
                tile->pixels = px;
                tile->w = w; tile->h = h;
                tile->state.store(OsmTile::State::Decoded, memory_order_release);
                cache_[key] = tile;
                return tile;
            } else {
                delete tile;
            }
        }
    }

    OsmTile* tile = new OsmTile();
    tile->key = key;
    tile->state.store(OsmTile::State::Queued, memory_order_relaxed);
    cache_[key] = tile;
    loader_.enqueue(tile);
    return tile;
}

void OsmTileCache::upload_ready() {
    for (auto& [k, tile] : cache_) {
        if (tile->state.load(memory_order_acquire) != OsmTile::State::Decoded)
            continue;

        unsigned char* px = nullptr;
        int w = 256, h = 256;

        {
            lock_guard<mutex> lk(tile->pixels_mtx);
            px = tile->pixels;
            w  = tile->w;
            h  = tile->h;
            tile->pixels = nullptr;
        }

        if (!px) continue;

        glGenTextures(1, &tile->tex_id);
        glBindTexture(GL_TEXTURE_2D, tile->tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);

        stbi_image_free(px);

        tile->state.store(OsmTile::State::Uploaded, memory_order_release);

        cout << "[OSM] GL uploaded z=" << k.z
             << " x=" << k.x << " y=" << k.y << "\n";
    }
}

void OsmTileCache::evict(int keep_z) {
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->first.z == keep_z) { ++it; continue; }

        OsmTile* tile = it->second;
        OsmTile::State s = tile->state.load(memory_order_acquire);

        if (s == OsmTile::State::Uploaded || s == OsmTile::State::Error) {
            delete tile;
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

OsmTileCache g_tile_cache;

void osm_draw(double lat_min, double lat_max,
              double lon_min, double lon_max,
              int zoom)
{
    g_tile_cache.upload_ready();

    double tx0 = osm_lon2x(lon_min, zoom);
    double tx1 = osm_lon2x(lon_max, zoom);
    double ty0 = osm_lat2y(lat_max, zoom);
    double ty1 = osm_lat2y(lat_min, zoom);

    int txi0 = max(0, (int)floor(tx0));
    int txi1 = min((1 << zoom) - 1, (int)floor(tx1));
    int tyi0 = max(0, (int)floor(ty0));
    int tyi1 = min((1 << zoom) - 1, (int)floor(ty1));

    for (int tx = txi0; tx <= txi1; tx++) {
        for (int ty = tyi0; ty <= tyi1; ty++) {
            OsmTile* tile = g_tile_cache.get(zoom, tx, ty);
            if (!tile) continue;

            if (tile->state.load(memory_order_acquire) != OsmTile::State::Uploaded)
                continue;

            double lon0 = osm_x2lon((double)tx,     zoom);
            double lon1 = osm_x2lon((double)(tx+1), zoom);
            double lat1 = osm_y2lat((double)ty,     zoom);
            double lat0 = osm_y2lat((double)(ty+1), zoom);

            ImPlotPoint bmin{ lon0, lat0 };
            ImPlotPoint bmax{ lon1, lat1 };
            ImVec2 uv0{ 0.f, 0.f };
            ImVec2 uv1{ 1.f, 1.f };
            ImVec4 tint{ 1, 1, 1, 1 };

            ImPlot::PlotImage("##tile",
                (ImTextureID)(intptr_t)tile->tex_id,
                bmin, bmax, uv0, uv1, tint);
        }
    }
}
