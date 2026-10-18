# Project-managed editor decoder

This project downloads unmodified FFmpeg executables from the pinned
imageio-ffmpeg 0.6.0 wheels on PyPI. The repository ships URLs and SHA-256
pins, not FFmpeg binaries. No wheel Python code is installed or executed.

Upstream package, license and binary provenance:
- https://pypi.org/project/imageio-ffmpeg/0.6.0/
- https://github.com/imageio/imageio-ffmpeg/tree/v0.6.0
- https://github.com/imageio/imageio-ffmpeg/blob/v0.6.0/tasks.py
- https://github.com/imageio/imageio-binaries/tree/master/ffmpeg

The wrapper package is BSD-2-Clause. That license does **not** replace the
FFmpeg executable's own license. These builds can include GPL codecs/libraries.
The cache retains the package notices and the executable's `-L` license output
and `-version` build configuration. Consult those for the selected platform.
FFmpeg license texts and upstream source versions:
- https://ffmpeg.org/legal.html
- https://github.com/FFmpeg/FFmpeg/tree/n7.1 (Mac/Windows)
- https://github.com/FFmpeg/FFmpeg/tree/n7.0.2 (Linux)

These source links identify upstream versions, not a claim that this project
provides complete corresponding source for every third-party build. If you
redistribute the downloaded decoder, obtain matching sources/build information
for its enabled libraries from its binary provider and satisfy its license.
Do not copy Saved/Tools/FFmpeg into packaged games or project distributions.
Packaged games consume imported SoundWave assets and need no decoder.

For a different decoder build, pass -AudioDecoder with its absolute path.
