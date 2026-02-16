# Mixxx Audio Analysis Library

A C/C++ library with Python bindings that provides access to Mixxx's audio analysis algorithms for:
- **BPM detection** (tempo/beats per minute)
- **Key detection** (musical key)
- **ReplayGain calculation** (loudness normalization)

This library allows you to analyze audio files programmatically without running the full Mixxx application.

## Features

- Standalone shared library (.so/.dll/.dylib)
- C API for maximum compatibility
- Python wrapper using ctypes
- Support for all audio formats that Mixxx supports (MP3, FLAC, WAV, OGG, M4A, etc.)
- Fast analysis using the same algorithms as Mixxx

## Building

### Prerequisites

- CMake >= 3.21
- Qt 5 or Qt 6
- All standard Mixxx dependencies (see main README.md)

### Build Instructions

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_ANALYSIS_LIB=ON

# Build the library
cmake --build build --target mixxx-analysis

# Install (optional)
sudo cmake --install build
```

The library will be built as:
- **Linux**: `libmixxx_analysis.so`
- **macOS**: `libmixxx_analysis.dylib`
- **Windows**: `mixxx_analysis.dll`

## C API Usage

```c
#include <mixxx_analysis.h>
#include <stdio.h>

int main() {
    // Create analyzer
    MixxxAnalyzerHandle analyzer = mixxx_analyzer_create();
    if (!analyzer) {
        fprintf(stderr, "Failed to create analyzer\n");
        return 1;
    }
    
    // Analyze file
    MixxxAnalysisResult result;
    if (mixxx_analyze_file(analyzer, "/path/to/audio.mp3", &result) == 0) {
        if (result.bpm_detected) {
            printf("BPM: %.2f\n", result.bpm);
        }
        if (result.key_detected) {
            printf("Key: %s\n", result.key_name);
        }
        if (result.gain_calculated) {
            printf("ReplayGain: %.2f dB\n", result.replay_gain_db);
        }
    } else {
        fprintf(stderr, "Analysis failed: %s\n", result.error_message);
    }
    
    // Cleanup
    mixxx_analyzer_destroy(analyzer);
    return 0;
}
```

Compile with:
```bash
gcc -o analyze analyze.c -lmixxx_analysis
```

## Python Usage

### Basic Example

```python
from mixxx_analysis import MixxxAnalyzer

# Create analyzer (automatically finds library)
with MixxxAnalyzer() as analyzer:
    # Analyze a file
    result = analyzer.analyze_file("/path/to/audio.mp3")
    
    print(f"BPM: {result['bpm']:.2f}")
    print(f"Key: {result['key_name']}")
    print(f"ReplayGain: {result['replay_gain_db']:.2f} dB")
```

### Batch Processing

```python
from mixxx_analysis import MixxxAnalyzer
from pathlib import Path

with MixxxAnalyzer() as analyzer:
    for audio_file in Path("/music").glob("**/*.mp3"):
        try:
            result = analyzer.analyze_file(str(audio_file))
            if result['bpm_detected']:
                print(f"{audio_file.name}: {result['bpm']:.1f} BPM, {result['key_name']}")
        except Exception as e:
            print(f"Error analyzing {audio_file.name}: {e}")
```

### Command Line Usage

```bash
# Analyze a single file
python src/analysis_lib/mixxx_analysis.py /path/to/audio.mp3

# With custom library path
python -c "
from mixxx_analysis import MixxxAnalyzer
analyzer = MixxxAnalyzer('/custom/path/libmixxx_analysis.so')
result = analyzer.analyze_file('audio.mp3')
print(result)
analyzer.close()
"
```

## API Reference

### C API

#### Functions

- **`MixxxAnalyzerHandle mixxx_analyzer_create()`**
  - Creates and initializes an analyzer context
  - Returns: Handle to analyzer, or NULL on failure

- **`int mixxx_analyze_file(MixxxAnalyzerHandle handle, const char* file_path, MixxxAnalysisResult* result)`**
  - Analyzes an audio file
  - Parameters:
    - `handle`: Analyzer handle from `mixxx_analyzer_create()`
    - `file_path`: Path to audio file
    - `result`: Pointer to result structure
  - Returns: 0 on success, -1 on failure

- **`void mixxx_analyzer_destroy(MixxxAnalyzerHandle handle)`**
  - Destroys analyzer context and frees resources
  - Parameters:
    - `handle`: Analyzer handle to destroy

- **`const char* mixxx_analyzer_version()`**
  - Returns library version string

#### MixxxAnalysisResult Structure

```c
typedef struct {
    double bpm;                  // Detected BPM (0.0 if failed)
    int key;                     // Key as integer 0-24 (-1 if failed)
    char key_name[16];           // Key name (e.g., "C", "Am", "F#")
    double replay_gain_ratio;    // ReplayGain ratio (1.0 = no adjustment)
    double replay_gain_db;       // ReplayGain in decibels
    int bpm_detected;            // 1 if BPM detected, 0 otherwise
    int key_detected;            // 1 if key detected, 0 otherwise
    int gain_calculated;         // 1 if gain calculated, 0 otherwise
    char error_message[256];     // Error message (empty if no error)
} MixxxAnalysisResult;
```

### Python API

#### MixxxAnalyzer Class

**Constructor:**
```python
MixxxAnalyzer(library_path: Optional[str] = None)
```
- `library_path`: Optional path to shared library (auto-detected if not provided)

**Methods:**

- **`analyze_file(file_path: str) -> Dict`**
  - Analyzes an audio file
  - Returns dictionary with keys:
    - `bpm` (float): Detected BPM
    - `key` (int): Key as integer
    - `key_name` (str): Key name
    - `replay_gain_ratio` (float): ReplayGain ratio
    - `replay_gain_db` (float): ReplayGain in dB
    - `bpm_detected` (bool): Success flag
    - `key_detected` (bool): Success flag
    - `gain_calculated` (bool): Success flag
    - `error_message` (str): Error message if any

- **`get_version() -> str`**
  - Returns library version

- **`close()`**
  - Cleanup resources (called automatically with context manager)

## Supported Audio Formats

The library supports the same formats as Mixxx:
- MP3 (.mp3)
- FLAC (.flac)
- Ogg Vorbis (.ogg)
- AAC/M4A (.m4a, .aac)
- WAV (.wav)
- AIFF (.aiff)
- WavPack (.wv)
- Opus (.opus)
- MP4 (.mp4)

## Musical Key Values

The `key` field in results uses the following mapping:
- 0-11: Major keys (C, Db, D, Eb, E, F, F#, G, Ab, A, Bb, B)
- 12-23: Minor keys (Am, Bbm, Bm, Cm, C#m, Dm, Ebm, Em, Fm, F#m, Gm, G#m)
- 24: Invalid/Unknown
- -1: Detection failed

## Performance Notes

- Analysis is CPU-intensive and processes the entire audio file
- Typical processing time is 2-10 seconds per track depending on duration and CPU speed
- The library is thread-safe for multiple analyzer instances
- For best performance, reuse a single analyzer instance for multiple files

## Troubleshooting

### Library Not Found
If Python can't find the library, specify the path explicitly:
```python
analyzer = MixxxAnalyzer('/path/to/libmixxx_analysis.so')
```

### Analysis Fails
Common reasons:
- Unsupported audio format
- Corrupted audio file
- Insufficient permissions
- Audio file too short (< 10 seconds may fail)

Check the `error_message` field in the result for details.

## License

This library is part of Mixxx and uses the same GPL license. See LICENSE file in the root directory.

## Contributing

Contributions are welcome! Please see CONTRIBUTING.md in the root directory.

## Support

- GitHub Issues: https://github.com/mixxxdj/mixxx/issues
- Mixxx Zulip Chat: https://mixxx.zulipchat.com/
- Website: https://www.mixxx.org/
