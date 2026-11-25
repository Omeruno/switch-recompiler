"""Export a drawn frame from the local FreakOut Windows demo."""
import argparse
import hashlib
import math
from pathlib import Path
import struct

from trace_boot import Boot, load_demo


DEFAULT_CLEAR = (0xFF000000, 0x3F800000)
DEPTH_RESET = 32
VERTEX_COLOR = 64


def frame_clear(events):
    """Extract the initial planes and depth clears between draws."""
    present = ("IDirect3DDevice9", "Present")
    presents = [index for index, event in enumerate(events) if event == present]
    start = presents[-2] + 1 if len(presents) >= 2 else 0
    draw_index = 0
    depth_resets = set()
    color = depth = None
    for event in events[start:presents[-1]]:
        if event[0] == "draw":
            draw_index += 1
        elif event[0] == "Clear":
            _, rectangles, flags, argb, depth_bits, _ = event
            if rectangles not in ((), ((0, 0, 1024, 768),)) or flags & ~7:
                raise ValueError("unsupported Clear placement or rectangle")
            if draw_index:
                if flags & 1 or (flags & 2 and depth_bits != DEFAULT_CLEAR[1]):
                    raise ValueError("unsupported color or depth value between draws")
                if flags & 2:
                    depth_resets.add(draw_index)
                continue
            if flags & 1:
                color = argb
            if flags & 2:
                depth = depth_bits
    if color is None or depth is None:
        raise ValueError("frame does not initialize both color and depth")
    # Depth cleared after the final draw is discarded at presentation.
    return (color, depth), depth_resets - {draw_index}


def transform(vector, matrix):
    return tuple(sum(vector[row] * matrix[row * 4 + col] for row in range(4))
                 for col in range(4))


class FrameExport:
    def __init__(self):
        self.textures = {}
        self.draws = []

    def capture(self, probe, args, stride, vertices, index_size, indices):
        state = probe.device_state
        render = lambda setting: state["SetRenderState", setting]
        stage = lambda setting: state["SetTextureStageState", 0, setting]
        sampler = lambda setting: state["SetSamplerState", 0, setting]
        primitive, _, minimum, vertex_count, _, primitive_count = args
        vertex_color = (state.get("SetFVF"), stride) == (0x152, 36) and render(137) == 0
        white_vertex = (state.get("SetFVF"), stride) == (0x112, 32) and render(137) == 1
        if ((not vertex_color and not white_vertex) or index_size != 2 or
                primitive not in (4, 5) or state.get("SetVertexShader") or state.get("SetPixelShader")):
            raise ValueError("unsupported fixed-function geometry")
        if (struct.unpack("<4I2f", state["SetViewport"]) != (0, 0, 1024, 768, 0.0, 1.0)
                or any(render(k) != v for k, v in {
                    7: 1, 19: 5, 20: 6, 22: 2, 23: 4, 25: 8, 28: 0, 52: 0,
                    136: 1, 168: 15, 171: 1, 174: 0, 194: 0, 206: 0,
                }.items())
                or [stage(k) for k in (1, 2, 3, 4, 5, 6)] != [4, 2, 0, 4, 2, 0]
                or state["SetTextureStageState", 1, 1] != 1
                or stage(24) not in (0, 2) or sampler(5) != 2 or sampler(6) != 2
                or sampler(7) not in (0, 2) or sampler(11) != 0
                or any(sampler(k) not in (1, 3) for k in (1, 2))):
            raise ValueError("unsupported render state in demo frame")
        material = struct.unpack("<17f", state["SetMaterial"])
        lights = [struct.unpack("<I25f", state["SetLight", key[1]])
                  for key, enabled in state.items()
                  if isinstance(key, tuple) and key[0] == "LightEnable" and enabled]
        # In the captured UI draws, white material and saturated ambient
        # lighting make the fixed-function diffuse modulation exactly white.
        if white_vertex and (material[:8] != (1.0,) * 8 or render(29) != 0 or not lights
                or any(light[0] != 3 or min(light[1:4] + light[9:12]) < 0 for light in lights)
                or any(sum(light[channel] for light in lights) < 1.0 for channel in range(9, 12))
                or min(material[12:15]) < 0):
            raise ValueError("frame replay requires white ambient and diffuse modulation")

        texture = state["SetTexture", 0]
        if texture not in self.textures:
            width, height, level_count, _, fmt, _ = probe.textures[texture][0]
            levels = []
            for level in range(level_count):
                payload = probe.texture_storage.get((texture, level))
                if payload is None:
                    break
                address, size, _ = payload
                levels.append(bytes(probe.boot.cpu.mem_read(address, size)))
            if not levels or fmt not in (21, 0x31545844, 0x35545844):
                raise ValueError("unsupported texture or missing top mip")
            header = struct.pack("<4I", width, height, fmt, len(levels))
            self.textures[texture] = (len(self.textures), header + b"".join(levels))

        matrices = [struct.unpack("<16f", state["SetTransform", n]) for n in (256, 2, 3)]
        uv_matrix = struct.unpack("<16f", state["SetTransform", 16]) if stage(24) else None
        vertex_data = bytearray()
        layout = '<6fI2f' if vertex_color else '<8f'
        for row in struct.iter_unpack(layout, vertices):
            x, y, z, *_ = row
            u, v = row[-2:]
            position = (x, y, z, 1.0)
            for matrix in matrices:
                position = transform(position, matrix)
            if uv_matrix:
                u, v = transform((u, v, 1.0, 0.0), uv_matrix)[:2]
            values = (*position, u, v)
            if not all(map(math.isfinite, values)):
                raise ValueError("non-finite coordinate in demo frame")
            vertex_data.extend(struct.pack("<6f", *values))
            if vertex_color:
                argb = row[6]
                vertex_data.extend(struct.pack('<4f', *((argb >> shift & 255) / 255.0
                                                         for shift in (16, 8, 0, 24))))
        remapped = [index - minimum for index, in struct.iter_unpack("<H", indices)]
        expected_indices = primitive_count * 3 if primitive == 4 else primitive_count + 2
        if (len(remapped) != expected_indices or len(remapped) < 3
                or min(remapped) < 0 or max(remapped) >= vertex_count):
            raise ValueError("index outside draw's vertex range")
        flags = (bool(render(27)) | (bool(render(14)) << 1) | ((sampler(1) == 3) << 2)
                 | ((sampler(2) == 3) << 3) | ((sampler(7) == 2) << 4)
                 | (VERTEX_COLOR if vertex_color else 0))
        draw = struct.pack("<5I", self.textures[texture][0], primitive, vertex_count,
                           len(remapped), flags)
        self.draws.append(draw + vertex_data + struct.pack(f"<{len(remapped)}H", *remapped))

    def serialize(self, clear=DEFAULT_CLEAR, depth_resets=()):
        draws = list(self.draws)
        for index in depth_resets:
            draw = bytearray(draws[index])
            draw[16] |= DEPTH_RESET
            draws[index] = draw
        payload = b"".join(entry[1] for entry in self.textures.values()) + b"".join(draws)
        version = 3 if any(draw[16] & VERTEX_COLOR for draw in draws) else 1
        if version == 3 or clear != DEFAULT_CLEAR or depth_resets:
            version = max(version, 2)
            payload = struct.pack("<2I", *clear) + payload
        return (struct.pack("<4s4I", b"FOFR", version, len(self.textures), len(self.draws), len(payload))
                + hashlib.sha256(payload).digest() + payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--drawn-frame", type=int, choices=range(1, 9), default=1)
    parser.add_argument("--menu", action="store_true", help="capture after the game's loading thread exits")
    args = parser.parse_args()
    boot = Boot(*load_demo(args.exe), stop_at_winmain=False, probe_audio=True,
                present_limit=2048 if args.menu else 16, frame_after_draw=True,
                drawn_frame_limit=args.drawn_frame, draw_after_worker=args.menu,
                instruction_limit=900_000_000,
                assets=args.assets.resolve())
    capture = FrameExport()
    def capture_selected(probe, *draw):
        if probe.capture_phase() and probe.drawn_frames == args.drawn_frame - 1:
            capture.capture(probe, *draw)
    boot.d3d.draw_sink = capture_selected
    report = boot.run()
    if not report["status"].startswith("drawn frame checkpoint:") or not capture.draws:
        raise SystemExit(f"Capture stopped before the expected frame: {report['status']}")
    try:
        clear, depth_resets = frame_clear(report["d3d"])
    except ValueError as error:
        raise SystemExit(f"Capture could not determine initial render state: {error}") from error
    packet = capture.serialize(clear, depth_resets)
    args.out.write_bytes(packet)
    print(report["status"])
    print(f"{len(capture.draws)} draws, {len(capture.textures)} textures, {len(packet)} bytes")
    print(f"Clear depth before draw indices: {sorted(depth_resets)}")
    print(f"SHA-256: {hashlib.sha256(packet).hexdigest()}")


if __name__ == "__main__":
    main()
