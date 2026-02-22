#!/usr/bin/env python3
"""
Python wrapper for the Mixxx Analysis Library

This module provides a Python interface to Mixxx's audio analysis algorithms
for BPM detection, key detection, and ReplayGain (loudness) analysis.

Example:
    >>> from mixxx_analysis import MixxxAnalyzer
    >>> analyzer = MixxxAnalyzer()
    >>> result = analyzer.analyze_file("/path/to/audio.mp3")
    >>> print(f"BPM: {result['bpm']}, Key: {result['key_name']}")
    >>> analyzer.close()
"""

import ctypes
import os
import platform
from pathlib import Path
from typing import Any, Dict, Optional


class MixxxAnalysisLibraryNotFoundError(OSError):
    """Raised when the Mixxx Analysis Library cannot be found."""
    pass


class MixxxAnalysisResult(ctypes.Structure):
    """C structure for analysis results"""
    _fields_ = [
        ("bpm", ctypes.c_double),
        ("key", ctypes.c_int),
        ("key_name", ctypes.c_char * 16),
        ("replay_gain_ratio", ctypes.c_double),
        ("replay_gain_db", ctypes.c_double),
        ("bpm_detected", ctypes.c_int),
        ("key_detected", ctypes.c_int),
        ("gain_calculated", ctypes.c_int),
        ("error_message", ctypes.c_char * 256),
    ]


class MixxxAnalyzer:
    """
    Wrapper class for the Mixxx Analysis Library.
    
    This class provides access to Mixxx's audio analysis algorithms from Python.
    """
    
    def __init__(self, library_path: Optional[str] = None):
        """
        Initialize the analyzer.
        
        Args:
            library_path: Optional path to the shared library. If not provided,
                         it will search in standard locations.
        
        Raises:
            MixxxAnalysisLibraryNotFoundError: If the library cannot be found.
            RuntimeError: If the library loads but analyzer creation fails.
        """
        # Initialize attributes first to ensure they exist even if initialization fails
        self._handle = None
        self._lib = None
        
        # Track search information for better error messages
        searched_paths = []
        
        if library_path is None:
            library_path, searched_paths = self._find_library_with_paths()
        
        try:
            # Use RTLD_LAZY | RTLD_GLOBAL for flexible symbol resolution.
            # This is necessary because mixxx-lib contains UI code that creates undefined
            # symbols, but those code paths are never executed by the analysis library.
            # - RTLD_LAZY: Defer symbol resolution until first use
            # - RTLD_GLOBAL: Make symbols available for resolution (helps with undefined symbols)
            # On Windows, these flags don't exist but undefined symbols are handled differently
            lazy_mode = getattr(os, 'RTLD_LAZY', 0)
            global_mode = getattr(os, 'RTLD_GLOBAL', 0)
            load_mode = lazy_mode | global_mode if lazy_mode else 0
            self._lib = ctypes.CDLL(library_path, mode=load_mode)
        except OSError as e:
            # Provide a helpful error message with build instructions
            error_msg = [
                "Could not load the Mixxx Analysis Library.",
                "",
                "The library file was not found or could not be loaded.",
            ]
            
            if searched_paths:
                error_msg.append("")
                error_msg.append("Searched in the following locations:")
                for path in searched_paths:
                    error_msg.append(f"  - {path}")
            
            error_msg.extend([
                "",
                "To build the library, run from the repository root:",
                "  cmake -B build -DBUILD_ANALYSIS_LIB=ON",
                "  cmake --build build --target mixxx-analysis",
                "",
                "Or specify the library path explicitly:",
                "  MixxxAnalyzer('/path/to/libmixxx_analysis.so')",
                "",
                f"Original error: {e}",
            ])
            
            raise MixxxAnalysisLibraryNotFoundError("\n".join(error_msg)) from e
        
        # Define function signatures
        self._lib.mixxx_analyzer_create.argtypes = []
        self._lib.mixxx_analyzer_create.restype = ctypes.c_void_p
        
        self._lib.mixxx_analyze_file.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.POINTER(MixxxAnalysisResult)
        ]
        self._lib.mixxx_analyze_file.restype = ctypes.c_int
        
        self._lib.mixxx_analyzer_destroy.argtypes = [ctypes.c_void_p]
        self._lib.mixxx_analyzer_destroy.restype = None
        
        self._lib.mixxx_analyzer_version.argtypes = []
        self._lib.mixxx_analyzer_version.restype = ctypes.c_char_p
        
        # Create analyzer handle
        self._handle = self._lib.mixxx_analyzer_create()
        if not self._handle:
            raise RuntimeError("Failed to create analyzer context")
    
    def _find_library_with_paths(self):
        """
        Find the shared library in standard locations.
        
        Returns:
            Tuple of (library_path, searched_paths) where searched_paths is a list
            of all locations that were checked.
        """
        system = platform.system()
        
        if system == "Linux":
            lib_names = ["libmixxx_analysis.so", "libmixxx_analysis.so.2"]
        elif system == "Darwin":
            lib_names = ["libmixxx_analysis.dylib", "libmixxx_analysis.2.dylib"]
        elif system == "Windows":
            lib_names = ["mixxx_analysis.dll"]
        else:
            raise OSError(f"Unsupported platform: {system}")
        
        # Search in common locations
        # The wrapper is in src/analysis_lib/, so repository root is two levels up
        repo_root = Path(__file__).parent.parent.parent
        
        search_paths = [
            Path.cwd(),  # Current directory
            Path(__file__).parent,  # Same directory as this script (src/analysis_lib)
            repo_root / "build",  # Repository root build directory
            Path(__file__).parent / "build",  # src/analysis_lib/build (for alternative setups)
            Path("/usr/local/lib"),
            Path("/usr/lib"),
        ]
        
        searched = []
        for search_path in search_paths:
            for lib_name in lib_names:
                lib_path = search_path / lib_name
                searched.append(str(lib_path))
                if lib_path.exists():
                    return str(lib_path), searched
        
        # Last resort: try loading by name (system will search standard paths)
        # Add this to the searched list for informational purposes
        searched.append(f"{lib_names[0]} (system library paths)")
        return lib_names[0], searched
    
    def _find_library(self) -> str:
        """Find the shared library in standard locations (legacy method)."""
        library_path, _ = self._find_library_with_paths()
        return library_path
    
    def analyze_file(self, file_path: str) -> Dict[str, Any]:
        """
        Analyze an audio file and return BPM, key, and ReplayGain.
        
        Args:
            file_path: Path to the audio file to analyze.
        
        Returns:
            Dictionary containing:
                - bpm (float): Detected BPM, or 0.0 if detection failed
                - key (int): Musical key as integer (0-24), or -1 if detection failed
                - key_name (str): Key name (e.g., "C", "Am", "F#")
                - replay_gain_ratio (float): ReplayGain ratio
                - replay_gain_db (float): ReplayGain in dB
                - bpm_detected (bool): Whether BPM was successfully detected
                - key_detected (bool): Whether key was successfully detected
                - gain_calculated (bool): Whether gain was successfully calculated
                - error_message (str): Error message if analysis failed
        
        Raises:
            FileNotFoundError: If the file does not exist.
            RuntimeError: If analysis fails.
        """
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"File not found: {file_path}")
        
        result = MixxxAnalysisResult()
        file_path_bytes = file_path.encode('utf-8')
        
        ret = self._lib.mixxx_analyze_file(
            self._handle,
            file_path_bytes,
            ctypes.byref(result)
        )
        
        if ret != 0:
            error_msg = result.error_message.decode('utf-8')
            raise RuntimeError(f"Analysis failed: {error_msg}")
        
        return {
            'bpm': result.bpm,
            'key': result.key,
            'key_name': result.key_name.decode('utf-8'),
            'replay_gain_ratio': result.replay_gain_ratio,
            'replay_gain_db': result.replay_gain_db,
            'bpm_detected': bool(result.bpm_detected),
            'key_detected': bool(result.key_detected),
            'gain_calculated': bool(result.gain_calculated),
            'error_message': result.error_message.decode('utf-8'),
        }
    
    def get_version(self) -> str:
        """Get the library version string."""
        return self._lib.mixxx_analyzer_version().decode('utf-8')
    
    def close(self):
        """Clean up and destroy the analyzer context."""
        if self._handle:
            self._lib.mixxx_analyzer_destroy(self._handle)
            self._handle = None
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()
    
    def __del__(self):
        """Destructor to ensure cleanup."""
        self.close()


if __name__ == "__main__":
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python mixxx_analysis.py <audio_file>")
        sys.exit(1)
    
    with MixxxAnalyzer() as analyzer:
        print(f"Mixxx Analysis Library v{analyzer.get_version()}")
        print(f"Analyzing: {sys.argv[1]}")
        
        try:
            result = analyzer.analyze_file(sys.argv[1])
            
            print("\nResults:")
            print(f"  BPM: {result['bpm']:.2f}" + 
                  (" ✓" if result['bpm_detected'] else " ✗"))
            print(f"  Key: {result['key_name']}" + 
                  (" ✓" if result['key_detected'] else " ✗"))
            print(f"  ReplayGain: {result['replay_gain_db']:.2f} dB" + 
                  (" ✓" if result['gain_calculated'] else " ✗"))
            
            if result['error_message']:
                print(f"\nError: {result['error_message']}")
        
        except Exception as e:
            print(f"Error: {e}")
            sys.exit(1)
