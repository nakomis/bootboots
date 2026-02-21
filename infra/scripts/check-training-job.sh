#!/bin/bash
#
# Check the status of the most recent SageMaker training job.
# Prints status, duration so far, and final metrics if complete.
#
# Usage:
#   ./scripts/check-training-job.sh
#   ./scripts/check-training-job.sh <job-name>   # check a specific job
#

set -e

export AWS_PROFILE="${AWS_PROFILE:-nakom.is-sandbox}"
REGION="eu-west-2"
JOB_NAME="${1:-}"

# If no job name given, find the most recent one
if [ -z "$JOB_NAME" ]; then
    JOB_NAME=$(aws sagemaker list-training-jobs \
        --region "$REGION" \
        --sort-by CreationTime \
        --sort-order Descending \
        --max-results 1 \
        --output json | jq -r '.TrainingJobSummaries[0].TrainingJobName')
fi

echo ""
echo "Job: $JOB_NAME"
echo "======================================================"

JOB=$(aws sagemaker describe-training-job \
    --training-job-name "$JOB_NAME" \
    --region "$REGION" \
    --output json)

STATUS=$(echo "$JOB" | jq -r '.TrainingJobStatus')
SECONDARY=$(echo "$JOB" | jq -r '.SecondaryStatus')
CREATED=$(echo "$JOB" | jq -r '.CreationTime')
STARTED=$(echo "$JOB" | jq -r '.TrainingStartTime // "not started"')
ENDED=$(echo "$JOB" | jq -r '.TrainingEndTime // "still running"')

echo "Status:           $STATUS ($SECONDARY)"
echo "Created:          $CREATED"
echo "Training started: $STARTED"
echo "Training ended:   $ENDED"

# Duration
if [ "$STARTED" != "not started" ] && [ "$ENDED" != "still running" ]; then
    START_EPOCH=$(date -j -f "%Y-%m-%dT%H:%M:%S" "${STARTED%.*}" "+%s" 2>/dev/null || date -d "$STARTED" "+%s")
    END_EPOCH=$(date -j -f "%Y-%m-%dT%H:%M:%S" "${ENDED%.*}" "+%s" 2>/dev/null || date -d "$ENDED" "+%s")
    DURATION=$(( END_EPOCH - START_EPOCH ))
    echo "Duration:         $(( DURATION / 60 ))m $(( DURATION % 60 ))s"
elif [ "$STARTED" != "not started" ]; then
    NOW=$(date "+%s")
    START_EPOCH=$(date -j -f "%Y-%m-%dT%H:%M:%S" "${STARTED%.*}" "+%s" 2>/dev/null || date -d "$STARTED" "+%s")
    ELAPSED=$(( NOW - START_EPOCH ))
    echo "Elapsed:          $(( ELAPSED / 60 ))m $(( ELAPSED % 60 ))s"
fi

# Failure reason
if [ "$STATUS" = "Failed" ]; then
    REASON=$(echo "$JOB" | jq -r '.FailureReason // "unknown"')
    echo ""
    echo "Failure reason:"
    echo "$REASON" | head -20
fi

# Final metrics
METRICS=$(echo "$JOB" | jq -r '.FinalMetricDataList // []')
if [ "$METRICS" != "[]" ]; then
    echo ""
    echo "Final metrics:"
    echo "$JOB" | jq -r '.FinalMetricDataList[] | "  \(.MetricName): \(.Value | . * 10000 | round / 10000)"'
fi

# Model artifacts
if [ "$STATUS" = "Completed" ]; then
    ARTIFACTS=$(echo "$JOB" | jq -r '.ModelArtifacts.S3ModelArtifacts // "none"')
    echo ""
    echo "Model artifacts: $ARTIFACTS"
    echo ""
    echo "To deploy, run:"
    echo "  cd infra && AWS_PROFILE=nakom.is-sandbox npx cdk deploy BootBootsAiTrainingStack \\"
    echo "    --context modelDataUrl=$ARTIFACTS"
fi

echo "======================================================"
echo ""
