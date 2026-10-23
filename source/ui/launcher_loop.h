#pragma once
namespace ui {
// One policy for Paint and all other EXEs; L is an explicit compatibility override.
constexpr bool UseNewGuestUi(bool legacyRequested) { return !legacyRequested; }
// RunGuest returns only after all guest work and session teardown have drained.
// Output also needs cleanup when initialization only partially succeeded.
template<class Frontend> void RunLauncher(Frontend& frontend) {
    bool initial=true;
    while(frontend.Alive()) {
        if(!frontend.Pick(initial) || !frontend.Alive())break;
        initial=false;
        if(frontend.BeginOutput())frontend.RunGuest();
        frontend.EndOutput();
        if(!frontend.Alive())break;
        frontend.RestorePicker();
    }
}
} // namespace ui
