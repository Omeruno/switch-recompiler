#include "resources.h"
extern "C" {
#include "profile.h"
}
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
static void complete(visual::Resources &r) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (r.pending() && std::chrono::steady_clock::now() < deadline) {
        r.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(!r.pending());
}
static std::string body(visual::Resource *r) { return std::string(r->data.begin(), r->data.end()); }
int main(int argc, char **argv) {
    CHECK(argc == 7);
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == 0);
    CHECK(browser_profile_open(nullptr, argv[5]));
    std::string base = argv[2];
    const std::string payload = "user=%D0%98%D0%B2%D0%B0%D0%BD%20%2B%20%26&password=LOCAL_TEST_"
                                "PASSWORD&csrf=TOKEN&multi=a&multi=b";
    {
        visual::Resources resources("");
        auto post = [&](const std::string &path,
                        const std::vector<std::string> &rules = std::vector<std::string>{}) {
            auto *r =
                resources.navigate_form({base + path, "POST", payload}, base + "/form", rules);
            CHECK(r);
            complete(resources);
            CHECK(r->post_body.empty() && r->handle == nullptr);
            return r;
        };
        auto *echo = post("/forms-echo");
        CHECK(echo->state == visual::Resource::ready && echo->method == "POST");
        CHECK(body(echo).find("\"accepted\": true") != std::string::npos);
        CHECK(body(echo).find("LOCAL_TEST_PASSWORD") == std::string::npos);
        auto *redirect = post("/forms-303");
        CHECK(redirect->state == visual::Resource::ready && redirect->method == "GET" &&
              redirect->redirects == 1);
        CHECK(redirect->effective == base + "/forms-final");
        CHECK(body(redirect).find("\"authenticated\": true") != std::string::npos);
        auto *preserved = post("/forms-307");
        CHECK(preserved->state == visual::Resource::ready && preserved->method == "POST" &&
              preserved->redirects == 1);
        CHECK(body(preserved).find("\"accepted\": true") != std::string::npos);
        auto *http_error = post("/forms-400");
        CHECK(http_error->state == visual::Resource::failed && http_error->transfer_ok &&
              http_error->http_status == 400 &&
              body(http_error).find("Invalid input") != std::string::npos);
        auto *blocked = post("/forms-policy", {"form-action " + base + "/forms-policy"});
        CHECK(blocked->state == visual::Resource::failed && !blocked->transfer_ok);
        auto *downgrade = post("/forms-downgrade");
        CHECK(downgrade->state == visual::Resource::failed && !downgrade->transfer_ok);
        auto *loop = post("/forms-loop");
        CHECK(loop->state == visual::Resource::failed && loop->redirects == 5);
        CHECK(resources.failures().http == 1 && resources.failures().policy == 3);
        CHECK(!resources.navigate_form({base + "/forms-echo", "POST", payload}, base + "/form",
                                       {"form-action 'none'"}));
        for (int i = 0; i < 10; i++)
            resources.pump();
        auto *count = resources.request(base + "/forms-count", 4096);
        complete(resources);
        CHECK(count->state == visual::Resource::ready);
        auto counts = body(count);
        CHECK(counts.find("\"forbidden\": 0") != std::string::npos &&
              counts.find("\"echo\": 1") != std::string::npos &&
              counts.find("\"preserved\": 1") != std::string::npos);
        auto *cancel =
            resources.navigate_form({base + "/forms-echo", "POST", payload}, base + "/form", {});
        CHECK(cancel);
        resources.pump();
        resources.clear();
        CHECK(!resources.pending() && resources.count() == 0);
    }
    browser_profile_close();
    curl_global_cleanup();
    puts("PASS HTTPS POST, body encoding, cookies, 303/307 redirects, policy/downgrade guards, "
         "HTTP errors, redirect limit, no idle replay and cancellation");
}
