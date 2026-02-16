# Quick Start Guide - Mixxx Analysis Library

This guide will help you quickly get started with building and using the Mixxx Analysis Library.

## Prerequisites

You need the same dependencies as Mixxx. On Ubuntu/Debian:

```bash
sudo apt-get install \
    build-essential cmake git \
    libqt5core5a libqt5sql5-sqlite libqt5svg5 \
    libqt5opengl5 qtdeclarative5-dev \
    libtag1-dev libchromaprint-dev libfftw3-dev \
    libmad0-dev libid3tag0-dev libmp3lame-dev \
    libvorbis-dev libogg-dev libflac-dev libopus-dev \
    libsndfile1-dev libsoundtouch-dev libebur128-dev \
    portaudio19-dev libjack-jackd2-dev
```

On macOS with Homebrew:

```bash
brew install cmake qt@5 taglib chromaprint fftw \
    mad lame libvorbis libogg flac opus \
    libsndfile soundtouch
```

## Quick Build

```bash
# Clone the repository if you haven't already
git clone https://github.com/mixxxdj/mixxx.git
cd mixxx

# Configure with the analysis library enabled
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_ANALYSIS_LIB=ON

# Build only the analysis library (much faster than building all of Mixxx)
cmake --build build --target mixxx-analysis -j$(nproc)

# The library will be in: build/libmixxx_analysis.so (or .dylib on macOS)
```

## Test the Library

### Using Python (Easiest)

```bash
# Copy or symlink the library to the examples directory
ln -s $(pwd)/build/libmixxx_analysis.so src/analysis_lib/examples/

# Run the Python example
python3 src/analysis_lib/examples/analyze_example.py /path/to/your/music.mp3
```

### Using C

```bash
# Compile the C example
gcc src/analysis_lib/examples/analyze_example.c \
    -I src/analysis_lib \
    -L build \
    -lmixxx_analysis \
    -o analyze_example

# Run it (you may need to set LD_LIBRARY_PATH)
LD_LIBRARY_PATH=build ./analyze_example /path/to/your/music.mp3
```

## Python Quick Example

```python
from mixxx_analysis import MixxxAnalyzer

with MixxxAnalyzer(library_path="./build/libmixxx_analysis.so") as analyzer:
    result = analyzer.analyze_file("song.mp3")
    print(f"BPM: {result['bpm']:.1f}")
    print(f"Key: {result['key_name']}")
    print(f"Gain: {result['replay_gain_db']:.2f} dB")
```

## Expected Output

```
Mixxx Analysis Library v2.7.0-alpha
Analyzing: song.mp3

Analysis Results:
-----------------
BPM:          128.00 ✓
Key:          Am (value: 22) ✓
ReplayGain:   -8.52 dB (ratio: 0.3750) ✓

Analysis complete!
```

## Common Issues

### Library Not Found

If you get "library not found" errors:

```bash
# Linux: Set LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/path/to/mixxx/build:$LD_LIBRARY_PATH

# macOS: Set DYLD_LIBRARY_PATH
export DYLD_LIBRARY_PATH=/path/to/mixxx/build:$DYLD_LIBRARY_PATH

# Or install the library system-wide
sudo cmake --install build
```

### Python Can't Find Library

Either:
1. Specify the path explicitly: `MixxxAnalyzer("/full/path/to/libmixxx_analysis.so")`
2. Copy the library to the same directory as your Python script
3. Install it system-wide with `sudo cmake --install build`

### Missing Dependencies

If CMake reports missing dependencies, install them using your package manager. The library needs:
- Qt5 Core and SQL modules
- Audio codec libraries (FLAC, MP3, OGG, etc.)
- Analysis libraries (ChromaPrint for key, SoundTouch for BPM, libebur128 for gain)

## Next Steps

- Read the full [README.md](README.md) for detailed API documentation
- Check out more examples in `src/analysis_lib/examples/`
- Integrate the library into your own projects

## Performance Tips

- Analysis takes 2-10 seconds per track depending on length and CPU
- Reuse the same analyzer instance for multiple files
- The library is thread-safe - you can create multiple analyzers in different threads

## Need Help?

- Check the [main Mixxx documentation](https://github.com/mixxxdj/mixxx)
- Open an issue on GitHub
- Join the Mixxx Zulip chat: https://mixxx.zulipchat.com/
