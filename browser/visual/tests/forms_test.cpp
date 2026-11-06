#include "choices.h"
#include "scripts.h"
extern "C" {
#include "profile.h"
}
#include <cstdio>
#include <cstdlib>
#include <map>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
static std::map<std::string, std::vector<std::string>> decode(const std::string &body) {
    std::map<std::string, std::vector<std::string>> result;
    size_t start = 0;
    while (start < body.size()) {
        auto end = body.find('&', start);
        auto part = body.substr(start, end == std::string::npos ? end : end - start);
        auto eq = part.find('=');
        std::string n = part.substr(0, eq), v = eq == std::string::npos ? "" : part.substr(eq + 1);
        std::replace(n.begin(), n.end(), '+', ' ');
        std::replace(v.begin(), v.end(), '+', ' ');
        int nn = 0, nv = 0;
        char *name = curl_easy_unescape(nullptr, n.c_str(), int(n.size()), &nn),
             *value = curl_easy_unescape(nullptr, v.c_str(), int(v.size()), &nv);
        CHECK(name && value);
        result[std::string(name, size_t(nn))].push_back(std::string(value, size_t(nv)));
        curl_free(name);
        curl_free(value);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return result;
}
int main(int argc, char **argv) {
    CHECK(argc == 3);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    CHECK(TTF_Init() == 0);
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == 0);
    CHECK(browser_profile_open(nullptr, nullptr));
    auto *surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    CHECK(surface);
    auto *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        c.base = c.page_url = "https://example.test/page";
        auto doc = litehtml::document::createFromString(
            R"HTML(<!doctype html><form id=f action="/submit?old=1" method=post>
          <input name=csrf value=TOKEN type=hidden><input id=user name=user required value="Иван + &">
          <input id=pass name=password type=password value=LOCAL_TEST_PASSWORD>
          <input id=remember type=checkbox name=remember value=yes checked><input id=off type=checkbox name=off>
          <input id=r1 type=radio name=region value=a checked><input id=r2 type=radio name=region value=b checked>
          <select id=s name=language required><option value="" disabled>Choose</option><option value=ru selected>Русский</option><option value=en>English</option><optgroup disabled><option value=blocked>Blocked</option></optgroup></select>
          <select id=m name=multi multiple><option value=a selected>A</option><option value=b selected>B</option><option value=c disabled selected>C</option></select>
          <input name=disabled value=NO disabled><input name=readonly value=YES readonly required>
          <fieldset disabled><legend><input name=legend value=YES></legend><input name=fieldset value=NO></fieldset>
          <textarea id=notes name=notes>first</textarea><button id=go name=button value=save>Send</button><button name=other value=NO>Other</button>
          <input id=email type=email name=email><input id=number type=number name=number min=2 max=8>
        </form><input form=f name=outside value=YES><input form=missing name=orphan value=NO>)HTML",
            &c, litehtml::master_css + visual::control_styles());
        doc->render(1280);
        auto find = [&](const char *id) { return doc->root()->select_one(std::string("#") + id); };
        auto field = [&](const char *id) {
            auto p = std::dynamic_pointer_cast<visual::Control>(find(id));
            CHECK(p);
            return p;
        };
        auto refresh = [&] {
            c.invalidate_text();
            doc->refresh_native_dom();
            doc->render(1280);
        };
        CHECK(!field("r1")->checked() && field("r2")->checked());
        CHECK(field("remember")->checked());
        CHECK(field("s")->value() == "ru");
        std::string error;
        auto request = visual::prepare_form(find("go"), c.base, error);
        CHECK(request && request.method == "POST" &&
              request.url == "https://example.test/submit?old=1");
        auto data = decode(request.body);
        CHECK(data["user"] == std::vector<std::string>{"Иван + &"});
        CHECK(data["password"] == std::vector<std::string>{"LOCAL_TEST_PASSWORD"});
        CHECK(data["csrf"] == std::vector<std::string>{"TOKEN"});
        CHECK(data["region"] == std::vector<std::string>{"b"});
        CHECK(data["language"] == std::vector<std::string>{"ru"});
        CHECK(data["multi"] == std::vector<std::string>({"a", "b"}));
        CHECK(data["legend"] == std::vector<std::string>{"YES"});
        CHECK(data["outside"] == std::vector<std::string>{"YES"});
        CHECK(data.count("off") == 0 && data.count("disabled") == 0 &&
              data.count("fieldset") == 0 && data.count("orphan") == 0 && data.count("other") == 0);
        field("remember")->checked(false);
        field("r1")->checked(true);
        field("s")->value("en");
        field("notes")->value("second\nthird\rfourth\r\nfifth");
        auto multiline = visual::prepare_form(find("go"), c.base, error);
        CHECK(multiline);
        CHECK(decode(multiline.body)["notes"] ==
              std::vector<std::string>{"second\r\nthird\r\nfourth\r\nfifth"});
        refresh();
        CHECK(find("remember")->get_attr("checked") != nullptr);
        CHECK(doc->root()->select_one("#remember:checked") == nullptr);
        CHECK(doc->root()->select_one("#r1:checked"));
        CHECK(!field("r2")->checked());
        CHECK(field("s")->selected_index() == 2);
        CHECK(doc->root()->select_all("option:checked").size() == 4);
        field("user")->value("");
        CHECK(!visual::prepare_form(find("go"), c.base, error));
        field("user")->value("restored");
        field("email")->value("not-email");
        CHECK(!visual::prepare_form(find("go"), c.base, error));
        field("email")->value("user@example.test");
        field("number")->value("1");
        CHECK(!visual::prepare_form(find("go"), c.base, error));
        field("number")->value("4");
        CHECK(visual::prepare_form(find("go"), c.base, error));
        visual::reset_form(find("go"));
        refresh();
        CHECK(field("user")->value() == "Иван + &" && field("remember")->checked() &&
              !field("r1")->checked() && field("r2")->checked());
        CHECK(field("s")->value() == "ru" && field("notes")->value() == "first");
        visual::Scripts js(doc, c, resources);
        auto eval = [&](const char *source) {
            if (!js.eval(source)) {
                fprintf(stderr, "JS: %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        eval("document.getElementById('remember').checked=false;document.getElementById('r1')."
             "checked=true;document.getElementById('s').selectedIndex=2;if(document.getElementById("
             "'s').value!=='en'||document.getElementById('remember').defaultChecked!==true||"
             "document.getElementById('r2').checked)throw Error('form "
             "properties');document.getElementById('f').submit();");
        auto scripted = js.submission();
        CHECK(scripted && scripted.method == "POST");
        data = decode(scripted.body);
        CHECK(data.count("remember") == 0 && data["language"] == std::vector<std::string>{"en"});
        eval("document.getElementById('f').reset();if(!document.getElementById('remember').checked|"
             "|document.getElementById('s').value!=='ru')throw "
             "Error('reset');globalThis.cancelled=0;document.getElementById('f').addEventListener('"
             "submit',e=>{cancelled++;e.preventDefault();});document.getElementById('f')."
             "requestSubmit(document.getElementById('go'));if(cancelled!==1)throw "
             "Error('requestSubmit event');");
        CHECK(!js.submission());
        js.flush();
        CHECK(visual::form_action_allowed({"default-src 'none'"}, c.page_url,
                                          "https://other.test/path"));
        CHECK(!visual::form_action_allowed({"form-action 'none'"}, c.page_url,
                                           "https://example.test/submit"));
        CHECK(visual::form_action_allowed({"form-action 'self'"}, c.page_url,
                                          "https://example.test/submit"));
        CHECK(!visual::form_action_allowed({"form-action 'self'"}, c.page_url,
                                           "https://other.test/submit"));
        CHECK(visual::form_action_allowed({"form-action https://example.test/allowed/"}, c.page_url,
                                          "https://example.test/allowed/child"));
        CHECK(!visual::form_action_allowed({"form-action https://example.test/allowed/"},
                                           c.page_url, "https://example.test/else"));
        CHECK(!visual::form_action_allowed({}, c.page_url, "http://example.test/submit"));
        visual::Choices choices(field("s"));
        CHECK(choices.hit({190, 90}) == 0);
        CHECK(choices.hit({10, 10}) == -1);
        choices.move(1);
        CHECK(choices.hover == field("s")->selected_index() + 1);
        CHECK(!choices.gestures.zoom_enabled);
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS successful controls, POST encoding, disabled/legend ownership, validation, live "
         "checked/select state, reset, JS submission and form-action CSP");
}
