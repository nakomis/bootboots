#!/usr/bin/env ts-node
/**
 * BootBoots local inference server
 *
 * Loads a TFJs GraphModel (exported by export_tfjs.py) and exposes a single
 * POST /predict endpoint that accepts raw JPEG bytes and returns the top
 * class prediction and confidence score.
 *
 * Usage:
 *   npm start                             # loads models_multiclass/tfjs_model by default
 *   npm start -- --model ../models/tfjs_model  # use binary model instead
 *
 * The sandbox SPA at sandbox.nakomis.com calls this server when labelling images.
 */

import cors from 'cors';
import express, { NextFunction, Request, Response } from 'express';
import * as fs from 'fs';
import * as path from 'path';
import * as tf from '@tensorflow/tfjs-node';

const PORT = 8765;
const IMG_SIZE = 224;

// ─── CLI args ────────────────────────────────────────────────────────────────

const args = process.argv.slice(2);
const modelFlagIdx = args.indexOf('--model');

// Default: multiclass model, two levels up from serve/src/
const DEFAULT_MODEL_DIR = path.resolve(__dirname, '../../models_multiclass/tfjs_model');
const modelDir = modelFlagIdx !== -1
    ? path.resolve(args[modelFlagIdx + 1])
    : DEFAULT_MODEL_DIR;

const modelJsonPath = path.join(modelDir, 'model.json');
// class_names.json lives one directory above the tfjs_model/ directory
const classNamesPath = path.join(modelDir, '..', 'class_names.json');

// ─── Startup ─────────────────────────────────────────────────────────────────

async function loadAssets(): Promise<{ model: tf.GraphModel; classNames: string[] }> {
    if (!fs.existsSync(modelJsonPath)) {
        console.error(`Model not found: ${modelJsonPath}`);
        console.error('Run "python export_tfjs.py" in local-training/ to convert the .keras model first.');
        process.exit(1);
    }
    if (!fs.existsSync(classNamesPath)) {
        console.error(`class_names.json not found: ${classNamesPath}`);
        process.exit(1);
    }

    const classNames: string[] = JSON.parse(fs.readFileSync(classNamesPath, 'utf-8'));
    console.log(`Classes (${classNames.length}): ${classNames.join(', ')}`);

    console.log(`Loading model from ${modelDir} ...`);
    const model = await tf.loadGraphModel(`file://${modelJsonPath}`);
    console.log('Model loaded.');

    return { model, classNames };
}

// ─── Inference ───────────────────────────────────────────────────────────────

interface Prediction {
    prediction: string;
    confidence: number;
}

function runInference(model: tf.GraphModel, classNames: string[], imageBuffer: Buffer): Prediction {
    // Decode JPEG → int32 HWC tensor
    const imgTensor = tf.node.decodeImage(imageBuffer, 3) as tf.Tensor3D;

    // Resize to [224, 224], cast to float32 (values remain in [0, 255])
    // The model has mobilenet_v2.preprocess_input baked in, which handles
    // normalisation to [-1, 1] internally.
    const resized = tf.image.resizeBilinear(imgTensor, [IMG_SIZE, IMG_SIZE]);
    const float32 = resized.toFloat();
    const batched = float32.expandDims(0) as tf.Tensor4D;

    const outputTensor = model.predict(batched) as tf.Tensor2D;
    const probs = Array.from(outputTensor.dataSync());

    // Dispose all intermediate tensors
    imgTensor.dispose();
    resized.dispose();
    float32.dispose();
    batched.dispose();
    outputTensor.dispose();

    const maxIdx = probs.reduce((best, p, i) => (p > probs[best] ? i : best), 0);
    return {
        prediction: classNames[maxIdx],
        confidence: Math.round(probs[maxIdx] * 1000) / 1000,
    };
}

// ─── Express app ─────────────────────────────────────────────────────────────

async function main() {
    const { model, classNames } = await loadAssets();

    const app = express();
    app.use(cors());

    // Health check — useful for the SPA to detect whether the server is running
    app.get('/health', (_req: Request, res: Response) => {
        res.json({ status: 'ok', classes: classNames });
    });

    // Predict — accepts raw JPEG bytes, returns { prediction, confidence }
    app.post(
        '/predict',
        express.raw({ type: 'application/octet-stream', limit: '10mb' }),
        (req: Request, res: Response, next: NextFunction) => {
            try {
                const imageBuffer = req.body as Buffer;
                if (!imageBuffer || imageBuffer.length === 0) {
                    res.status(400).json({ error: 'Empty request body — send raw JPEG bytes' });
                    return;
                }
                const result = runInference(model, classNames, imageBuffer);
                res.json(result);
            } catch (err) {
                next(err);
            }
        },
    );

    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    app.use((err: Error, _req: Request, res: Response, _next: NextFunction) => {
        console.error('Unhandled error:', err);
        res.status(500).json({ error: 'Inference failed' });
    });

    app.listen(PORT, () => {
        console.log(`\nBootBoots inference server  →  http://localhost:${PORT}`);
        console.log('POST /predict   send raw JPEG bytes → { prediction, confidence }');
        console.log('GET  /health    check server is up\n');
    });
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
