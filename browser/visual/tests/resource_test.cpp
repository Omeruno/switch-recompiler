#include "resources.h"
extern "C" {
#include "profile.h"
}
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
static void complete(visual::Resources &r) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (r.pending() && std::chrono::steady_clock::now() < deadline) {
        r.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(!r.pending());
}
int main(int argc, char **argv) {
    CHECK(argc == 7);
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    CHECK(browser_profile_open(nullptr, argv[5]));
    std::string base = argv[2];
    CHECK(visual::Resources::resolve("demo:/index.html", "https://example.com/") ==
          "https://example.com/");
    CHECK(visual::Resources::resolve(base + "/assets/style.css", "../image.png") ==
          base + "/image.png");
    CHECK(visual::Resources::resolve(base + "/a", "//localhost:123/a") ==
          "https://localhost:123/a");
    CHECK(visual::Resources::resolve(base, "http://example.com").empty());
    CHECK(visual::Resources::resolve(base, "https://user:secret@example.com/").empty());
    {
        visual::Resources r("");
        auto *partial = r.navigate_document(base + "/partial-document", 100);
        complete(r);
        CHECK(partial->state == visual::Resource::failed &&
              partial->result == CURLE_OPERATION_TIMEDOUT);
        CHECK(partial->http_status == 200 && partial->body_bytes == 3 && partial->data.empty() &&
              !partial->transfer_ok && partial->elapsed_ms >= 90);
        CHECK(partial->error.find("Сервер") != std::string::npos);
        r.clear();
        auto *slow = r.navigate_document(base + "/partial-document", 1000);
        complete(r);
        CHECK(slow->transfer_ok && slow->data.size() == 100 && slow->body_bytes == 100);
        r.clear();
        auto *page = r.request(base + "/set", 4096);
        CHECK(page && page->state == visual::Resource::queued);
        complete(r);
        CHECK(page->state == visual::Resource::ready);
        auto *script = r.request(base + "/resource.js", 4096);
        complete(r);
        CHECK(script->state == visual::Resource::ready);
        CHECK(std::string(script->data.begin(), script->data.end()).find("Cookie reached script") !=
              std::string::npos);
        auto *downgrade = r.request(base + "/redirect-http", 4096);
        complete(r);
        CHECK(downgrade->state == visual::Resource::failed);
        auto *hostname = r.request(argv[3], 4096);
        complete(r);
        CHECK(hostname->state == visual::Resource::failed);
        auto *expired = r.request(argv[4], 4096);
        complete(r);
        CHECK(expired->state == visual::Resource::failed);
        auto failures = r.failures();
        CHECK(failures.tls == 2 && failures.policy == 1 && failures.http == 0);
        CHECK(r.failures().tls == failures.tls); // Observing a failure does not recount it.
        r.clear();
        CHECK(r.count() == 0 && !r.pending());
        auto *small = r.request(base + "/set", 8);
        complete(r);
        CHECK(small->state == visual::Resource::failed && small->data.empty());
        CHECK(r.failures().limits == 1 && r.failures().transport == 0);
        r.clear();
        for (int i = 0; i < 8; i++)
            CHECK(r.request(base + "/set?n=" + std::to_string(i), 4096));
        r.pump();
        r.clear();
        CHECK(r.count() == 0 && !r.pending());
        auto *again = r.request(base + "/set", 4096);
        complete(r);
        CHECK(again->state == visual::Resource::ready);
        r.clear();
        for (int i = 0; i < 64; i++)
            CHECK(r.request(base + "/set?quota=" + std::to_string(i), 4096));
        CHECK(!r.request(base + "/set?quota=64", 4096));
        CHECK(!r.request(base + "/set?quota=64", 4096));
        CHECK(r.failures().limits == 1);
    }
    browser_profile_close();
    CHECK(browser_profile_open(nullptr, argv[6]));
    {
        visual::Resources r("");
        auto *bad = r.request(base + "/", 4096);
        complete(r);
        CHECK(bad->state == visual::Resource::failed);
    }
    browser_profile_close();
    curl_global_cleanup();
    puts("PASS async HTTPS, relative URLs, cookies, certificate errors, downgrade refusal, "
         "byte/request quotas, cancellation and reuse");
}
