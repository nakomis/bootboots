#!/bin/bash

# BootBoots Cat Detection Accuracy Test Runner
# Usage: ./run-cat-detection-test.sh [--random]

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory (test folder is the project root)
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${BLUE}🐱 BootBoots Cat Detection Test Runner${NC}"
echo -e "${BLUE}======================================${NC}"

# Parse arguments
USE_RANDOM=false
SHOW_HELP=false

for arg in "$@"; do
    case $arg in
        --random|-r)
            USE_RANDOM=true
            shift
            ;;
        --help|-h)
            SHOW_HELP=true
            shift
            ;;
        *)
            echo -e "${RED}Unknown argument: $arg${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Show help if requested
if [ "$SHOW_HELP" = true ]; then
    echo ""
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --random, -r    Use random image selection instead of specific test images"
    echo "  --help, -h      Show this help message"
    echo ""
    echo "Test Modes:"
    echo "  SPECIFIC (default): Uses predefined set of specific images for consistent testing"
    echo "  RANDOM (--random):  Uses 3 randomly selected images per cat for varied testing"
    echo ""
    echo "Examples:"
    echo "  $0              # Use specific images"
    echo "  $0 --random     # Use random images"
    echo ""
    exit 0
fi

# Check if we're in the right directory
if [ ! -f "$TEST_DIR/cat-detection-accuracy.test.ts" ]; then
    echo -e "${RED}❌ Error: cat-detection-accuracy.test.ts not found in $TEST_DIR${NC}"
    echo "Make sure you're running this script from the test directory"
    exit 1
fi

# Check if Node.js is installed
if ! command -v node &> /dev/null; then
    echo -e "${RED}❌ Error: Node.js is not installed${NC}"
    echo "Please install Node.js to run this test"
    exit 1
fi

# Check if TypeScript compiler is available
if ! command -v npx &> /dev/null; then
    echo -e "${RED}❌ Error: npx is not available${NC}"
    echo "Please install npm to run this test"
    exit 1
fi

# Navigate to test directory (which is our project root)
cd "$TEST_DIR"

# Check if node_modules exists in test directory
if [ ! -d "node_modules" ]; then
    echo -e "${YELLOW}⚠️  node_modules not found in test directory. Installing dependencies...${NC}"
    npm install
fi

# Determine test mode
if [ "$USE_RANDOM" = true ]; then
    echo -e "${YELLOW}🎲 Running test with RANDOM image selection${NC}"
    TEST_MODE="random"
else
    echo -e "${GREEN}📋 Running test with SPECIFIC image selection${NC}"
    TEST_MODE="specific"
fi

echo ""
echo -e "${BLUE}🔧 Compiling TypeScript...${NC}"

# Compile TypeScript to JavaScript
if ! npx tsc cat-detection-accuracy.test.ts --target es2020 --module commonjs --esModuleInterop --skipLibCheck --resolveJsonModule; then
    echo -e "${RED}❌ TypeScript compilation failed${NC}"
    exit 1
fi

echo -e "${GREEN}✅ TypeScript compiled successfully${NC}"
echo ""

# Run the test
echo -e "${BLUE}🚀 Starting cat detection accuracy test...${NC}"
echo ""

# Set environment variable if using random mode
if [ "$USE_RANDOM" = true ]; then
    export USE_RANDOM=true
fi

# Run the compiled JavaScript
if node cat-detection-accuracy.test.js $([ "$USE_RANDOM" = true ] && echo "--random"); then
    echo ""
    echo -e "${GREEN}🎉 Test completed successfully!${NC}"
    
    # Show results file location
    RESULTS_FILE=$(ls -t cat-detection-results-${TEST_MODE}-*.json 2>/dev/null | head -n1)
    if [ -n "$RESULTS_FILE" ]; then
        echo -e "${BLUE}📄 Results saved to: $TEST_DIR/$RESULTS_FILE${NC}"
    fi
    
    exit 0
else
    echo ""
    echo -e "${RED}❌ Test failed or accuracy below threshold${NC}"
    
    # Show results file location even on failure
    RESULTS_FILE=$(ls -t cat-detection-results-${TEST_MODE}-*.json 2>/dev/null | head -n1)
    if [ -n "$RESULTS_FILE" ]; then
        echo -e "${BLUE}📄 Results saved to: $TEST_DIR/$RESULTS_FILE${NC}"
    fi
    
    exit 1
fi
