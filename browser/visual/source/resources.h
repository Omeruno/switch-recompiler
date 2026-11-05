#pragma once
#include <curl/curl.h>
#include <map>
#include <string>
#include <vector>
namespace visual {
struct Resource {
    enum State { queued, loading, ready, failed } state = queued;
    std::string url, effective, error;
    std::vector<unsigned char> data;
    CURL *handle = nullptr;
    size_t limit = 0;
};
class Resources {
    CURLM *multi = nullptr;
    std::string local_root;
    std::map<std::string, Resource> items;
    size_t received = 0;
    unsigned active = 0;
    static size_t receive(char *, size_t, size_t, void *);
    void start(Resource &);

  public:
    static constexpr size_t total_limit = 16u * 1024u * 1024u;
    unsigned revision = 0;
    explicit Resources(std::string root);
    ~Resources();
    Resources(const Resources &) = delete;
    Resources &operator=(const Resources &) = delete;
    void clear();
    static std::string resolve(const std::string &base, const std::string &reference);
    Resource *request(const std::string &, size_t limit);
    bool pump();
    bool pending() const;
    size_t count() const { return items.size(); }
    std::string status() const;
};
} // namespace visual
