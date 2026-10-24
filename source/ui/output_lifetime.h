#pragma once
namespace ui {
enum class OutputPreference {Software,GpuPreferred};
// Device ownership is the frontend's lifetime, not a guest's. Open may choose
// software fallback for GpuPreferred; retain that choice until an explicit mode
// change or a real failure. All methods run on the same native owner thread.
template<class Device> class OutputLifetime {
public:
    explicit OutputLifetime(Device& device):device_(device) {}
    ~OutputLifetime() {Close();}
    OutputLifetime(const OutputLifetime&)=delete;
    OutputLifetime& operator=(const OutputLifetime&)=delete;
    bool Ensure(OutputPreference mode) {
        if(ready_ && preference_==mode)return true;
        Close();preference_=mode;
        if(device_.Open(mode)){ready_=true;return true;}
        device_.Close();return false; // includes partially initialized devices
    }
    bool Ready() const {return ready_;}
    OutputPreference Preference() const {return preference_;}
    void Close() {if(ready_){device_.Close();ready_=false;}}
private:
    Device& device_;
    bool ready_=false;
    OutputPreference preference_=OutputPreference::GpuPreferred;
};
// Selection, execution and return use one frontend owner and one output device.
// EndGuest runs after native continuations/session cleanup, including load errors.
template<class Frontend> void RunWorkspace(Frontend& frontend) {
    struct CloseScope {Frontend& f;~CloseScope(){f.Close();}} close{frontend};
    bool initial=true;
    while(frontend.Alive()) {
        if(!frontend.Pick(initial) || !frontend.Alive())break;
        initial=false;
        if(frontend.BeginGuest())frontend.RunGuest();
        frontend.EndGuest();
        if(!frontend.Alive())break;
        frontend.ReturnToDesktop();
    }
}
} // namespace ui
