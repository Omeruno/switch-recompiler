#include <switch.h>
#include <stdio.h>
#include <string.h>

static const char *const k_sites[] = {
    "https://keldurn.com/ru/",
    "https://play.keldurn.com/",
};

static bool valid_https_url(const char *url) {
    if (strncmp(url, "https://", 8) != 0 || !url[8]) return false;
    size_t length = strlen(url);
    if (length > 1023) return false;
    for (size_t i = 8; i < length; ++i) {
        unsigned char c = (unsigned char)url[i];
        if (c <= 0x20 || c == 0x7f || c == '\\' || c == '"' || c == '<' || c == '>') return false;
    }
    return true;
}

static bool enter_url(char *url, size_t capacity) {
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0))) return false;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, "HTTPS address");
    swkbdConfigSetGuideText(&keyboard, "Enter a complete https:// URL");
    swkbdConfigSetStringLenMax(&keyboard, 1023);
    swkbdConfigSetInitialText(&keyboard, url);
    char typed[1024] = {0};
    Result result = swkbdShow(&keyboard, typed, sizeof(typed));
    swkbdClose(&keyboard);
    if (R_FAILED(result) || !valid_https_url(typed) || strlen(typed) >= capacity) return false;
    strcpy(url, typed);
    return true;
}

static Result open_url(const char *url, WebExitReason *reason) {
    WebCommonConfig config;
    WebCommonReply reply;
    Result result = webPageCreate(&config, url);
    if (R_FAILED(result)) return result;
    // The WebApplet otherwise inherits the calling title's narrow whitelist.
    result = webConfigSetWhitelist(&config, "^https://.*");
    if (R_FAILED(result)) return result;
    result = webConfigSetPointer(&config, true);
    if (R_FAILED(result)) return result;
    result = webConfigSetLeftStickMode(&config, WebLeftStickMode_Cursor);
    if (R_FAILED(result)) return result;
    result = webConfigShow(&config, &reply);
    if (R_SUCCEEDED(result)) result = webReplyGetExitReason(&reply, reason);
    return result;
}

int main(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    int selection = 0;
    char custom_url[1024] = "https://";
    char status[160] = "Choose a site. Use title override for full memory.";

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 pressed = padGetButtonsDown(&pad);
        if (pressed & HidNpadButton_Plus) break;
        if (pressed & (HidNpadButton_Up | HidNpadButton_Down))
            selection = (selection + 2 + ((pressed & HidNpadButton_Down) ? 1 : -1)) % 3;
        if (pressed & HidNpadButton_A) {
            bool ready = selection != 2 || enter_url(custom_url, sizeof(custom_url));
            if (ready) {
                const char *url = selection == 2 ? custom_url : k_sites[selection];
                WebExitReason reason = WebExitReason_UnknownE;
                Result result = open_url(url, &reason);
                if (R_FAILED(result))
                    snprintf(status, sizeof(status), "WebApplet failed: 0x%08X", result);
                else if (reason == WebExitReason_ErrorDialog)
                    snprintf(status, sizeof(status), "WebApplet displayed an error; check connection/site.");
                else
                    snprintf(status, sizeof(status), "WebApplet closed (reason %u).", (unsigned)reason);
            } else snprintf(status, sizeof(status), "URL cancelled or invalid. HTTPS is required.");
            padUpdate(&pad);
        }

        consoleClear();
        printf("Switch Browser Probe\n\n");
        printf("%c Keldurn (Russian site)\n", selection == 0 ? '>' : ' ');
        printf("%c Keldurn (game)\n", selection == 1 ? '>' : ' ');
        printf("%c Enter HTTPS address\n\n", selection == 2 ? '>' : ' ');
        printf("D-pad: select   A: open   +: quit\n");
        printf("Touch and stick controls are provided by Switch WebApplet.\n\n");
        printf("%s\n", status);
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
