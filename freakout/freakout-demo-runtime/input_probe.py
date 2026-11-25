"""Initial DirectInput 8 model for tracing menu startup with neutral input."""

ROOT_METHODS = (
    "QueryInterface", "AddRef", "Release", "CreateDevice", "EnumDevices", "GetDeviceStatus",
    "RunControlPanel", "Initialize", "FindDevice", "EnumDevicesBySemantics", "ConfigureDevices",
)
DEVICE_METHODS = (
    "QueryInterface", "AddRef", "Release", "GetCapabilities", "EnumObjects", "GetProperty",
    "SetProperty", "Acquire", "Unacquire", "GetDeviceState", "GetDeviceData", "SetDataFormat",
    "SetEventNotification", "SetCooperativeLevel", "GetObjectInfo", "GetDeviceInfo",
    "RunControlPanel", "Initialize", "CreateEffect", "EnumEffects", "GetEffectInfo",
    "GetForceFeedbackState", "SendForceFeedbackCommand", "EnumCreatedEffectObjects",
    "Escape", "Poll", "SendDeviceData", "EnumEffectsInFile", "WriteEffectToFile",
    "BuildActionMap", "SetActionMap", "GetImageInfo",
)
INPUT_MODULE = 0x2D180001
KEYBOARD = bytes.fromhex("612b1d6fa0d5cf11bfc7444553540000")
MOUSE = bytes.fromhex("602b1d6fa0d5cf11bfc7444553540000")
INVALID = 0x80070057


class InputProbe:
    def __init__(self, boot):
        self.boot = boot
        self.events = []
        self.module_refs = 0
        self.roots = {}
        self.devices = {}

    def handle(self, dll, name, arg, finish):
        if name == "LoadLibraryA" and self.boot.c_string(arg(0)).lower() == b"dinput8.dll":
            self.module_refs += 1
            self.events.append(("LoadLibraryA", "dinput8.dll"))
            finish(INPUT_MODULE, 4)
        elif name == "GetModuleHandleA" and arg(0) and self.boot.c_string(arg(0)).lower() == b"dinput8.dll":
            finish(INPUT_MODULE if self.module_refs else 0, 4)
        elif name == "FreeLibrary" and arg(0) == INPUT_MODULE:
            if self.module_refs:
                self.module_refs -= 1
                finish(1, 4)
            else:
                finish(0, 4)
        elif name == "GetProcAddress" and arg(0) == INPUT_MODULE:
            selector = arg(1)
            symbol = self.boot.c_string(selector) if selector > 0xFFFF else b""
            address = self.boot.add_host_api("dinput8.dll", "DirectInput8Create") if symbol == b"DirectInput8Create" else 0
            finish(address, 8)
        elif name == "DirectInput8Create" and dll == "dinput8.dll":
            self.events.append(("DirectInput8Create", arg(1)))
            if arg(1) != 0x800 or not arg(3) or arg(4):
                finish(INVALID, 20)
            else:
                root = self.boot.d3d.make_interface("IDirectInput8", ROOT_METHODS)
                self.roots[root] = 1
                self.boot.put(arg(3), root)
                finish(0, 20)
        elif dll == "IDirectInput8" and arg(0) in self.roots:
            if not self.root_call(name, arg, finish):
                return False
        elif dll == "IDirectInputDevice8" and arg(0) in self.devices:
            if not self.device_call(name, arg, finish):
                return False
        else:
            return False
        return True

    def root_call(self, name, arg, finish):
        if name in ("AddRef", "Release"):
            self.roots[arg(0)] += 1 if name == "AddRef" else -1
            finish(self.roots[arg(0)], 4)
        elif name == "EnumDevices":
            self.events.append(("enumerate", arg(1), arg(4)))
            # No external controllers in this test. The system keyboard and mouse
            # are created through their well-known GUIDs, without enumeration.
            finish(0, 20)
        elif name == "CreateDevice":
            guid = bytes(self.boot.cpu.mem_read(arg(1), 16))
            kind = {KEYBOARD: "keyboard", MOUSE: "mouse"}.get(guid)
            self.events.append(("create", kind or guid.hex()))
            if not kind:
                finish(0x80040154, 16)
            else:
                device = self.boot.d3d.make_interface("IDirectInputDevice8", DEVICE_METHODS)
                self.devices[device] = {"kind": kind, "acquired": False}
                self.boot.put(arg(2), device)
                finish(0, 16)
        else:
            return False
        return True

    def device_call(self, name, arg, finish):
        device = self.devices[arg(0)]
        if name in ("Acquire", "Unacquire"):
            device["acquired"] = name == "Acquire"
            finish(0, 4)
        elif name == "SetDataFormat":
            device["data_size"] = self.boot.get(arg(1) + 12)
            finish(0, 8)
        elif name in ("SetProperty", "SetCooperativeLevel"):
            finish(0, 12)
        elif name == "GetDeviceState" and device["acquired"] and arg(1) <= 4096:
            self.boot.cpu.mem_write(arg(2), bytes(arg(1)))
            finish(0, 12)
        elif name == "GetDeviceData" and device["acquired"]:
            self.boot.put(arg(3), 0)
            finish(0, 20)
        elif name == "Poll" and device["acquired"]:
            finish(0, 4)
        elif name == "Release":
            del self.devices[arg(0)]
            finish(0, 4)
        else:
            return False
        return True
