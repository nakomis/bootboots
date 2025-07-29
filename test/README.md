# BootBoots Cat Detection Accuracy Test

This directory contains automated testing tools for evaluating the accuracy of the BootBoots cat detection system.

## Overview

The test system evaluates how well the BootBoots API can correctly identify cats from images. It supports two testing modes:

- **SPECIFIC MODE** (default): Uses predefined test images for consistent, reproducible results
- **RANDOM MODE**: Uses randomly selected images for varied testing scenarios

## Files

- `cat-detection-accuracy.test.ts` - Main TypeScript test file
- `run-cat-detection-test.sh` - Shell script for easy test execution
- `package.json` - Node.js dependencies and scripts
- `node_modules/` - Installed dependencies (auto-generated)
- `cat-detection-results-*.json` - Test result files (generated after each run)

## Usage

### Quick Start

```bash
# Run test with specific images (default)
./run-cat-detection-test.sh

# Run test with random images
./run-cat-detection-test.sh --random

# Show help
./run-cat-detection-test.sh --help
```

### Test Modes

#### Specific Mode (Default)
Uses predefined test images that were previously validated:

```bash
./run-cat-detection-test.sh
```

**Specific Test Images:**
- **Boots**: `600.jpeg.jpg`, `609.jpeg.jpg`, `1614.jpeg.jpg`
- **Chi**: `IMG_2665.jpeg.jpg`, `IMG_2666.jpeg.jpg`, `IMG_2667.jpeg.jpg`
- **Kappa**: `1171.jpeg.jpg`, `IMG_2652.jpeg.jpg`, `1616.jpeg.jpg`
- **Mu**: `IMG_0127.jpeg.jpg`, `IMG_0540.jpeg.jpg`, `IMG_0153.jpeg.jpg`
- **Tau**: `IMG_2368.jpeg.jpg`, `453.jpeg.jpg`, `1015.jpeg.jpg`
- **Wolf**: `1486.jpeg.jpg`, `1423.jpeg.jpg`, `1428.jpeg.jpg`

#### Random Mode
Randomly selects 3 images per cat for varied testing:

```bash
./run-cat-detection-test.sh --random
```

### Environment Variables

You can also control the test mode using environment variables:

```bash
# Use random mode via environment variable
USE_RANDOM=true ./run-cat-detection-test.sh
```

## Test Results

### Current Performance (Specific Mode)
- **Overall Accuracy**: 83.3% (15/18 correct predictions)
- **Production Ready**: ✅ Exceeds 80% accuracy threshold

#### Per-Cat Performance
| Cat | Accuracy | Status | Notes |
|-----|----------|--------|-------|
| Boots | 100% (3/3) | 🟢 Perfect | Critical for deterrent system |
| Chi | 100% (3/3) | 🟢 Perfect | User's cat correctly identified |
| Kappa | 33% (1/3) | 🔴 Needs attention | Often confused with Boots |
| Mu | 67% (2/3) | 🟡 Good | One server error occurred |
| Tau | 100% (3/3) | 🟢 Perfect | User's cat correctly identified |
| Wolf | 100% (3/3) | 🟢 Perfect | User's cat correctly identified |

### Key Insights
- **Boots detection is 100% accurate** - Deterrent system will work correctly
- **No false negatives** - System won't miss actual intruders
- **Kappa confusion** - May trigger false deterrent activations (acceptable for territory management)

## Output

The test generates detailed reports including:

- Overall accuracy percentage
- Per-cat accuracy breakdown
- Confusion matrix
- Detailed analysis of incorrect predictions
- Error reporting for failed API calls

Results are saved to timestamped JSON files for further analysis.

## Requirements

- Node.js and npm (version 16.0.0 or higher)
- Access to BootBoots API endpoint
- Valid API key configured in test file

**Note**: This test folder is self-contained with its own `package.json` and dependencies. No setup required in parent directories.

## Configuration

Edit `cat-detection-accuracy.test.ts` to modify:

- API endpoint URL
- API key
- Test image paths
- Specific test images
- Number of images per cat

## Troubleshooting

### Common Issues

1. **TypeScript compilation errors**: Ensure all dependencies are installed with `npm install`
2. **API authentication errors**: Verify the API key is correct
3. **Image not found errors**: Check that the image paths exist in the sanitized images directory
4. **HTTP 500 errors**: Some images may cause server errors; these are logged but don't fail the test

### Dependencies

The test folder is self-contained. If you encounter missing dependencies:

```bash
cd /Users/martinharris/repos/nakomis/bootboots/test
npm install
```

Or use the shell script which automatically installs dependencies:

```bash
./run-cat-detection-test.sh
```

## Development

To modify the test:

1. Edit `cat-detection-accuracy.test.ts`
2. Use npm scripts for easy development:

```bash
# Build the TypeScript file
npm run build

# Run tests
npm run test              # specific images
npm run test:random       # random images
npm run test:specific     # specific images (explicit)

# Clean generated files
npm run clean
```

Or use the shell script which handles everything automatically:

```bash
./run-cat-detection-test.sh
```

## Integration

This test system is designed to be integrated into CI/CD pipelines for continuous validation of the BootBoots cat detection system accuracy.
