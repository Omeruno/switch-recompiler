"""Memory layout of the texture formats accepted by the demo probe."""
PIXEL_BYTES = {20: 3, 21: 4, 22: 4, 23: 2, 24: 2, 25: 2, 26: 2, 28: 1}
BLOCK_BYTES = {int.from_bytes(f'DXT{i}'.encode(), 'little'): 8 if i == 1 else 16
               for i in range(1, 6)}
