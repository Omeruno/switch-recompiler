"""DirectShow startup model that reports intro videos unavailable to the game."""
from uuid import UUID

FILTER_GRAPH = UUID("e436ebb3-524f-11ce-9f53-0020af0ba770").bytes_le
GRAPH = ("QueryInterface", "AddRef", "Release", "AddFilter", "RemoveFilter",
         "EnumFilters", "FindFilterByName", "ConnectDirect", "Reconnect", "Disconnect",
         "SetDefaultSyncSource", "Connect", "Render", "RenderFile", "AddSourceFilter",
         "SetLogFile", "Abort", "ShouldOperationContinue")
EVENTS = ("QueryInterface", "AddRef", "Release", "GetTypeInfoCount", "GetTypeInfo",
          "GetIDsOfNames", "Invoke", "GetEventHandle", "GetEvent", "WaitForCompletion",
          "CancelDefaultHandling", "RestoreDefaultHandling", "FreeEventParams",
          "SetNotifyWindow", "SetNotifyFlags", "GetNotifyFlags")
DISPATCH = ("QueryInterface", "AddRef", "Release", "GetTypeInfoCount", "GetTypeInfo", "GetIDsOfNames", "Invoke")
CONTROL = DISPATCH + ("Run", "Pause", "Stop", "GetState", "RenderFile", "AddSourceFilter",
                      "get_FilterCollection", "get_RegFilterCollection", "StopWhenReady")
POSITION = DISPATCH + ("get_Duration", "put_CurrentPosition", "get_CurrentPosition",
                       "get_StopTime", "put_StopTime", "get_PrerollTime", "put_PrerollTime",
                       "put_Rate", "get_Rate", "CanSeekForward", "CanSeekBackward")
WINDOW = DISPATCH + (
    "put_Caption", "get_Caption", "put_WindowStyle", "get_WindowStyle", "put_WindowStyleEx",
    "get_WindowStyleEx", "put_AutoShow", "get_AutoShow", "put_WindowState", "get_WindowState",
    "put_BackgroundPalette", "get_BackgroundPalette", "put_Visible", "get_Visible",
    "put_Left", "get_Left", "put_Width", "get_Width", "put_Top", "get_Top",
    "put_Height", "get_Height", "put_Owner", "get_Owner", "put_MessageDrain",
    "get_MessageDrain", "get_BorderColor", "put_BorderColor", "get_FullScreenMode",
    "put_FullScreenMode", "SetWindowForeground", "NotifyOwnerMessage", "SetWindowPosition",
)
COM_NAMES = {
    0x56A868A9: "IGraphBuilder", 0x56A868B1: "IMediaControl",
    0x56A868B6: "IMediaEventEx", 0x56A868B2: "IMediaPosition", 0x56A868B4: "IVideoWindow",
}


class VideoProbe:
    def __init__(self, boot):
        self.boot = boot
        self.events = []
        self.objects = {}
        self.references = {}
        self.notification = (0, 0, 0)
        self.pending_unavailable = False
        self.completed = False

    def object(self, interface):
        methods = {"IGraphBuilder": GRAPH, "IMediaEventEx": EVENTS,
                   "IVideoWindow": WINDOW, "IMediaControl": CONTROL, "IMediaPosition": POSITION}.get(
            interface, ("QueryInterface", "AddRef", "Release")
        )
        if interface not in self.objects:
            self.objects[interface] = self.boot.d3d.make_interface(interface, methods)
            self.references[interface] = 0
        self.references[interface] += 1
        return self.objects[interface]

    def handle(self, dll, name, arg, finish):
        if name == "CoCreateInstance" and bytes(self.boot.cpu.mem_read(arg(0), 16)) == FILTER_GRAPH:
            self.boot.put(arg(4), self.object("IGraphBuilder"))
            self.events.append(("FilterGraph", "diagnostic object"))
            finish(0, 20)
        elif dll not in self.objects:
            return False
        elif name == "QueryInterface":
            interface = COM_NAMES.get(self.boot.get(arg(1)))
            self.boot.put(arg(2), self.object(interface) if interface else 0)
            finish(0 if interface else 0x80004002, 12)
        elif name in ("AddRef", "Release"):
            self.references[dll] += 1 if name == "AddRef" else -1
            finish(self.references[dll], 4)
        elif name == "SetNotifyWindow":
            self.notification = (arg(1), arg(2), arg(3))
            finish(0, 16)
        elif name == "SetNotifyFlags":
            finish(0, 8)
        elif dll == "IVideoWindow" and name in ("put_Visible", "SetWindowForeground"):
            finish(0, 8)
        elif dll == "IMediaControl" and name in ("Run", "Pause", "Stop", "StopWhenReady"):
            self.events.append(("intro control", name))
            if name == "Run" and self.pending_unavailable and self.boot.message_loop:
                self.pending_unavailable = False
                self.completed = True
                window, message, parameter = self.notification
                if window and message:
                    self.boot.message_loop.post(window, message, lparam=parameter)
            finish(0, 4)
        elif dll == "IMediaEventEx" and name == "GetEvent":
            if self.completed:
                self.completed = False
                self.boot.put(arg(1), 1)  # EC_COMPLETE, with an S_OK parameter.
                self.boot.put(arg(2), 0)
                self.boot.put(arg(3), 0)
                self.events.append(("intro completion", "diagnostic"))
                finish(0, 20)
            else:
                finish(0x80004004, 20)
        elif dll == "IMediaEventEx" and name == "FreeEventParams":
            finish(0, 16)
        elif dll == "IGraphBuilder" and name == "Abort":
            self.completed = False
            self.pending_unavailable = False
            finish(0, 4)
        elif dll == "IMediaPosition" and name in ("get_Duration", "get_CurrentPosition"):
            self.boot.cpu.mem_write(arg(1), bytes(8))
            finish(0, 8)
        elif dll == "IMediaPosition" and name == "put_CurrentPosition":
            finish(0, 12)
        elif name == "RenderFile":
            raw = bytearray()
            for offset in range(0, 1024, 2):
                word = self.boot.cpu.mem_read(arg(1) + offset, 2)
                if word == b"\0\0":
                    break
                raw.extend(word)
            self.pending_unavailable = True
            self.events.append(("movie unavailable", raw.decode("utf-16-le", "replace")))
            finish(0x80070002, 12)
        else:
            return False
        return True
