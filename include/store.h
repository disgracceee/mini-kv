#pragma once
#include <array>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

class Store {
public:
    void   set(std::string key, std::string val);
    bool   get(const std::string& key, std::string& out) const;
    bool   del(const std::string& key);
    size_t size() const;

private:
    static constexpr size_t SHARDS = 16;

    struct Shard {
        mutable std::mutex mx;
        std::unordered_map<std::string, std::string> map;
    };

    std::array<Shard, SHARDS> shards_;

    Shard& shardFor(const std::string& key);
    const Shard& shardFor(const std::string& key) const;
};

extern Store g_store;
