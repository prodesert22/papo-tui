"""Exercise FFmpeg and graphical encoders with local files; no HTTP/PTY responder."""
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='papo-image-decode-') as directory:
    root = Path(directory)
    for n, color in enumerate(((255, 0, 0), (0, 0, 255))):
        (root / f'{n}.ppm').write_bytes(b'P6\n16 12\n255\n' + bytes(color) * 16 * 12)
    def ffmpeg(*args):
        subprocess.run([sys.argv[2], '-v', 'error', *map(str, args), '-threads', '1', '-y'], check=True, capture_output=True)
    png, gif, large, tall = (root / name for name in ('small.png', 'animated.gif', 'large.png', 'tall.png'))
    ffmpeg('-i', root / '0.ppm', '-frames:v', '1', png)
    ffmpeg('-framerate', '5', '-i', root / '%d.ppm', '-frames:v', '2', '-loop', '0', gif)
    ffmpeg('-f', 'lavfi', '-i', 'color=red:s=1000x500', '-frames:v', '1', large)
    ffmpeg('-f', 'lavfi', '-i', 'color=red:s=128x160', '-frames:v', '1', tall)
    broken = root / 'broken.gif'; broken.write_bytes(b'GIF89a\x10\x00\x0c\x00invalid')
    long_gif = root / 'long.gif'
    ffmpeg('-stream_loop', '128', '-i', gif, '-frames:v', '257', long_gif)
    subprocess.run([sys.argv[1], str(png), str(gif), str(large), str(tall), str(broken), str(long_gif)], check=True)
