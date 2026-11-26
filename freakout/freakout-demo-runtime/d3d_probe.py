"""Instrument the demo's D3D9 COM calls in a renderer-free startup trace."""
import hashlib
import struct

from dx9_programs import Dx9Programs
from dx9_formats import BLOCK_BYTES, PIXEL_BYTES

ROOT_METHODS = (
    "QueryInterface", "AddRef", "Release", "RegisterSoftwareDevice",
    "GetAdapterCount", "GetAdapterIdentifier", "GetAdapterModeCount",
    "EnumAdapterModes", "GetAdapterDisplayMode", "CheckDeviceType",
    "CheckDeviceFormat", "CheckDeviceMultiSampleType", "CheckDepthStencilMatch",
    "CheckDeviceFormatConversion", "GetDeviceCaps", "GetAdapterMonitor", "CreateDevice",
)
DEVICE_METHODS = (
    'QueryInterface', 'AddRef', 'Release', 'TestCooperativeLevel',
    'GetAvailableTextureMem', 'EvictManagedResources', 'GetDirect3D', 'GetDeviceCaps',
    'GetDisplayMode', 'GetCreationParameters', 'SetCursorProperties', 'SetCursorPosition',
    'ShowCursor', 'CreateAdditionalSwapChain', 'GetSwapChain', 'GetNumberOfSwapChains',
    'Reset', 'Present', 'GetBackBuffer', 'GetRasterStatus',
    'SetDialogBoxMode', 'SetGammaRamp', 'GetGammaRamp', 'CreateTexture',
    'CreateVolumeTexture', 'CreateCubeTexture', 'CreateVertexBuffer', 'CreateIndexBuffer',
    'CreateRenderTarget', 'CreateDepthStencilSurface', 'UpdateSurface', 'UpdateTexture',
    'GetRenderTargetData', 'GetFrontBufferData', 'StretchRect', 'ColorFill',
    'CreateOffscreenPlainSurface', 'SetRenderTarget', 'GetRenderTarget', 'SetDepthStencilSurface',
    'GetDepthStencilSurface', 'BeginScene', 'EndScene', 'Clear',
    'SetTransform', 'GetTransform', 'MultiplyTransform', 'SetViewport',
    'GetViewport', 'SetMaterial', 'GetMaterial', 'SetLight',
    'GetLight', 'LightEnable', 'GetLightEnable', 'SetClipPlane',
    'GetClipPlane', 'SetRenderState', 'GetRenderState', 'CreateStateBlock',
    'BeginStateBlock', 'EndStateBlock', 'SetClipStatus', 'GetClipStatus',
    'GetTexture', 'SetTexture', 'GetTextureStageState', 'SetTextureStageState',
    'GetSamplerState', 'SetSamplerState', 'ValidateDevice', 'SetPaletteEntries',
    'GetPaletteEntries', 'SetCurrentTexturePalette', 'GetCurrentTexturePalette', 'SetScissorRect',
    'GetScissorRect', 'SetSoftwareVertexProcessing', 'GetSoftwareVertexProcessing', 'SetNPatchMode',
    'GetNPatchMode', 'DrawPrimitive', 'DrawIndexedPrimitive', 'DrawPrimitiveUP',
    'DrawIndexedPrimitiveUP', 'ProcessVertices', 'CreateVertexDeclaration', 'SetVertexDeclaration',
    'GetVertexDeclaration', 'SetFVF', 'GetFVF', 'CreateVertexShader',
    'SetVertexShader', 'GetVertexShader', 'SetVertexShaderConstantF', 'GetVertexShaderConstantF',
    'SetVertexShaderConstantI', 'GetVertexShaderConstantI', 'SetVertexShaderConstantB', 'GetVertexShaderConstantB',
    'SetStreamSource', 'GetStreamSource', 'SetStreamSourceFreq', 'GetStreamSourceFreq',
    'SetIndices', 'GetIndices', 'CreatePixelShader', 'SetPixelShader',
    'GetPixelShader', 'SetPixelShaderConstantF', 'GetPixelShaderConstantF', 'SetPixelShaderConstantI',
    'GetPixelShaderConstantI', 'SetPixelShaderConstantB', 'GetPixelShaderConstantB', 'DrawRectPatch',
    'DrawTriPatch', 'DeletePatch', 'CreateQuery',
)
SURFACE_METHODS = (
    "QueryInterface", "AddRef", "Release", "GetDevice", "SetPrivateData", "GetPrivateData",
    "FreePrivateData", "SetPriority", "GetPriority", "PreLoad", "GetType",
    "GetContainer", "GetDesc", "LockRect", "UnlockRect", "GetDC", "ReleaseDC",
)
TEXTURE_METHODS = (
    "QueryInterface", "AddRef", "Release", "GetDevice", "SetPrivateData", "GetPrivateData",
    "FreePrivateData", "SetPriority", "GetPriority", "PreLoad", "GetType", "SetLOD",
    "GetLOD", "GetLevelCount", "SetAutoGenFilterType", "GetAutoGenFilterType",
    "GenerateMipSubLevels", "GetLevelDesc", "GetSurfaceLevel", "LockRect",
    "UnlockRect", "AddDirtyRect",
)
BUFFER_METHODS = (
    "QueryInterface", "AddRef", "Release", "GetDevice", "SetPrivateData", "GetPrivateData",
    "FreePrivateData", "SetPriority", "GetPriority", "PreLoad", "GetType", "Lock", "Unlock", "GetDesc",
)
MODULE = 0x2D390001
ADAPTER_MODE = (1024, 768, 60, 22)  # X8R8G8B8
S_OK = 0
NOT_AVAILABLE = 0x8876086A
INVALID_CALL = 0x8876086C
PRIMITIVE_COUNTS = {1: (1, 0), 2: (2, 0), 3: (1, 1), 4: (3, 0), 5: (1, 2), 6: (1, 2)}


class D3DProbe:
    def __init__(self, boot):
        self.boot = boot
        self.events = []
        self.module_seen = False
        self.root = 0
        self.root_refs = 0
        self.device = 0
        self.device_refs = 0
        self.device_parameters = None
        self.device_state = {}
        self.scene = False
        self.present_count = 0
        self.draw_count = 0
        self.drawn_frames = 0
        self.last_present_draw_count = 0
        self.draw_sink = None
        self.clear_sink = None
        self.frame_sink = None
        self.surfaces = {}
        self.buffers = {}
        self.buffer_upload_count = 0
        self.textures = {}
        self.texture_storage = {}
        self.texture_revisions = {}
        self.texture_locks = set()
        self.uploads = []
        self.texture_surfaces = {}
        self.surface_levels = {}
        self.targets = {}
        self.backbuffer = 0
        self.depth = 0
        self.vtables = None
        self.programs = Dx9Programs(self)

    def stop(self, status):
        self.boot.status = status
        self.boot.cpu.emu_stop()

    def make_interface(self, interface, methods):
        instance = self.boot.allocate(4)
        key = (interface, methods)
        table = self.vtables.get(key) if self.vtables is not None else None
        if not table:
            table = self.boot.allocate(4 * len(methods))
            for index, method in enumerate(methods):
                self.boot.put(table + index * 4, self.boot.add_host_api(interface, method))
            if self.vtables is not None:
                self.vtables[key] = table
        self.boot.put(instance, table)
        return instance

    def handle(self, dll, name, arg, finish):
        if self.programs.handle(dll, name, arg, finish):
            return True
        if name == "LoadLibraryA":
            if self.boot.c_string(arg(0)).lower() != b"d3d9.dll":
                return False
            self.module_seen = True
            self.events.append(("LoadLibraryA", "d3d9.dll"))
            finish(MODULE, 4)
        elif name == "GetProcAddress" and arg(0) == MODULE:
            symbol = self.boot.c_string(arg(1))
            self.events.append(("GetProcAddress", symbol.decode("ascii", "replace")))
            address = self.boot.add_host_api("d3d9.dll", "Direct3DCreate9") if symbol == b"Direct3DCreate9" else 0
            finish(address, 8)
        elif name == "Direct3DCreate9" and self.module_seen:
            self.events.append(("Direct3DCreate9", arg(0)))
            if arg(0) != 32:
                finish(0, 4)
                return True
            if not self.root:
                self.root = self.make_interface("IDirect3D9", ROOT_METHODS)
            self.root_refs += 1
            finish(self.root, 4)
        elif dll == "IDirect3DDevice9":
            self.events.append((dll, name))
            self.handle_device(name, arg, finish)
        elif dll == "IDirect3DTexture9":
            self.events.append((dll, name))
            self.handle_texture(name, arg, finish)
        elif dll in ("IDirect3DVertexBuffer9", "IDirect3DIndexBuffer9"):
            self.events.append((dll, name))
            self.handle_buffer(name, arg, finish)
        elif dll == "IDirect3DSurface9":
            self.events.append((dll, name))
            self.handle_surface(name, arg, finish)
        elif dll == "IDirect3D9":
            self.events.append((dll, name))
            self.handle_root(name, arg, finish)
        else:
            return False
        return True

    def write_caps(self, address, adapter, device_type):
        caps = [0] * 76
        values = {
            0: device_type, 1: adapter, 5: 1, 7: 0x10000,
            22: 4096, 23: 4096, 24: 256, 27: 16, 36: 8, 37: 8, 38: 8,
            45: 0xFFFF, 46: 0xFFFF, 47: 16, 48: 256,
            49: 0xFFFE0200, 50: 256, 51: 0xFFFF0200, 58: 1, 60: 1,
        }
        for slot, value in values.items():
            caps[slot] = value
        self.boot.cpu.mem_write(address, struct.pack("<76I", *caps))

    def new_surface(self, width, height, fmt, usage, samples=0, quality=0):
        handle = self.make_interface("IDirect3DSurface9", SURFACE_METHODS)
        self.surfaces[handle] = [(fmt, 1, usage, 0, samples, quality, width, height), 1]
        return handle

    def handle_texture(self, name, arg, finish):
        texture = self.textures.get(arg(0))
        if texture is None:
            self.stop(f"invalid texture at {name}")
            return
        width, height, levels, usage, fmt, pool = texture[0]
        if name in ("AddRef", "Release"):
            texture[1] += 1 if name == "AddRef" else -1
            finish(texture[1], 4)
        elif name == "GetType":
            finish(3, 4)
        elif name == "GetLevelCount":
            finish(levels, 4)
        elif name == "GetLevelDesc" and arg(1) < levels:
            level = arg(1)
            desc = (fmt, 1, usage, pool, 0, 0, max(1, width >> level), max(1, height >> level))
            self.boot.cpu.mem_write(arg(2), struct.pack("<8I", *desc))
            finish(S_OK, 12)
        elif name == "GetSurfaceLevel" and arg(1) < levels:
            key = (arg(0), arg(1))
            if key not in self.texture_surfaces:
                surface = self.new_surface(max(1, width >> arg(1)), max(1, height >> arg(1)), fmt, usage)
                self.texture_surfaces[key] = surface
                self.surface_levels[surface] = key
                desc = list(self.surfaces[surface][0])
                desc[3] = pool
                self.surfaces[surface][0] = tuple(desc)
            self.return_surface(self.texture_surfaces[key], arg(2), 12, finish)
        elif name == "LockRect" and arg(1) < levels:
            key = (arg(0), arg(1))
            self.lock_rect(key, fmt, max(1, width >> arg(1)), max(1, height >> arg(1)),
                           arg(2), arg(3), 20, finish)
        elif name == "UnlockRect" and (arg(0), arg(1)) in self.texture_locks:
            self.unlock_rect((arg(0), arg(1)), 8, finish)
        else:
            self.stop(f"texture boundary: IDirect3DTexture9::{name}")

    def lock_rect(self, key, fmt, width, height, output, rectangle, pop, finish):
        pixel_size = PIXEL_BYTES.get(fmt)
        block_size = BLOCK_BYTES.get(fmt)
        if not pixel_size and not block_size:
            self.stop(f"texture lock format {fmt}")
            return
        left, top, right, bottom = (
            struct.unpack("<4i", self.boot.cpu.mem_read(rectangle, 16))
            if rectangle else (0, 0, width, height)
        )
        if key in self.texture_locks or not (0 <= left < right <= width and 0 <= top < bottom <= height):
            finish(INVALID_CALL, pop)
            return
        if block_size:
            if (left % 4 or top % 4 or (right % 4 and right != width)
                    or (bottom % 4 and bottom != height)):
                finish(INVALID_CALL, pop)
                return
            pitch = ((width + 3) // 4) * block_size
            rows = (height + 3) // 4
            offset = (top // 4) * pitch + (left // 4) * block_size
        else:
            pitch = (width * pixel_size + 3) & ~3
            rows = height
            offset = top * pitch + left * pixel_size
        if key not in self.texture_storage:
            size = pitch * rows
            self.texture_storage[key] = (self.boot.allocate(size), size, fmt)
        address, _, _ = self.texture_storage[key]
        self.boot.cpu.mem_write(output, struct.pack("<2I", pitch, address + offset))
        self.texture_locks.add(key)
        finish(S_OK, pop)

    def unlock_rect(self, key, pop, finish):
        self.texture_locks.remove(key)
        self.texture_revisions[key[0]] = self.texture_revisions.get(key[0], 0) + 1
        address, size, fmt = self.texture_storage[key]
        if fmt in BLOCK_BYTES:
            data = self.boot.cpu.mem_read(address, size)
            self.uploads.append((fmt.to_bytes(4, "little").decode("ascii"), key[1], size,
                                 hashlib.sha256(data).hexdigest()))
            limit = self.boot.compressed_upload_limit
            if limit and len(self.uploads) >= limit:
                self.stop(f"texture checkpoint: {len(self.uploads)} compressed mip uploads")
                return
        finish(S_OK, pop)

    def handle_surface(self, name, arg, finish):
        surface = self.surfaces.get(arg(0))
        if surface is None:
            self.stop(f"invalid surface at {name}")
        elif name == "GetDesc":
            self.boot.cpu.mem_write(arg(1), struct.pack("<8I", *surface[0]))
            finish(S_OK, 8)
        elif name == "LockRect":
            fmt, _, _, _, _, _, width, height = surface[0]
            key = self.surface_levels.get(arg(0), (arg(0), 0))
            self.lock_rect(key, fmt, width, height, arg(1), arg(2), 16, finish)
        elif name == "UnlockRect" and self.surface_levels.get(arg(0), (arg(0), 0)) in self.texture_locks:
            self.unlock_rect(self.surface_levels.get(arg(0), (arg(0), 0)), 4, finish)
        elif name == "GetDevice":
            self.device_refs += 1
            self.boot.put(arg(1), self.device)
            finish(S_OK, 8)
        elif name in ("AddRef", "Release"):
            surface[1] += 1 if name == "AddRef" else -1
            finish(surface[1], 4)
        else:
            self.stop(f"surface boundary: IDirect3DSurface9::{name}")

    def return_surface(self, handle, output, pop, finish):
        if not handle:
            finish(INVALID_CALL, pop)
            return
        self.surfaces[handle][1] += 1
        self.boot.put(output, handle)
        finish(S_OK, pop)

    def create_buffer(self, name, arg, finish):
        kind = 6 if name == "CreateVertexBuffer" else 7
        length, usage, fmt, pool = (arg(i) for i in range(1, 5))
        self.events.append((name, length, usage, fmt, pool))
        if not length or not arg(5):
            finish(INVALID_CALL, 28)
            return
        interface = "IDirect3DVertexBuffer9" if kind == 6 else "IDirect3DIndexBuffer9"
        handle = self.make_interface(interface, BUFFER_METHODS)
        self.buffers[handle] = {
            "data": self.boot.allocate(length), "length": length, "usage": usage,
            "format": fmt, "pool": pool, "kind": kind, "locked": None, "refs": 1,
        }
        self.boot.put(arg(5), handle)
        finish(S_OK, 28)

    def handle_buffer(self, name, arg, finish):
        buffer = self.buffers.get(arg(0))
        if buffer is None:
            self.stop(f"invalid D3D9 buffer at {name}")
        elif name == "Lock":
            offset = arg(1)
            size = arg(2) or buffer["length"] - offset
            if buffer["locked"] is not None or not 0 <= offset <= offset + size <= buffer["length"]:
                finish(INVALID_CALL, 20)
            else:
                buffer["locked"] = (offset, size)
                self.boot.put(arg(3), buffer["data"] + offset)
                finish(S_OK, 20)
        elif name == "Unlock" and buffer["locked"] is not None:
            offset, size = buffer["locked"]
            bytes_written = self.boot.cpu.mem_read(buffer["data"] + offset, size)
            self.events.append(("buffer data", buffer["kind"], offset, size,
                                hashlib.sha256(bytes_written).hexdigest()))
            buffer["locked"] = None
            self.buffer_upload_count += 1
            limit = self.boot.buffer_upload_limit
            if limit and self.buffer_upload_count >= limit:
                self.stop(f"geometry checkpoint: {self.buffer_upload_count} vertex/index buffer uploads")
            else:
                finish(S_OK, 4)
        elif name in ("AddRef", "Release"):
            buffer["refs"] += 1 if name == "AddRef" else -1
            finish(buffer["refs"], 4)
        elif name == "GetType":
            finish(buffer["kind"], 4)
        elif name == "GetDesc":
            fmt = 100 if buffer["kind"] == 6 else buffer["format"]
            desc = (fmt, buffer["kind"], buffer["usage"], buffer["pool"], buffer["length"])
            if buffer["kind"] == 6:
                desc += (buffer["format"],)
            self.boot.cpu.mem_write(arg(1), struct.pack(f"<{len(desc)}I", *desc))
            finish(S_OK, 8)
        else:
            self.stop(f"buffer boundary: {name}")

    def capture_phase(self):
        return not self.boot.draw_after_worker or self.boot.thread.workers_reaped()

    def draw(self, name, arg, finish):
        argument_count = {"DrawPrimitive": 3, "DrawIndexedPrimitive": 6,
                          "DrawPrimitiveUP": 4, "DrawIndexedPrimitiveUP": 8}[name]
        args = tuple(arg(i) for i in range(1, argument_count + 1))
        self.events.append(("draw", name, args))
        self.draw_count += 1
        self.record_geometry(name, args)
        if self.boot.frame_after_draw:
            finish(S_OK, (1 + argument_count) * 4)
        else:
            self.stop(f"draw checkpoint: IDirect3DDevice9::{name}")

    def record_geometry(self, name, args):
        state = self.device_state
        stream = state.get(("SetStreamSource", 0))
        index_handle = state.get("SetIndices")
        texture_handle = state.get(("SetTexture", 0))
        vertex_digest = None
        index_digest = None
        stride = None
        primitive_type = args[0]
        if name == "DrawIndexedPrimitive" and stream:
            step, extra = PRIMITIVE_COUNTS[primitive_type]
            _, base, minimum, vertices, start_index, primitives = args
            base = struct.unpack("<i", struct.pack("<I", base))[0]
            vertex_handle, offset, stride = stream
            index_size = {101: 2, 102: 4}[self.buffers[index_handle]["format"]]
            index_data = self.read_buffer(index_handle, start_index * index_size,
                                          (primitives * step + extra) * index_size)
            vertex_data = self.read_buffer(vertex_handle, offset + (base + minimum) * stride,
                                           vertices * stride)
            index_digest = hashlib.sha256(index_data).hexdigest()
            vertex_digest = hashlib.sha256(vertex_data).hexdigest()
            if self.draw_sink:
                self.draw_sink(self, args, stride, vertex_data, index_size, index_data)
        elif name == "DrawPrimitive" and stream and self.draw_sink:
            _, first_vertex, primitives = args
            step, extra = PRIMITIVE_COUNTS[primitive_type]
            vertex_count = primitives * step + extra
            vertex_handle, offset, stride = stream
            vertex_data = self.read_buffer(vertex_handle, offset + first_vertex * stride,
                                           vertex_count * stride)
            index_size = 2 if vertex_count <= 65536 else 4
            index_format = "H" if index_size == 2 else "I"
            index_data = struct.pack(f"<{vertex_count}{index_format}", *range(vertex_count))
            draw_args = (primitive_type, 0, 0, vertex_count, 0, primitives)
            self.draw_sink(self, draw_args, stride, vertex_data, index_size, index_data)
        elif self.draw_sink:
            raise ValueError(f"unsupported replay draw: {name}")
        texture = self.textures.get(texture_handle)
        self.events.append(("geometry", stride, state.get("SetFVF"), vertex_digest, index_digest,
                            texture[0][:5] if texture else None))

    def read_buffer(self, handle, offset, length):
        buffer = self.buffers[handle]
        if not 0 <= offset <= offset + length <= buffer["length"]:
            raise ValueError("draw reads past a D3D9 buffer")
        return bytes(self.boot.cpu.mem_read(buffer["data"] + offset, length))

    def handle_device(self, name, arg, finish):
        if arg(0) != self.device:
            self.stop(f"invalid IDirect3DDevice9 this pointer at {name}")
        elif name == "AddRef":
            self.device_refs += 1
            finish(self.device_refs, 4)
        elif name == "Release":
            self.device_refs -= 1
            finish(self.device_refs, 4)
        elif name == "GetDeviceCaps":
            self.write_caps(arg(1), 0, 1)
            finish(S_OK, 8)
        elif name == "GetDisplayMode" and arg(1) == 0:
            self.boot.cpu.mem_write(arg(2), struct.pack("<4I", *ADAPTER_MODE))
            finish(S_OK, 12)
        elif name == "GetRenderTarget":
            self.return_surface(self.targets.get(arg(1)), arg(2), 12, finish)
        elif name == "GetBackBuffer" and arg(1) == 0 and arg(2) == 0:
            self.return_surface(self.targets.get(0), arg(4), 20, finish)
        elif name == "GetDepthStencilSurface":
            self.return_surface(self.depth, arg(1), 8, finish)
        elif name == "SetDepthStencilSurface" and (not arg(1) or arg(1) in self.surfaces):
            self.depth = arg(1)
            finish(S_OK, 8)
        elif name == "SetRenderTarget" and (not arg(2) or arg(2) in self.surfaces):
            self.targets[arg(1)] = arg(2)
            finish(S_OK, 12)
        elif name in ("CreateVertexBuffer", "CreateIndexBuffer"):
            self.create_buffer(name, arg, finish)
        elif name in ("DrawPrimitive", "DrawIndexedPrimitive", "DrawPrimitiveUP", "DrawIndexedPrimitiveUP"):
            self.draw(name, arg, finish)
        elif name == "CreateTexture":
            width, height, levels, usage, fmt, pool = (arg(i) for i in range(1, 7))
            self.events.append(("CreateTexture", width, height, levels, usage, fmt, pool))
            handle = self.make_interface("IDirect3DTexture9", TEXTURE_METHODS)
            self.textures[handle] = [(width, height, levels or max(width, height).bit_length(),
                                      usage, fmt, pool), 1]
            self.boot.put(arg(7), handle)
            finish(S_OK, 36)
        elif name == "CreateRenderTarget":
            surface = self.new_surface(arg(1), arg(2), arg(3), 1, arg(4), arg(5))
            self.boot.put(arg(7), surface)
            finish(S_OK, 36)
        elif name == "CreateDepthStencilSurface":
            surface = self.new_surface(arg(1), arg(2), arg(3), 2, arg(4), arg(5))
            self.boot.put(arg(7), surface)
            finish(S_OK, 36)
        elif name == "GetAvailableTextureMem":
            finish(128 * 1024 * 1024, 4)
        elif name == "TestCooperativeLevel":
            finish(S_OK, 4)
        elif name in ("CreateVertexDeclaration", "CreateVertexShader", "CreatePixelShader"):
            self.programs.create_program(name[6:], arg, finish)
        elif name in ("SetVertexShader", "SetPixelShader", "SetVertexDeclaration"):
            self.programs.set_program(name[3:], arg(1), finish)
        elif name in ("GetVertexShader", "GetPixelShader", "GetVertexDeclaration"):
            if not arg(1):
                finish(INVALID_CALL, 8)
            else:
                handle = self.programs.bindings.get(name[3:], 0)
                if handle:
                    self.programs.resources[handle].refs += 1
                self.boot.put(arg(1), handle)
                finish(S_OK, 8)
        elif name in ("SetVertexShaderConstantF", "SetVertexShaderConstantI", "SetVertexShaderConstantB",
                      "SetPixelShaderConstantF", "SetPixelShaderConstantI", "SetPixelShaderConstantB"):
            self.programs.set_constants(name, arg, finish)
        elif name in ("SetIndices", "SetFVF"):
            if name == "SetFVF":
                self.programs.unbind("VertexDeclaration")
                self.device_state.pop("SetVertexDeclaration", None)
            self.device_state[name] = arg(1)
            finish(S_OK, 8)
        elif name in ("SetRenderState", "SetTexture"):
            self.device_state[name, arg(1)] = arg(2)
            finish(S_OK, 12)
        elif name in ("SetSamplerState", "SetTextureStageState"):
            self.device_state[name, arg(1), arg(2)] = arg(3)
            finish(S_OK, 16)
        elif name == "SetStreamSource":
            self.device_state[name, arg(1)] = (arg(2), arg(3), arg(4))
            finish(S_OK, 20)
        elif name == "GetRenderState" and ("SetRenderState", arg(1)) in self.device_state:
            self.boot.put(arg(2), self.device_state["SetRenderState", arg(1)])
            finish(S_OK, 12)
        elif name == "SetTransform":
            self.device_state[name, arg(1)] = bytes(self.boot.cpu.mem_read(arg(2), 64))
            finish(S_OK, 12)
        elif name == "SetViewport":
            self.device_state[name] = bytes(self.boot.cpu.mem_read(arg(1), 24))
            finish(S_OK, 8)
        elif name == "GetViewport":
            viewport = self.device_state.get("SetViewport")
            if viewport is None:
                viewport = struct.pack("<4I2f", 0, 0, *self.device_parameters[:2], 0.0, 1.0)
            self.boot.cpu.mem_write(arg(1), viewport)
            finish(S_OK, 8)
        elif name in ("SetScissorRect", "SetMaterial"):
            size = 16 if name == "SetScissorRect" else 68
            self.device_state[name] = bytes(self.boot.cpu.mem_read(arg(1), size))
            finish(S_OK, 8)
        elif name == "SetLight":
            self.device_state[name, arg(1)] = bytes(self.boot.cpu.mem_read(arg(2), 104))
            finish(S_OK, 12)
        elif name == "LightEnable":
            self.device_state[name, arg(1)] = bool(arg(2))
            finish(S_OK, 12)
        elif name == "BeginScene" and not self.scene:
            self.scene = True
            finish(S_OK, 4)
        elif name == "EndScene" and self.scene:
            self.scene = False
            finish(S_OK, 4)
        elif name == "Clear":
            count, address = arg(1), arg(2)
            if count > 64 or (count and not address):
                self.stop("unsupported Clear rectangle list")
                return
            rectangles = tuple(struct.iter_unpack("<4i", self.boot.cpu.mem_read(address, count * 16))) if count else ()
            event = (rectangles, *(arg(i) for i in range(3, 7)))
            self.events.append(("Clear", *event))
            if self.clear_sink:
                self.clear_sink(self, event)
            finish(S_OK, 28)
        elif name == "Present":
            self.present_count += 1
            new_draws = self.draw_count - self.last_present_draw_count
            self.last_present_draw_count = self.draw_count
            if self.frame_sink:
                self.frame_sink(self, self.present_count, new_draws)
            if self.boot.frame_after_draw and new_draws:
                self.events.append(("drawn frame", self.present_count, self.draw_count))
                if self.capture_phase():
                    self.drawn_frames += 1
                if self.drawn_frames == self.boot.drawn_frame_limit:
                    self.stop(f"drawn frame checkpoint: Present #{self.present_count}, {self.draw_count} draws")
                    return
            if self.present_count >= self.boot.present_limit:
                self.stop("frame boundary: IDirect3DDevice9::Present")
            else:
                finish(S_OK, 20)
        else:
            self.stop(f"device method boundary: IDirect3DDevice9::{name}")

    def handle_root(self, name, arg, finish):
        if arg(0) != self.root:
            self.stop(f"invalid IDirect3D9 this pointer at {name}")
            return
        if name == "AddRef":
            self.root_refs += 1
            finish(self.root_refs, 4)
        elif name == "Release":
            self.root_refs -= 1
            finish(self.root_refs, 4)
        elif name == "GetAdapterCount":
            finish(1, 4)
        elif name == "GetAdapterIdentifier":
            info = bytearray(1100)
            for offset, text in ((0, b"trace-d3d9"), (512, b"Diagnostic adapter"), (1024, b"DISPLAY1")):
                info[offset:offset + len(text)] = text
            self.boot.cpu.mem_write(arg(3), bytes(info))
            finish(S_OK if arg(1) == 0 else INVALID_CALL, 16)
        elif name == "GetDeviceCaps":
            self.write_caps(arg(3), arg(1), arg(2))
            finish(S_OK if arg(1) == 0 and arg(2) == 1 else NOT_AVAILABLE, 16)
        elif name == "GetAdapterModeCount":
            finish(int(arg(1) == 0 and arg(2) in (21, 22)), 12)
        elif name in ("EnumAdapterModes", "GetAdapterDisplayMode"):
            native_display = name == "GetAdapterDisplayMode"
            output = arg(2) if native_display else arg(4)
            if arg(1) == 0 and (native_display or (arg(2) in (21, 22) and arg(3) == 0)):
                mode = ADAPTER_MODE if native_display else (*ADAPTER_MODE[:3], arg(2))
                self.boot.cpu.mem_write(output, struct.pack("<4I", *mode))
                finish(S_OK, 12 if native_display else 20)
            else:
                finish(INVALID_CALL, 12 if native_display else 20)
        elif name in ("CheckDeviceType", "CheckDeviceFormat", "CheckDepthStencilMatch",
                      "CheckDeviceFormatConversion"):
            count = {"CheckDeviceType": 6, "CheckDeviceFormat": 7, "CheckDepthStencilMatch": 6,
                     "CheckDeviceFormatConversion": 5}[name]
            self.events.append((name, tuple(arg(i) for i in range(1, count))))
            finish(S_OK if arg(1) == 0 and arg(2) == 1 else NOT_AVAILABLE, count * 4)
        elif name == "CheckDeviceMultiSampleType":
            if arg(6):
                self.boot.put(arg(6), int(arg(5) == 0))
            finish(S_OK if arg(5) == 0 else NOT_AVAILABLE, 28)
        elif name == "CreateDevice":
            fields = struct.unpack("<14I", self.boot.cpu.mem_read(arg(5), 56))
            self.events.append(("CreateDevice", fields, arg(4)))
            width, height, format_id = fields[:3]
            if self.boot.probe_frame:
                self.device_parameters = fields
                self.device = self.make_interface("IDirect3DDevice9", DEVICE_METHODS)
                self.device_refs = 1
                self.backbuffer = self.new_surface(width, height, format_id, 1, fields[4], fields[5])
                self.targets = {0: self.backbuffer}
                self.depth = (self.new_surface(width, height, fields[10], 2, fields[4], fields[5])
                              if fields[9] else 0)
                self.boot.put(arg(6), self.device)
                finish(S_OK, 28)
            else:
                self.stop(f"device boundary: CreateDevice({width}x{height}, format={format_id})")
        else:
            self.stop(f"Direct3D boundary: IDirect3D9::{name}")
