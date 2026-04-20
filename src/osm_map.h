#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <atomic>
#include <functional>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#include <GL/gl.h>
#endif

double osm_lon2x(double lon, int z);
double osm_lat2y(double lat, int z);
double osm_x2lon(double x, int z);
double osm_y2lat(double y, int z);
int    osm_auto_zoom(double lon_range, float plot_px_w);

std::string get_cache_path(int z, int x, int y);

struct TileKey {
    int z, x, y;

    bool operator==(const TileKey& o) const {
        return z == o.z && x == o.x && y == o.y;
    }
    bool operator<(const TileKey& o) const {
        if (z != o.z) return z < o.z;
        if (x != o.x) return x < o.x;
        return y < o.y;
    }
};

struct TileKeyHash {
    size_t operator()(const TileKey& k) const {
        return (size_t)k.z * 1000000007ULL ^
               (size_t)k.x * 998244353ULL  ^
               (size_t)k.y * 999999937ULL;
    }
};

struct OsmTile {
    TileKey key{};

    enum class State {
        Queued,
        Loading,
        Decoded,
        Uploaded,
        Error
    };

    std::atomic<State> state{ State::Queued };

    std::mutex         pixels_mtx;
    unsigned char*     pixels = nullptr;
    int w = 256, h = 256;

    GLuint tex_id = 0;

    ~OsmTile();
};

class TileLoader {
public:
    explicit TileLoader(int num_threads = 6);
    ~TileLoader();

    void enqueue(OsmTile* tile);

    void stop();

private:
    void worker();

    std::vector<std::thread>   threads_;
    std::queue<OsmTile*>       queue_;
    std::mutex                 queue_mtx_;
    std::condition_variable    cv_;
    std::atomic<bool>          stopping_{ false };
};

class OsmTileCache {
public:
    OsmTileCache();
    ~OsmTileCache();

    OsmTile* get(int z, int x, int y);

    void upload_ready();

    void evict(int keep_z);

private:
    std::unordered_map<TileKey, OsmTile*, TileKeyHash> cache_;
    TileLoader loader_;
};

extern OsmTileCache g_tile_cache;

void osm_draw(double lat_min, double lat_max,
              double lon_min, double lon_max,
              int zoom);
