"""Capture Direct3D commands and the referenced data for a selected Present."""
import json
from pathlib import Path
import struct


class SceneCapture:
    def __init__(self, probe, present, directory, skinning=None):
        self.present = present
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.draws = []
        self.declarations = {}
        self.shaders = {}
        self.images = {}
        self.image_keys = {}
        self.targets = {}
        self.clears = []
        self.skinning = skinning
        self.software_skinned_draws = 0
        if probe.present_count + 1 == present:
            probe.draw_sink = self.draw
            probe.clear_sink = self.clear
        probe.frame_sink = self.finish_frame

    def write_blob(self, kind, tag, data):
        name = f'{kind}-{tag}.bin'
        (self.directory / name).write_bytes(data)
        return name

    def image(self, probe, handle):
        key = handle, probe.texture_revisions.get(handle, 0)
        if key in self.image_keys:
            return self.image_keys[key]
        texture_id = len(self.images) + 1
        shape = probe.textures[handle][0]
        levels = []
        for level in range(shape[2]):
            storage = probe.texture_storage.get((handle, level))
            if storage is None:
                break
            address, size, _ = storage
            pixels = probe.boot.cpu.mem_read(address, size)
            levels.append(self.write_blob('texture', f'{texture_id}-{level}', pixels))
        self.images[texture_id] = {'shape': shape, 'levels': levels}
        self.image_keys[key] = texture_id
        return texture_id

    def target(self, probe):
        destination = probe.targets[0]
        if destination == probe.backbuffer:
            return 0
        if destination not in self.targets:
            image, level = probe.surface_levels[destination]
            if level != 0:
                raise ValueError('only level-zero render targets are supported')
            texture = probe.textures[image][0]
            depth = probe.surfaces[probe.depth][0]
            if depth[-2:] != texture[:2]:
                raise ValueError('render target and depth surface dimensions differ')
            self.targets[destination] = self.image(probe, image)
        return self.targets[destination]

    def clear(self, probe, event):
        self.clears.append((len(self.draws), event, self.target(probe)))

    def draw(self, probe, args, stride, vertices, index_size, indices):
        if probe.present_count + 1 != self.present:
            return
        state = probe.device_state
        index = len(self.draws)
        resources = probe.programs.resources
        vertex_shader = state.get('SetVertexShader', 0)
        pixel_shader = state.get('SetPixelShader', 0)
        declaration = state.get('SetVertexDeclaration', 0)
        for handle in (vertex_shader, pixel_shader):
            if handle and handle not in self.shaders:
                self.shaders[handle] = self.write_blob('shader', handle, resources[handle].data)
        if declaration and declaration not in self.declarations:
            data = resources[declaration].data
            self.declarations[declaration] = list(struct.iter_unpack('<HHBBBB', data))[:-1]

        grouped = {}
        for key, value in state.items():
            if isinstance(key, tuple):
                method, *indices_in_state = key
                state_index = ':'.join(map(str, indices_in_state))
                grouped.setdefault(method, {})[state_index] = value

        def vectors(method):
            registers = probe.programs.constants.get(method, {})
            return {key: struct.unpack('<4f', value) for key, value in registers.items()}

        slots = {slot: self.image(probe, handle)
                 for slot, handle in grouped.get('SetTexture', {}).items() if handle}
        viewport = state.get('SetViewport')
        if viewport is None:
            viewport = struct.pack('<4I2f', 0, 0, *probe.device_parameters[:2], 0.0, 1.0)
        material = state.get('SetMaterial')
        vertex_constants = vectors('SetVertexShaderConstantF')
        if self.skinning:
            vertices, restored = self.skinning.draw(resources.get(vertex_shader), stride, vertices)
            vertex_constants.update(restored)
            self.software_skinned_draws += bool(restored)
        self.draws.append({
            'args': args,
            'stride': stride,
            'index_size': index_size,
            'vertex_file': self.write_blob('vertices', index, vertices),
            'index_file': self.write_blob('indices', index, indices),
            'target': self.target(probe),
            'fvf': state.get('SetFVF'),
            'vs': vertex_shader,
            'ps': pixel_shader,
            'decl': declaration,
            'slots': slots,
            'render': grouped.get('SetRenderState', {}),
            'viewport': struct.unpack('<4I2f', viewport),
            'matrices': {key: struct.unpack('<16f', value)
                         for key, value in grouped.get('SetTransform', {}).items()},
            'lights': {key: struct.unpack('<I25f', value)
                       for key, value in grouped.get('SetLight', {}).items()
                       if grouped.get('LightEnable', {}).get(key)},
            'material': struct.unpack('<17f', material) if material else None,
            'samplers': grouped.get('SetSamplerState', {}),
            'stages': grouped.get('SetTextureStageState', {}),
            'v_constants': vertex_constants,
            'p_constants': vectors('SetPixelShaderConstantF'),
        })

    def finish_frame(self, probe, present, draw_count):
        if present + 1 == self.present:
            probe.draw_sink = self.draw
            probe.clear_sink = self.clear
            return
        if present != self.present:
            return
        if draw_count != len(self.draws) or not self.draws:
            raise ValueError(f'Present #{present}: captured {len(self.draws)} of {draw_count} draws')
        scene = {
            'present': present, 'draws': self.draws,
            'backbuffer': probe.device_parameters[:2],
            'clears': [(at, event) for at, event, target in self.clears],
            'clear_targets': [target for at, event, target in self.clears],
            'texture_resources': self.images, 'declarations': self.declarations, 'programs': self.shaders,
            'software_skinned_draws': self.software_skinned_draws,
        }
        path = self.directory / 'scene.json'
        pending = path.with_suffix('.tmp')
        pending.write_text(json.dumps(scene, indent=2))
        pending.replace(path)
        probe.draw_sink = None
        probe.clear_sink = None
        probe.frame_sink = None
