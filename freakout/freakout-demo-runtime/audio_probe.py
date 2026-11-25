"""FMOD 3.74 sample-bank handling for the host probe, with silent output."""
import math
import struct

from fsb_bank import parse_bank
from unicorn.x86_const import UC_X86_REG_FP0, UC_X86_REG_FPSW, UC_X86_REG_FPTAG

DRIVER_ADDRESS = 0xA00700
DRIVER = b"FreakOut diagnostic audio\0"
VERSION = struct.unpack("<f", struct.pack("<f", 3.74))[0]
LOOP_MODE_MASK = 0x7
FSOUND_2D = 0x2000


def push_float(cpu, value):
    # The x86 return ABI puts a float in ST(0), using physical x87 registers.
    if hasattr(cpu, "push_guest_float"):
        cpu.push_guest_float(value)
        return
    if not math.isfinite(value):
        raise ValueError("nonfinite x87 return in startup probe")
    mantissa, exponent = math.frexp(abs(value))
    sign = 0x8000 if math.copysign(1, value) < 0 else 0
    exponent = (exponent + 16382 if mantissa else 0) | sign
    status = cpu.reg_read(UC_X86_REG_FPSW)
    top = ((status >> 11) - 1) & 7
    tags = cpu.reg_read(UC_X86_REG_FPTAG)
    cpu.reg_write(UC_X86_REG_FP0 + top, (int(mantissa * (1 << 64)), exponent))
    tag = 0 if mantissa else 1
    cpu.reg_write(UC_X86_REG_FPTAG, (tags & ~(3 << (2 * top))) | (tag << (2 * top)))
    cpu.reg_write(UC_X86_REG_FPSW, (status & ~0x3800) | (top << 11))


def pop_float(cpu):
    if hasattr(cpu, "pop_guest_float"):
        return cpu.pop_guest_float()
    status = cpu.reg_read(UC_X86_REG_FPSW)
    top = (status >> 11) & 7
    tags = cpu.reg_read(UC_X86_REG_FPTAG)
    if ((tags >> (top * 2)) & 3) == 3:
        raise ValueError("empty x87 stack on math import")
    mantissa, exponent = cpu.reg_read(UC_X86_REG_FP0 + top)
    value = math.ldexp(mantissa / (1 << 63), (exponent & 0x7FFF) - 16383) if mantissa else 0.0
    cpu.reg_write(UC_X86_REG_FPTAG, tags | (3 << (2 * top)))
    cpu.reg_write(UC_X86_REG_FPSW, (status & ~0x3800) | (((top + 1) & 7) << 11))
    return -value if exponent & 0x8000 else value


class AudioProbe:
    def __init__(self, boot):
        self.boot = boot
        self.output = 0
        self.driver = 0
        self.channels = 0
        self.events = []
        self.banks = {}
        self.samples = {}
        self.sample_modes = {}
        self.payload_bytes = 0
        self.peak_payload_bytes = 0

    def change_payload(self, amount):
        self.payload_bytes += amount
        self.peak_payload_bytes = max(self.peak_payload_bytes, self.payload_bytes)

    def load_bank(self, pathname, mode=0):
        path = self.boot.asset_path(pathname)
        if path is None or not path.is_file():
            self.events.append(("bank missing", pathname))
            return 0
        # Read the complete bank before publishing its handle.
        with path.open("rb") as source:
            data = source.read(64 * 1024 * 1024 + 1)
        if len(data) > 64 * 1024 * 1024:
            raise ValueError("FSB bank exceeds the menu bank limit")
        samples = parse_bank(data)
        bank = self.boot.allocate(4)
        handles = tuple(self.boot.allocate(4) for _ in samples)
        self.banks[bank] = handles
        self.samples.update(zip(handles, samples))
        self.change_payload(sum(len(sample.payload) for sample in samples))
        loop_mode = mode & LOOP_MODE_MASK
        spatial_mode = mode & FSOUND_2D
        self.sample_modes.update(
            (handle, ((sample.mode & ~LOOP_MODE_MASK) | loop_mode if loop_mode else sample.mode) | spatial_mode)
            for handle, sample in zip(handles, samples)
        )
        self.events.append(("bank loaded", pathname, len(samples)))
        return bank

    def free_bank(self, bank):
        handles = self.banks.pop(bank, None)
        if handles is None:
            return False
        for handle in handles:
            self.change_payload(-len(self.samples.pop(handle).payload))
            del self.sample_modes[handle]
        self.events.append(("bank freed", bank))
        return True

    def handle(self, name, arg, finish):
        if name == "_FSOUND_GetVersion@0":
            push_float(self.boot.cpu, VERSION)
            self.events.append(("version", "3.74"))
            finish()
        elif name == "_FSOUND_SetOutput@4":
            self.output = arg(0)
            self.events.append(("output", self.output))
            finish(1, 4)
        elif name == "_FSOUND_SetDriver@4":
            self.driver = arg(0)
            finish(int(self.driver == 0), 4)
        elif name == "_FSOUND_Init@12":
            rate, channels, flags = arg(0), arg(1), arg(2)
            self.events.append(("init", rate, channels, flags))
            self.channels = channels if rate and self.driver == 0 else 0
            finish(int(bool(self.channels)), 12)
        elif name == "_FSOUND_Close@0":
            for bank in tuple(self.banks):
                self.free_bank(bank)
            self.channels = 0
            finish()
        elif (name == "_FMUSIC_LoadSongEx@24" and (arg(3) & ~FSOUND_2D) in (0, 1, 2, 4)
              and not any(arg(i) for i in (1, 2, 4, 5))):
            pathname = self.boot.c_string(arg(0)).decode("ascii")
            finish(self.load_bank(pathname, arg(3)), 24)
        elif name == "_FMUSIC_GetNumSamples@4":
            finish(len(self.banks.get(arg(0), ())), 4)
        elif name == "_FMUSIC_GetSample@8":
            handles = self.banks.get(arg(0), ())
            index = arg(1)
            finish(handles[index] if index < len(handles) else 0, 8)
        elif name == "_FMUSIC_FreeSong@4":
            finish(int(self.free_bank(arg(0))), 4)
        elif name == "_FSOUND_Sample_SetMode@8":
            sample = arg(0)
            if sample not in self.samples:
                finish(0, 8)
            else:
                self.sample_modes[sample] = arg(1)
                finish(1, 8)
        elif name == "_FSOUND_GetNumDrivers@0":
            finish(1)
        elif name == "_FSOUND_GetDriverName@4":
            self.boot.cpu.mem_write(DRIVER_ADDRESS, DRIVER)
            finish(DRIVER_ADDRESS if arg(0) == 0 else 0, 4)
        elif name == "_FSOUND_GetDriver@0":
            finish(self.driver)
        elif name == "_FSOUND_GetOutputRate@0":
            finish(44100 if self.channels else 0)
        elif name == "_FSOUND_GetMemoryStats@8":
            for pointer, value in ((arg(0), self.payload_bytes), (arg(1), self.peak_payload_bytes)):
                if pointer:
                    self.boot.put(pointer, value)
            finish(0, 8)
        elif name == "_FSOUND_Update@0":
            finish()
        elif name in ("_FSOUND_SetSFXMasterVolume@4", "_FSOUND_3D_SetDistanceFactor@4",
                      "_FSOUND_3D_Listener_SetCurrent@8", "_FSOUND_3D_Listener_SetAttributes@32"):
            finish(pop=int(name.rsplit("@", 1)[1]))
        else:
            return False
        return True
