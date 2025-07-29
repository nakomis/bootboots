import * as fs from 'fs';
import * as path from 'path';
import fetch from 'node-fetch';
import { API_ENDPOINT, API_KEY } from './secrets';

// Configuration
const IMAGES_DIR = '/Users/martinharris/repos/nakomis/bootboots/sort-images/sanitizedimages';
const CAT_NAMES = ['Boots', 'Chi', 'Kappa', 'Mu', 'Tau', 'Wolf'];

// Default specific images to test (from previous successful test run)
const DEFAULT_TEST_IMAGES = {
    'Boots': [
        '600.jpeg.jpg',
        '609.jpeg.jpg', 
        '1614.jpeg.jpg'
    ],
    'Chi': [
        'IMG_2665.jpeg.jpg',
        'IMG_2666.jpeg.jpg',
        'IMG_2667.jpeg.jpg'
    ],
    'Kappa': [
        '1171.jpeg.jpg',
        'IMG_2652.jpeg.jpg',
        '1616.jpeg.jpg'
    ],
    'Mu': [
        'IMG_0127.jpeg.jpg',
        'IMG_0540.jpeg.jpg',
        'IMG_0153.jpeg.jpg'
    ],
    'Tau': [
        'IMG_2368.jpeg.jpg',
        '453.jpeg.jpg',
        '1015.jpeg.jpg'
    ],
    'Wolf': [
        '1486.jpeg.jpg',
        '1423.jpeg.jpg',
        '1428.jpeg.jpg'
    ]
};

interface ApiResponse {
    success: boolean;
    data: {
        probabilities: number[];
    };
    mostLikelyCat: {
        name: string;
        confidence: number;
        index: number;
    };
    metadata: {
        endpointName: string;
        contentType: string;
        timestamp: string;
        s3ImageKey: string | null;
    };
}

interface TestResult {
    catName: string;
    expectedIndex: number;
    imagePath: string;
    imageFileName: string;
    predictedCat: string;
    predictedIndex: number;
    confidence: number;
    correct: boolean;
    probabilities: number[];
    error?: string;
}

interface TestSummary {
    testMode: 'specific' | 'random';
    totalTests: number;
    correctPredictions: number;
    accuracy: number;
    results: TestResult[];
    perCatAccuracy: { [catName: string]: { correct: number; total: number; accuracy: number } };
}

// Helper function to get random images from a directory
function getRandomImages(dirPath: string, count: number): string[] {
    const files = fs.readdirSync(dirPath)
        .filter(file => file.toLowerCase().endsWith('.jpg') || file.toLowerCase().endsWith('.jpeg'))
        .map(file => path.join(dirPath, file));
    
    // Shuffle array and take first 'count' items
    const shuffled = files.sort(() => 0.5 - Math.random());
    return shuffled.slice(0, Math.min(count, files.length));
}

// Helper function to get specific or random images
function getTestImages(catName: string, useRandom: boolean): string[] {
    const catDir = path.join(IMAGES_DIR, catName);
    
    if (!fs.existsSync(catDir)) {
        console.log(`  ⚠️  Directory not found: ${catDir}`);
        return [];
    }
    
    if (useRandom) {
        return getRandomImages(catDir, 3);
    } else {
        // Use specific images, fallback to random if specific images don't exist
        const specificImages = DEFAULT_TEST_IMAGES[catName] || [];
        const imagePaths: string[] = [];
        
        for (const fileName of specificImages) {
            const fullPath = path.join(catDir, fileName);
            if (fs.existsSync(fullPath)) {
                imagePaths.push(fullPath);
            } else {
                console.log(`  ⚠️  Specific image not found: ${fileName}, will use random fallback`);
            }
        }
        
        // If we don't have enough specific images, fill with random ones
        if (imagePaths.length < 3) {
            const availableFiles = fs.readdirSync(catDir)
                .filter(file => file.toLowerCase().endsWith('.jpg') || file.toLowerCase().endsWith('.jpeg'))
                .filter(file => !specificImages.includes(file)) // Exclude already selected specific images
                .map(file => path.join(catDir, file));
            
            const shuffled = availableFiles.sort(() => 0.5 - Math.random());
            const needed = 3 - imagePaths.length;
            imagePaths.push(...shuffled.slice(0, needed));
        }
        
        return imagePaths.slice(0, 3);
    }
}

// Function to test a single image
async function testImage(imagePath: string, expectedCatName: string, expectedIndex: number): Promise<TestResult> {
    const imageFileName = path.basename(imagePath);
    
    try {
        console.log(`Testing ${expectedCatName}: ${imageFileName}`);
        
        const imageBuffer = fs.readFileSync(imagePath);
        
        const response = await fetch(API_ENDPOINT, {
            method: 'POST',
            headers: {
                'X-API-Key': API_KEY,
                'Content-Type': 'image/jpeg'
            },
            body: imageBuffer
        });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const apiResponse = await response.json() as ApiResponse;
        
        if (!apiResponse.success) {
            throw new Error('API returned success: false');
        }

        const result: TestResult = {
            catName: expectedCatName,
            expectedIndex,
            imagePath,
            imageFileName,
            predictedCat: apiResponse.mostLikelyCat.name,
            predictedIndex: apiResponse.mostLikelyCat.index,
            confidence: apiResponse.mostLikelyCat.confidence,
            correct: apiResponse.mostLikelyCat.name === expectedCatName,
            probabilities: apiResponse.data.probabilities
        };

        const status = result.correct ? '✅' : '❌';
        console.log(`  ${status} Predicted: ${result.predictedCat} (${(result.confidence * 100).toFixed(1)}%) | Expected: ${expectedCatName}`);
        
        return result;
        
    } catch (error) {
        console.error(`  ❌ Error testing ${imagePath}:`, error);
        return {
            catName: expectedCatName,
            expectedIndex,
            imagePath,
            imageFileName,
            predictedCat: 'ERROR',
            predictedIndex: -1,
            confidence: 0,
            correct: false,
            probabilities: [],
            error: error instanceof Error ? error.message : String(error)
        };
    }
}

// Main test function
async function runCatDetectionAccuracyTest(useRandom: boolean = false): Promise<TestSummary> {
    const testMode = useRandom ? 'random' : 'specific';
    console.log('🐱 Starting Cat Detection Accuracy Test\n');
    console.log(`Test Mode: ${testMode.toUpperCase()}`);
    console.log(`Images Directory: ${IMAGES_DIR}\n`);
    
    const allResults: TestResult[] = [];
    const perCatAccuracy: { [catName: string]: { correct: number; total: number; accuracy: number } } = {};
    
    for (let i = 0; i < CAT_NAMES.length; i++) {
        const catName = CAT_NAMES[i];
        
        console.log(`\n📁 Testing ${catName} (Expected Index: ${i}) - ${testMode} mode`);
        console.log('─'.repeat(60));
        
        const testImages = getTestImages(catName, useRandom);
        
        if (testImages.length === 0) {
            console.log(`  ⚠️  No images found for ${catName}`);
            continue;
        }
        
        const totalInDir = fs.existsSync(path.join(IMAGES_DIR, catName)) 
            ? fs.readdirSync(path.join(IMAGES_DIR, catName)).length 
            : 0;
        console.log(`  Found ${totalInDir} images in directory, testing ${testImages.length}`);
        
        if (!useRandom) {
            console.log(`  Using specific images: ${testImages.map(p => path.basename(p)).join(', ')}`);
        }
        console.log('');
        
        let correctForThisCat = 0;
        
        for (const imagePath of testImages) {
            const result = await testImage(imagePath, catName, i);
            allResults.push(result);
            
            if (result.correct) {
                correctForThisCat++;
            }
            
            // Add a small delay to avoid overwhelming the API
            await new Promise(resolve => setTimeout(resolve, 1000));
        }
        
        perCatAccuracy[catName] = {
            correct: correctForThisCat,
            total: testImages.length,
            accuracy: testImages.length > 0 ? (correctForThisCat / testImages.length) * 100 : 0
        };
        
        console.log(`\n  📊 ${catName} Accuracy: ${correctForThisCat}/${testImages.length} (${perCatAccuracy[catName].accuracy.toFixed(1)}%)`);
    }
    
    const totalTests = allResults.length;
    const correctPredictions = allResults.filter(r => r.correct).length;
    const overallAccuracy = totalTests > 0 ? (correctPredictions / totalTests) * 100 : 0;
    
    const summary: TestSummary = {
        testMode,
        totalTests,
        correctPredictions,
        accuracy: overallAccuracy,
        results: allResults,
        perCatAccuracy
    };
    
    return summary;
}

// Function to print detailed results
function printDetailedResults(summary: TestSummary) {
    console.log('\n' + '='.repeat(60));
    console.log(`📊 DETAILED TEST RESULTS (${summary.testMode.toUpperCase()} MODE)`);
    console.log('='.repeat(60));
    
    console.log(`\n🎯 Overall Accuracy: ${summary.correctPredictions}/${summary.totalTests} (${summary.accuracy.toFixed(1)}%)\n`);
    
    console.log('📈 Per-Cat Accuracy:');
    console.log('─'.repeat(40));
    for (const [catName, stats] of Object.entries(summary.perCatAccuracy)) {
        const status = stats.accuracy >= 80 ? '🟢' : stats.accuracy >= 60 ? '🟡' : '🔴';
        console.log(`${status} ${catName.padEnd(8)}: ${stats.correct}/${stats.total} (${stats.accuracy.toFixed(1)}%)`);
    }
    
    console.log('\n🔍 Incorrect Predictions:');
    console.log('─'.repeat(40));
    const incorrectResults = summary.results.filter(r => !r.correct && r.predictedCat !== 'ERROR');
    
    if (incorrectResults.length === 0) {
        console.log('🎉 All predictions were correct!');
    } else {
        for (const result of incorrectResults) {
            console.log(`❌ ${result.catName} → ${result.predictedCat} (${(result.confidence * 100).toFixed(1)}%)`);
            console.log(`   Image: ${result.imageFileName}`);
            
            // Show top 3 probabilities for analysis
            const sortedProbs = result.probabilities
                .map((prob, idx) => ({ name: CAT_NAMES[idx], prob, idx }))
                .sort((a, b) => b.prob - a.prob)
                .slice(0, 3);
            
            console.log(`   Top 3: ${sortedProbs.map(p => `${p.name}(${(p.prob * 100).toFixed(1)}%)`).join(', ')}`);
            console.log('');
        }
    }
    
    // Show errors if any
    const errorResults = summary.results.filter(r => r.predictedCat === 'ERROR');
    if (errorResults.length > 0) {
        console.log('\n⚠️  Errors:');
        console.log('─'.repeat(40));
        for (const result of errorResults) {
            console.log(`❌ ${result.catName}: ${result.imageFileName}`);
            console.log(`   Error: ${result.error}`);
            console.log('');
        }
    }
    
    console.log('\n💾 Confusion Matrix:');
    console.log('─'.repeat(40));
    const matrix: { [expected: string]: { [predicted: string]: number } } = {};
    
    // Initialize matrix
    for (const catName of CAT_NAMES) {
        matrix[catName] = {};
        for (const predictedCat of CAT_NAMES) {
            matrix[catName][predictedCat] = 0;
        }
    }
    
    // Fill matrix
    for (const result of summary.results) {
        if (result.predictedCat !== 'ERROR') {
            matrix[result.catName][result.predictedCat]++;
        }
    }
    
    // Print matrix
    console.log('Expected \\ Predicted:');
    const header = '        ' + CAT_NAMES.map(name => name.padEnd(6)).join(' ');
    console.log(header);
    
    for (const expectedCat of CAT_NAMES) {
        const row = expectedCat.padEnd(8) + CAT_NAMES.map(predictedCat => 
            matrix[expectedCat][predictedCat].toString().padEnd(6)
        ).join(' ');
        console.log(row);
    }
}

// Parse command line arguments
function parseArgs(): { useRandom: boolean; help: boolean } {
    const args = process.argv.slice(2);
    let useRandom = false;
    let help = false;
    
    // Check environment variable
    if (process.env.USE_RANDOM === 'true' || process.env.USE_RANDOM === '1') {
        useRandom = true;
    }
    
    // Check command line arguments
    for (const arg of args) {
        if (arg === '--random' || arg === '-r') {
            useRandom = true;
        } else if (arg === '--help' || arg === '-h') {
            help = true;
        }
    }
    
    return { useRandom, help };
}

function printHelp() {
    console.log(`
🐱 Cat Detection Accuracy Test

Usage:
  node cat-detection-accuracy.test.js [options]
  
Options:
  --random, -r    Use random image selection instead of specific test images
  --help, -h      Show this help message

Environment Variables:
  USE_RANDOM=true    Use random image selection (same as --random flag)

Test Modes:
  SPECIFIC (default): Uses predefined set of specific images for consistent testing
  RANDOM (--random):  Uses 3 randomly selected images per cat for varied testing

Examples:
  node cat-detection-accuracy.test.js              # Use specific images
  node cat-detection-accuracy.test.js --random     # Use random images
  USE_RANDOM=true node cat-detection-accuracy.test.js  # Use random via env var
`);
}

// Run the test
async function main() {
    try {
        const { useRandom, help } = parseArgs();
        
        if (help) {
            printHelp();
            process.exit(0);
        }
        
        const summary = await runCatDetectionAccuracyTest(useRandom);
        printDetailedResults(summary);
        
        // Save results to JSON file
        const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
        const resultsFile = path.join(__dirname, `cat-detection-results-${summary.testMode}-${timestamp}.json`);
        fs.writeFileSync(resultsFile, JSON.stringify(summary, null, 2));
        console.log(`\n💾 Results saved to: ${resultsFile}`);
        
        // Exit with appropriate code
        process.exit(summary.accuracy >= 80 ? 0 : 1);
        
    } catch (error) {
        console.error('❌ Test failed:', error);
        process.exit(1);
    }
}

// Run if this file is executed directly
if (require.main === module) {
    main();
}

export { runCatDetectionAccuracyTest, TestSummary, TestResult };
