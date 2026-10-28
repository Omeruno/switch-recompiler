#include "legacy_session.h"
#include "host_functions.h"
#include <utility>
namespace win32 {
// One list supplies both defaults and exchange; no guest callbacks on discard.
#define LEGACY_VALUES(X) \
 X(g_imageBase,0) X(g_imageBuffer,nullptr) X(g_imageBufferSize,0) X(g_resourceDirRva,0) \
 X(g_loadedMenu,{}) X(g_menuLoaded,false) X(g_lastWindowW,0) X(g_lastWindowH,0) \
 X(g_dialogActive,false) X(g_currentDialog,{}) X(g_dialogHwnd,0) X(g_dialogItemText,{}) \
 X(g_dialogEndRequested,false) X(g_dialogEndResult,0) X(g_aboutBoxActive,false) \
 X(g_aboutBoxTitle,{}) X(g_aboutBoxLines,{}) X(g_aboutBoxHwnd,0) X(g_aboutBoxCloseRequested,false) \
 X(g_requestedToolId,0) X(g_requestedForegroundColor,0xFFFFFFFFu) X(g_requestedBackgroundColor,0xFFFFFFFFu) \
 X(g_paintCDC,0) X(g_paintView,0) X(g_mfcStubSlots,{}) \
 X(g_windowManager,{}) X(g_messageQueue,{}) X(g_timerRegistry,{})
struct LegacySession::Saved {
#define FIELD(name,initial) decltype(win32::name) name=initial;
    LEGACY_VALUES(FIELD)
#undef FIELD
    // Host callbacks/configuration are inherited, then restored if a frontend
    // changes them during the launch. Device ownership stays with the frontend.
    decltype(g_getTickCountImpl) tick=g_getTickCountImpl;
    decltype(g_pumpHostFrame) pump=g_pumpHostFrame;
    decltype(g_playAudioImpl) audio=g_playAudioImpl;
    decltype(g_consoleWriteImpl) console=g_consoleWriteImpl;
    void* stack=g_threadStackBase;
    bool trace=g_traceHostCalls,ui=IsHostLayerEnabled(kUiLayer);
    std::vector<RegistryHandle> registryHandles;
    void Exchange() {
        g_registry.ExchangeOpenHandles(registryHandles);
#define SWAP(name,initial) std::swap(win32::name,name);
        LEGACY_VALUES(SWAP)
#undef SWAP
    }
};
LegacySession::LegacySession():previous_(std::make_unique<Saved>()) {previous_->Exchange();}
LegacySession::~LegacySession() {
    previous_->Exchange();
    g_getTickCountImpl=previous_->tick;g_pumpHostFrame=previous_->pump;
    g_playAudioImpl=previous_->audio;g_consoleWriteImpl=previous_->console;
    g_threadStackBase=previous_->stack;g_traceHostCalls=previous_->trace;
    EnableHostLayer(kUiLayer,previous_->ui);
}
#undef LEGACY_VALUES
}
