#pragma once
#include "executable_catalog.h"
#include "region.h"
#include "surface.h"
#include "shell_browser.h"
#include "text_document.h"
#include "bitmap_document.h"
#include "shell_link.h"
#include "folder_tree.h"
#include <array>
#include <string>
#include <vector>

namespace ui {
// Host-owned idle shell. No Process, HWND, guest pointers or GL context.
// Fixed logical coordinates; presentation and pointer use the same FitViewport.
class DesktopShell {
public:
    static constexpr int Width=1280,Height=720,Rows=14,RowBase=100,BrowserRowBase=200;
    enum Target {None=0,Start=1,Programs=2,Computer=3,Documents=4,
                 MenuPrograms=5,MenuComputer=6,MenuDocuments=7,MenuRun=8,
                 About=9,Exit=10,Close=11,Launch=12,Previous=13,Next=14,NavBack=15,NavUp=16,Refresh=17,Sort=18,Address=19,
                 QuickComputer=20,QuickDocuments=21,QuickC=22,QuickS=23,QuickR=24,OpenWith=25,GuestTask=26,NewFolder=27,Rename=28,Delete=29,Properties=30,ConfirmDelete=31,CancelDialog=32,Copy=33,Paste=34,FileActions=35,FolderActions=36,Settings=37,PreferenceSort=38,AssociationPrevious=39,AssociationNext=40,AssociationChange=41,AssociationForget=42,PreferenceSave=43,ContextBase=50,TextOpen=60,TextNew=61,TextEdit=62,TextInsert=63,TextErase=64,TextSave=65,TextSaveAs=66,TextDiscard=67,ImageOpen=68,ImageFit=69,ImageActual=70,DesktopShortcut=71,StartShortcut=72,TreeOpen=73,TreeExpand=74,TreeCollapse=75,FolderTreeButton=76,TextLineBase=300,TreeRowBase=400,DesktopLinkBase=500};
    enum class Panel {Desktop,Programs,About,Explorer};
    enum class TextInput {None,Address,Run,OpenWith,NewFolder,Rename,PasteName,AssociationApplication,TextLine,TextSavePath,ShortcutCommand};
    enum class Dialog {None,DeleteConfirm,Properties,CopyProgress,Preferences,TextEditor,TextClose,ImageViewer,FolderTree};
    const FolderTree& Tree() const {return tree_;}
    const std::vector<std::string>& DesktopLinks() const {return desktopLinks_;}
    const BitmapDocument& Image() const {return image_;}
    static Rect ImageViewport() {return {416,280,1006,492};}
    bool ImageFitted() const {return imageFit_;}
    int ImagePanX() const {return imagePanX_;}
    int ImagePanY() const {return imagePanY_;}
    const TextDocument& Document() const {return document_;}
    size_t TextSelection() const {return textSelection_;}
    Dialog ActiveDialog() const {return dialog_;}
    bool ModalActive() const {return dialog_!=Dialog::None || textInput_!=TextInput::None || contextOpen_;}
    const std::string& DialogPath() const {return operationPath_;}
    const rt::FileProperties& PropertiesSnapshot() const {return properties_;}
    bool ConfigureFilesystem(const std::string& system,const std::string& sd,const std::string& rom,std::string& error);
    bool FilesystemReady() const {return browser_.Ready();}
    const ShellBrowser& Browser() const {return browser_;}
    TextInput PendingTextInput() const {return textInput_;}
    const std::string& InitialText() const {return initialText_;}
    bool ServiceCopy(); // owner-thread bounded work; true requests a redraw
    bool CopyRunning() const {return browser_.CopyRunning();}
    const std::string& ClipboardPath() const {return clipboardPath_;}
    void CompleteTextInput(bool accepted,const std::string& text);
    void SetCatalog(const ExecutableCatalog&);
    void SetStatus(const std::string&);
    void SetGuestActive(const std::string& label);
    void EndGuest();
    bool GuestActive() const {return guestActive_;}
    void SetGuestTaskPending(bool value) {guestTaskPending_=value;}
    void SetGuestMinimized(bool value) {guestMinimized_=value;}
    void Enter(); // Cancel transient presses/menu on return, retain panel/selection.
    void ToggleStart();
    void OpenContext(bool folder=false);
    bool ContextOpen() const {return contextOpen_;}
    size_t ContextCount() const {return contextCount_;}
    int ContextAction(size_t index) const {return index<contextCount_?contextActions_[index]:None;}
    Rect ContextBounds(int target) const;
    void SecondaryDown(int x,int y);
    void SecondaryUp(int x,int y);
    void Move(int delta);
    void NextFocus();
    void Page(int delta);
    void Accept();
    void Back();
    void CancelPointer() {pressed_=None;secondaryTarget_=-2;}
    void PointerMove(int x,int y);
    void PointerDown(int x,int y);
    void PointerUp(int x,int y);
    int HitTest(int x,int y) const;
    static Rect Bounds(int target);
    bool Enabled(int target) const;
    const std::string& LaunchPath() const {return request_.nativePath;}
    const ShellLaunchRequest& LaunchRequest() const {return request_;}
    bool ExitRequested() const {return exit_;}
    Panel ActivePanel() const {return panel_;}
    bool StartOpen() const {return startOpen_;}
    size_t Selection() const {return selected_;}
    size_t Count() const {return files_.size();}
    int Focus() const {return focus_;}
    int Hover() const {return hover_;}
    // Safe copy of labels, bounded inventory and status; no borrowed catalog.
    void Render(Surface&,const std::u16string& clock) const;
private:
    void Activate(int target);
    bool ControlEnabled(int target) const;
    void CloseContext();
    void ShowContext(bool folder,int x,int y);
    int SecondaryTarget(int x,int y) const;
    std::array<int,8> contextActions_{};
    size_t contextCount_=0;
    bool contextOpen_=false;
    bool contextFile_=false;
    Rect contextRect_;
    std::string contextPath_,contextParent_,secondaryPath_,secondaryParent_;
    int contextReturnFocus_=FileActions,secondaryTarget_=-2;
    void OpenBrowser(const std::string& path);
    void LaunchProgram();
    void RefreshLinks();
    std::vector<std::string> desktopLinks_,catalogFiles_;
    bool shortcutStart_=false,linkTruncated_=false;
    size_t linkErrors_=0;
    FolderTree tree_;
    size_t treeSelection_=0;
    void OpenCurrent();
    void ChooseApplication(const std::string&);
    void FocusBrowserSelection();
    void CloseDialog();
    void CloseText();
    void SaveText(const std::string& path="");
    bool TextDialog() const {return dialog_==Dialog::TextEditor || dialog_==Dialog::TextClose;}
    BitmapDocument image_;
    bool imageFit_=true;
    int imagePanX_=0,imagePanY_=0;
    TextDocument document_;
    size_t textSelection_=0,textColumn_=0;
    std::u16string textNotice_;
    bool CaptureSelection();
    void MutationFeedback(ShellBrowser::MutationResult);
    Dialog dialog_=Dialog::None;
    rt::FileProperties properties_;
    std::string operationPath_,operationParent_,clipboardPath_,clipboardName_,pendingAssociation_;
    size_t preferenceSelection_=0;
    std::u16string operationNotice_;
    int dialogReturnFocus_=Properties;
    size_t First() const {return selected_/Rows*Rows;}
    std::vector<int> FocusOrder() const;
    std::vector<std::string> files_;
    std::string status_,initialText_,pendingDocument_;
    ShellBrowser browser_;
    ShellLaunchRequest request_;
    TextInput textInput_=TextInput::None;
    size_t selected_=0,errors_=0;
    std::string guestLabel_;
    bool guestActive_=false,guestMinimized_=false,guestTaskPending_=false;
    Panel returnPanel_=Panel::Desktop;
    int returnFocus_=Programs;
    bool truncated_=false,startOpen_=false,exit_=false;
    Panel panel_=Panel::Desktop;
    int focus_=Programs,menuReturnFocus_=Programs,hover_=None,pressed_=None;
};
} // namespace ui
