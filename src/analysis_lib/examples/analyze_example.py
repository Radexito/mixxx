#!/usr/bin/env python3
"""
Example script demonstrating the Mixxx Analysis Library Python wrapper

Usage:
    python analyze_example.py /path/to/audio/file.mp3
    python analyze_example.py /music/folder/*.mp3
"""

import sys
from pathlib import Path

# Add parent directory to path to import the wrapper
sys.path.insert(0, str(Path(__file__).parent.parent))

from mixxx_analysis import MixxxAnalyzer


def analyze_single_file(analyzer, file_path):
    """Analyze a single audio file and print results."""
    print(f"\nAnalyzing: {file_path}")
    print("-" * 60)
    
    try:
        result = analyzer.analyze_file(str(file_path))
        
        # Print BPM
        if result['bpm_detected']:
            print(f"  BPM:          {result['bpm']:.2f} ✓")
        else:
            print(f"  BPM:          Not detected ✗")
        
        # Print Key
        if result['key_detected']:
            print(f"  Key:          {result['key_name']} (value: {result['key']}) ✓")
        else:
            print(f"  Key:          Not detected ✗")
        
        # Print ReplayGain
        if result['gain_calculated']:
            print(f"  ReplayGain:   {result['replay_gain_db']:.2f} dB (ratio: {result['replay_gain_ratio']:.4f}) ✓")
        else:
            print(f"  ReplayGain:   Not calculated ✗")
        
        if result['error_message']:
            print(f"  Warning:      {result['error_message']}")
        
        return result
        
    except FileNotFoundError:
        print(f"  ERROR: File not found")
        return None
    except Exception as e:
        print(f"  ERROR: {e}")
        return None


def analyze_batch(file_paths):
    """Analyze multiple audio files."""
    results = []
    
    with MixxxAnalyzer() as analyzer:
        print(f"Mixxx Analysis Library v{analyzer.get_version()}")
        print(f"Analyzing {len(file_paths)} file(s)...\n")
        
        for file_path in file_paths:
            result = analyze_single_file(analyzer, file_path)
            if result:
                results.append((file_path, result))
    
    return results


def main():
    if len(sys.argv) < 2:
        print("Usage: python analyze_example.py <audio_file> [<audio_file2> ...]")
        print("\nExample:")
        print("  python analyze_example.py song.mp3")
        print("  python analyze_example.py *.mp3")
        print("  python analyze_example.py /music/folder/*.flac")
        sys.exit(1)
    
    # Collect all file paths
    file_paths = []
    for arg in sys.argv[1:]:
        path = Path(arg)
        if path.exists():
            if path.is_file():
                file_paths.append(path)
            elif path.is_dir():
                # If directory, find all audio files
                for ext in ['*.mp3', '*.flac', '*.ogg', '*.wav', '*.m4a']:
                    file_paths.extend(path.glob(ext))
    
    if not file_paths:
        print("ERROR: No valid audio files found")
        sys.exit(1)
    
    # Analyze all files
    results = analyze_batch(file_paths)
    
    # Print summary
    print("\n" + "=" * 60)
    print(f"Summary: Analyzed {len(results)}/{len(file_paths)} file(s) successfully")
    
    if results and len(results) > 1:
        print("\nQuick Summary:")
        for file_path, result in results:
            status = []
            if result['bpm_detected']:
                status.append(f"BPM:{result['bpm']:.1f}")
            if result['key_detected']:
                status.append(f"Key:{result['key_name']}")
            print(f"  {file_path.name}: {', '.join(status) if status else 'No results'}")


if __name__ == "__main__":
    main()
