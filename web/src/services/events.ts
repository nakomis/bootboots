import { signedFetch } from '../aws/signedFetch';
import type { AwsServices } from './aws';

export interface ClaudeResult {
  cat: string;
  confidence: string;
  reasoning: string;
}

export interface CatcamEvent {
  id: string;
  timestamp: string;
  imageName: string;
  bootsConfidence: number;
  /** Presigned by the events Lambda. */
  imageUrl: string | null;
  claudeResult?: ClaudeResult;
}

/** Catcam detections and near-misses from the bootboots events API (IAM auth). */
export async function getEvents(
  aws: AwsServices,
  minConfidence = 0.5,
  fetchImpl: typeof fetch = fetch,
): Promise<CatcamEvent[]> {
  const url = `${aws.config.eventsApiUrl}?minConfidence=${minConfidence}`;
  const response = await signedFetch(url, aws.credentials, aws.config.region, {}, fetchImpl);
  if (!response.ok) {
    const body = await response.text().catch(() => '');
    throw new Error(`Events API error: ${response.status} ${response.statusText}${body ? ` - ${body}` : ''}`);
  }
  return response.json();
}
