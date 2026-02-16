/**
 * Mixxx Audio Analysis Library C API
 * 
 * This library provides a C interface to Mixxx's audio analysis algorithms
 * for BPM detection, key detection, and ReplayGain (loudness) analysis.
 * 
 * It can be used from Python via ctypes or cffi.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to the analyzer context
typedef void* MixxxAnalyzerHandle;

// Analysis results structure
typedef struct {
    // BPM (beats per minute), 0.0 if detection failed
    double bpm;
    
    // Musical key as integer (0-24), -1 if detection failed
    // 0=INVALID, 1=C major, 2=Db major, ..., 12=B major
    // 13=C minor, 14=C# minor, ..., 24=B minor
    int key;
    
    // Key name as string (e.g., "C", "Am", "F#")
    char key_name[16];
    
    // ReplayGain ratio (not in dB), 1.0 means no gain adjustment needed
    // 0.0 if analysis failed
    double replay_gain_ratio;
    
    // ReplayGain in dB, 0.0 means no gain adjustment needed
    double replay_gain_db;
    
    // Success flags
    int bpm_detected;
    int key_detected;
    int gain_calculated;
    
    // Error message, empty if no error
    char error_message[256];
} MixxxAnalysisResult;

/**
 * Initialize the analysis library.
 * 
 * @return Handle to the analyzer context, NULL on failure
 */
MixxxAnalyzerHandle mixxx_analyzer_create();

/**
 * Analyze an audio file and return BPM, key, and ReplayGain.
 * 
 * @param handle The analyzer handle from mixxx_analyzer_create()
 * @param file_path Path to the audio file to analyze
 * @param result Pointer to structure to receive results
 * @return 0 on success, -1 on failure
 */
int mixxx_analyze_file(
    MixxxAnalyzerHandle handle,
    const char* file_path,
    MixxxAnalysisResult* result
);

/**
 * Clean up and destroy the analyzer context.
 * 
 * @param handle The analyzer handle to destroy
 */
void mixxx_analyzer_destroy(MixxxAnalyzerHandle handle);

/**
 * Get the library version string.
 * 
 * @return Version string (e.g., "2.7.0")
 */
const char* mixxx_analyzer_version();

#ifdef __cplusplus
}
#endif
