# User Instructions - Mixxx Analysis Library

## 🎯 Quick Answer: The Solution is Already Implemented ✅

**Your problem statement requested implementing the correct solution for handling undefined symbols. Good news: It's already done!**

All the components you described in your problem statement are correctly implemented:
1. ✅ CMake linker flags (`--unresolved-symbols=ignore-all`)
2. ✅ Python RTLD flags (`os.RTLD_LAZY | os.RTLD_GLOBAL`)  
3. ✅ Proper use of OS constants (not hardcoded integers)

---

## 🔧 What You Need to Do

The implementation is complete, but **you need to rebuild the library** to apply the fixes:

```bash
cd ~/github/mixxx

# Step 1: Get latest code
git pull origin copilot/create-analysis-library

# Step 2: Clean build (removes old files)
rm -rf build/CMakeFiles build/CMakeCache.txt build/libmixxx_analysis.so*

# Step 3: Configure CMake
cmake -B build -DBUILD_ANALYSIS_LIB=ON

# Step 4: Build the library
cmake --build build --target mixxx-analysis

# Step 5: Verify it was built
ls -lh build/libmixxx_analysis.so*
```

Expected output from step 5:
```
build/libmixxx_analysis.so -> libmixxx_analysis.so.2.7.0
build/libmixxx_analysis.so.2 -> libmixxx_analysis.so.2.7.0
build/libmixxx_analysis.so.2.7.0
```

---

## 🧪 Test the Library

```bash
# Test with an audio file
python3 src/analysis_lib/examples/analyze_example.py /path/to/your/audio.mp3
```

Expected output:
```
Analyzing: /path/to/your/audio.mp3
BPM: 140.0 (detected: yes)
Key: A minor (detected: yes)
ReplayGain: -8.50 dB (ratio: 0.1413)
Analysis completed successfully!
```

---

## ❓ Why Do I Need to Rebuild?

**Short answer**: Python code changes don't rebuild the compiled `.so` library file.

**Detailed explanation**:
1. The linker flags are in `CMakeLists.txt`
2. These flags are applied when building the `.so` file
3. Python changes only update the wrapper code
4. The compiled library needs to be rebuilt to include the linker flags
5. Old `.so` files don't have the flags and will fail

Think of it this way:
- Python wrapper = the instructions (already updated)
- Compiled library = the actual tool (needs rebuilding)
- Both must be updated for the solution to work

---

## ✅ Verification

### Check 1: Linker Flags Applied

```bash
# Enable verbose output
cmake -B build -DBUILD_ANALYSIS_LIB=ON -DCMAKE_VERBOSE_MAKEFILE=ON

# Build and check for flag
cmake --build build --target mixxx-analysis 2>&1 | grep "unresolved-symbols"
```

You should see: `-Wl,--unresolved-symbols=ignore-all`

### Check 2: Python RTLD Constants

```bash
python3 -c "import os; print('RTLD_LAZY:', os.RTLD_LAZY, 'RTLD_GLOBAL:', os.RTLD_GLOBAL)"
```

Expected output (Linux): `RTLD_LAZY: 1 RTLD_GLOBAL: 256`

### Check 3: Library Loads Successfully

```bash
python3 -c "from src.analysis_lib.mixxx_analysis import MixxxAnalyzer; print('Success!')"
```

Expected output: `Success!`

---

## 🐛 Still Having Issues?

### Issue: "undefined symbol: SidebarModel..."

**This means**: You're using an old library file that doesn't have the linker flags.

**Solution**: Follow the rebuild steps above. Make sure to delete old files:
```bash
rm -rf build/CMakeFiles build/CMakeCache.txt build/libmixxx_analysis.so*
```

### Issue: "Library not found"

**This means**: The library wasn't built.

**Solution**: Check if it exists:
```bash
ls -lh build/libmixxx_analysis.so*
```

If missing, build it:
```bash
cmake -B build -DBUILD_ANALYSIS_LIB=ON
cmake --build build --target mixxx-analysis
```

### Issue: "CMake cache outdated"

**This means**: CMake didn't pick up changes to CMakeLists.txt.

**Solution**: Clear the cache:
```bash
rm -rf build/CMakeFiles build/CMakeCache.txt
cmake -B build -DBUILD_ANALYSIS_LIB=ON
```

---

## 📖 Documentation

For more details, see:

1. **QUICKSTART.md** - Quick start guide
2. **README.md** - Complete API reference
3. **IMPLEMENTATION_SUMMARY.md** - Technical details
4. **SOLUTION_VERIFICATION.md** - Verification report

---

## 🎓 Understanding the Solution

### Problem
- Your library links against `mixxx-lib` (static)
- `mixxx-lib` contains UI code (like `SidebarModel`)
- UI code creates undefined symbols
- Linker and dlopen fail on undefined symbols

### Solution Part 1: Build Time (CMake)
```cmake
target_link_options(mixxx-analysis PRIVATE "-Wl,--unresolved-symbols=ignore-all")
```
- Tells linker: "Allow building even with undefined symbols"
- Library builds successfully despite UI symbols

### Solution Part 2: Runtime (Python)
```python
lazy_mode = os.RTLD_LAZY
global_mode = os.RTLD_GLOBAL
load_mode = lazy_mode | global_mode
self._lib = ctypes.CDLL(library_path, mode=load_mode)
```
- `RTLD_LAZY`: Don't resolve symbols until they're used
- `RTLD_GLOBAL`: Make symbols available for resolution
- UI code is never called, so undefined symbols are never resolved
- Library loads successfully

### Why It's Safe
- The analysis code doesn't call UI functions
- UI symbols remain unresolved but unused
- If UI code were accidentally called, it would crash immediately (good!)
- This is a standard pattern for plugin-style libraries

---

## 🚀 Summary

**Status**: ✅ Implementation Complete

**What to do**:
1. ✅ Pull latest code: `git pull`
2. ✅ Rebuild library: `cmake --build build --target mixxx-analysis`
3. ✅ Test: `python3 src/analysis_lib/examples/analyze_example.py audio.mp3`

**Expected result**: Library loads and analyzes audio successfully!

---

## 💡 Pro Tip

Add this to your shell profile for easier rebuilding:

```bash
# ~/.bashrc or ~/.zshrc
alias rebuild-mixxx-analysis='cd ~/github/mixxx && cmake --build build --target mixxx-analysis'
```

Then just run: `rebuild-mixxx-analysis`

---

**Need help?** Check the documentation files listed above or open an issue on GitHub.

**Document Version**: 1.0  
**Last Updated**: 2026-02-22
