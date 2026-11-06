#pragma once
#include "forms.h"
#include <cstdint>
#include <curl/curl.h>
#include <map>
#include <string>
#include <vector>
namespace visual {
struct WebRequest {
    std::string source, mode = "cors", credentials = "same-origin", redirect = "follow";
    std::vector<std::pair<std::string, std::string>> headers;
    unsigned timeout_ms = 15000;
};
struct Resource {
    enum State { queued, loading, checking, ready, failed } state = queued;
    std::string url, effective, error;
    std::string method = "GET", current_url, location, post_body, form_source;
    std::vector<std::string> form_rules;
    bool form_navigation = false;
    unsigned redirects = 0;
    bool web_request = false, preflight = false, cors_tainted = false;
    WebRequest web;
    std::string cors_origin, web_error, status_text, pending_url;
    std::map<std::string, std::string> response_headers;
    std::vector<std::string> unsafe_headers;
    curl_slist *request_headers = nullptr;
    uint64_t started_ms = 0, deadline_ms = 0;
    std::vector<unsigned char> data;
    std::vector<std::string> policies;
    size_t header_bytes = 0, web_header_bytes = 0;
    long http_status = 0;
    bool transfer_ok = false, critical = false, stylesheet = false, image = false;
    CURLcode result = CURLE_OK;
    bool byte_limit = false, policy_blocked = false;
    CURL *handle = nullptr;
    size_t limit = 0;
};
class Resources {
    CURLM *multi = nullptr;
    std::string local_root;
    std::map<std::string, Resource> items;
    size_t received = 0;
    unsigned active = 0, next_navigation = 0;
    bool request_limit = false;
    static size_t receive(char *, size_t, size_t, void *);
    static size_t header(char *, size_t, size_t, void *);
    void start(Resource &);
    void completed(Resource &);
    void stop(Resource &);
    bool prepare_web(Resource &);
    bool web_response(Resource &, long);
    void discard(Resource &);

  public:
    static constexpr size_t total_limit = 16u * 1024u * 1024u;
    unsigned revision = 0, style_revision = 0, image_revision = 0;
    explicit Resources(std::string root);
    ~Resources();
    Resources(const Resources &) = delete;
    Resources &operator=(const Resources &) = delete;
    void clear();
    static std::string resolve(const std::string &base, const std::string &reference);
    Resource *request(const std::string &, size_t limit);
    Resource *navigate_form(const Submission &, const std::string &source,
                            const std::vector<std::string> &policies, size_t limit = 1024u * 1024u);
    Resource *network(const std::string &url, const std::string &method, std::string body,
                      WebRequest options);
    void cancel(Resource &, const std::string &error = "AbortError");
    void timeout(Resource &, unsigned ms);
    void follow(Resource &, bool permitted);
    void release_body(Resource &);
    static bool forbidden_header(const std::string &);
    static bool valid_header(const std::string &, const std::string &);
    static bool valid_method(const std::string &);
    std::vector<std::pair<std::string, std::string>> exposed_headers(const Resource &) const;
    bool pump();
    bool pending() const;
    bool pending_styles() const;
    bool pending_load() const;
    size_t count() const { return items.size(); }
    struct Failures {
        unsigned http = 0, tls = 0, transport = 0, limits = 0, policy = 0;
    };
    Failures failures() const;
    std::string status() const;
};
} // namespace visual
