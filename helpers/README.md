# 12-EDO to N-EDO Scale Converter

A utility tool for converting 12-EDO scale masks to any N-EDO using closest pitch matching, similar to the resampling behavior in the PolyQuanta module.

## Building

### Console Version (Default)
```bash
make
```
This creates the `scale_converter` executable.

### Web GUI
```bash
# Web GUI (works in any browser, no dependencies)
make web
```

## Usage

### Console Version
Run the program:
```bash
./scale_converter
```

Or run with a specific file:
```bash
./scale_converter <input_file>
```

### Web GUI (Recommended)
```bash
# Using the launcher script
./web_launcher.sh

# Or manually open in browser
open scale_converter_web.html
```

### Startup Menu

When you run the program, you'll see a startup menu:

```
Choose input method:
1. Use input.txt file
2. Enter scales manually
Enter choice (1-2):
```

**Option 1: Use input.txt file**
- Uses an `input.txt` file in the current directory
- Simply place your scale definitions in `input.txt` and choose option 1
- Great for batch processing and reusing the same scales
- Create `input.txt` with your scales in the same format as manual entry

**Option 2: Enter scales manually**
- Paste scales directly into the terminal
- Original behavior for quick one-off conversions
- Good for testing or when you don't want to create a file

### Web GUI Features

- **Modern Interface**: Beautiful, intuitive web interface that works in any browser
- **Real-time Preview**: See scale count and validation status as you type
- **Visual Progress**: Animated progress bars during conversion
- **One-click Actions**: Save to file, copy to clipboard with single clicks
- **Example Loader**: Built-in example scales to get started quickly
- **Responsive Design**: Works on desktop, tablet, and mobile devices
- **No Dependencies**: Works offline, no installation required

### Console Features

- **Startup Menu**: Choose between file input or manual entry
- **File Output**: Results are automatically saved to a text file (you specify the filename)
- **Debug Logging**: Comprehensive logging to help debug issues - log files are created with timestamps
- **Error Handling**: Robust error handling prevents crashes and provides helpful error messages
- **Input Validation**: Validates all inputs and provides clear feedback on errors

### Input Format

Paste your 12-EDO scale definitions in the format used in ScaleDefs.cpp:

```
{"Chord: Sus4 Triad", {1,0,0,0,0,1,0,1,0,0,0,0}},
{"Scale: Major", {1,0,1,0,1,1,0,1,0,1,0,1}},
{"Scale: Chromatic", {1,1,1,1,1,1,1,1,1,1,1,1}},
```

End your input with an empty line.

### Conversion Modes

1. **Single EDO**: Convert to one specific EDO (e.g., 19-EDO)
2. **Range**: Convert to a range of EDOs (e.g., 13-120)
3. **Multiple Individual**: Convert to specific EDOs (e.g., 13,17,19,22)

### Output Format

The program outputs C++ array definitions to a text file, ready to paste into ScaleDefs.cpp:

```cpp
// Single EDO mode:
{"Scale: Major", {1,0,1,0,1,1,0,1,0,1,0,1,0,1,0,1,1,0,1}},

// Range mode:
// 13-EDO scales
const int NUM_SCALES_13EDO = 3;
static const Scale SCALES_13EDO[] = {
    {"Chord: Sus4 Triad", {1,0,0,0,0,1,0,1,0,0,0,0,0}},
    {"Scale: Major", {1,0,1,0,1,1,0,1,0,1,0,1,0}},
    {"Scale: Chromatic", {1,1,1,1,1,1,1,1,1,1,1,1,1}}
};
```

## How It Works

The converter uses **closest pitch matching** to map 12-EDO intervals to any N-EDO:

1. Each 12-EDO pitch position (0-11) is converted to a fractional position (0.0-1.0)
2. The fractional position is mapped to the nearest pitch in the target EDO
3. The root degree (bit 0) is always preserved as bit 0 in the target EDO

### Example: 12-EDO Major → 19-EDO

12-EDO Major: `{1,0,1,0,1,1,0,1,0,1,0,1}`
- Root (0): 0/12 = 0.000 → 0/19 = 0 (position 0)
- Major 2nd (2): 2/12 = 0.167 → 3/19 = 0.158 (position 3)
- Major 3rd (4): 4/12 = 0.333 → 6/19 = 0.316 (position 6)
- Perfect 4th (5): 5/12 = 0.417 → 8/19 = 0.421 (position 8)
- Perfect 5th (7): 7/12 = 0.583 → 11/19 = 0.579 (position 11)
- Major 6th (9): 9/12 = 0.750 → 14/19 = 0.737 (position 14)
- Major 7th (11): 11/12 = 0.917 → 17/19 = 0.895 (position 17)

Result: `{1,0,0,1,0,0,1,0,1,0,0,1,0,0,1,0,0,1,0}`

## Tips

- **For GUI users**: Use the web interface for the best experience - it's intuitive and feature-rich
- **For repeated use**: Create an `input.txt` file with your common scales and use option 1 in console
- **For quick tests**: Use option 2 to paste scales directly without creating files (console) or the web GUI
- The converter preserves the musical structure while adapting to different tuning systems
- Root degree stability is maintained across all conversions
- Use range mode to quickly generate scales for many EDOs at once
- Output is saved to a text file for easy copy-paste into ScaleDefs.cpp
- Check the generated log files if you encounter any issues - they contain detailed debugging information
- The program now handles errors gracefully and won't crash unexpectedly

## Cleanup

```bash
make clean
```

This removes the compiled executable.
