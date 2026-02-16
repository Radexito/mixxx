/**
 * Example program demonstrating the Mixxx Analysis Library C API
 * 
 * Compile with:
 *   gcc -o analyze_example analyze_example.c -lmixxx_analysis
 * 
 * Run with:
 *   ./analyze_example /path/to/audio/file.mp3
 */

#include "mixxx_analysis.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <audio_file>\n", argv[0]);
        return 1;
    }
    
    const char* file_path = argv[1];
    
    /* Print library version */
    printf("Mixxx Analysis Library v%s\n", mixxx_analyzer_version());
    printf("Analyzing: %s\n\n", file_path);
    
    /* Create analyzer context */
    MixxxAnalyzerHandle analyzer = mixxx_analyzer_create();
    if (!analyzer) {
        fprintf(stderr, "ERROR: Failed to create analyzer context\n");
        return 1;
    }
    
    /* Analyze the file */
    MixxxAnalysisResult result;
    int status = mixxx_analyze_file(analyzer, file_path, &result);
    
    if (status != 0) {
        fprintf(stderr, "ERROR: Analysis failed: %s\n", result.error_message);
        mixxx_analyzer_destroy(analyzer);
        return 1;
    }
    
    /* Print results */
    printf("Analysis Results:\n");
    printf("-----------------\n");
    
    if (result.bpm_detected) {
        printf("BPM:          %.2f\n", result.bpm);
    } else {
        printf("BPM:          Not detected\n");
    }
    
    if (result.key_detected) {
        printf("Key:          %s (value: %d)\n", result.key_name, result.key);
    } else {
        printf("Key:          Not detected\n");
    }
    
    if (result.gain_calculated) {
        printf("ReplayGain:   %.2f dB (ratio: %.4f)\n", 
               result.replay_gain_db, result.replay_gain_ratio);
    } else {
        printf("ReplayGain:   Not calculated\n");
    }
    
    /* Cleanup */
    mixxx_analyzer_destroy(analyzer);
    
    printf("\nAnalysis complete!\n");
    return 0;
}
