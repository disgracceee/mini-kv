#include "store.h"

Store g_store;

Store::Shard& Store::shardFor(const std::string& key) {
    return shards_[std::hash<std::string>{}(key) % SHARDS];
}

const Store::Shard& Store::shardFor(const std::string& key) const {
    return shards_[std::hash<std::string>{}(key) % SHARDS];
}

void Store::set(std::string key, std::string val) {
    Shard& s = shardFor(key);
    std::lock_guard<std::mutex> lk(s.mx);
    s.map[std::move(key)] = std::move(val);
}

bool Store::get(const std::string& key, std::string& out) const {
    const Shard& s = shardFor(key);
    std::lock_guard<std::mutex> lk(s.mx);
    auto it = s.map.find(key);
    if (it == s.map.end())
        return false;
    out = it->second;          
    return true;
}

bool Store::del(const std::string& key) {
    Shard& s = shardFor(key);
    std::lock_guard<std::mutex> lk(s.mx);
    return s.map.erase(key) != 0;
}

size_t Store::size() const {
    size_t total = 0;
    for (const Shard& s : shards_) {
        std::lock_guard<std::mutex> lk(s.mx);
        total += s.map.size();
    }
    return total;
}
