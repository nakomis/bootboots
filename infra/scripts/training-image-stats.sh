#!/bin/bash
#
# Training Image Statistics
#
# Shows the count of labeled training images per cat in the catadata DynamoDB table.
#
# Usage:
#   ./scripts/training-image-stats.sh
#
# Requires: AWS CLI configured with appropriate credentials
#

set -e

export AWS_PROFILE="${AWS_PROFILE:-nakom.is-sandbox}"
TABLE_NAME="catadata"
REGION="eu-west-2"

echo ""
echo "Scanning DynamoDB table: $TABLE_NAME..."
echo ""

# Scan the table and count by cat label
aws dynamodb scan \
    --table-name "$TABLE_NAME" \
    --region "$REGION" \
    --projection-expression "cat" \
    --output json | \
jq -r '
    .Items |
    group_by(.cat.S // "UNLABELED") |
    map({cat: .[0].cat.S // "UNLABELED", count: length}) |
    sort_by(.cat) |
    .[] |
    "\(.cat)\t\(.count)"
' | \
awk -F'\t' '
BEGIN {
    print "=================================================="
    print "  TRAINING IMAGE STATISTICS"
    print "=================================================="
    print ""
    total = 0
    unlabeled = 0
}
{
    cat = $1
    count = $2

    if (cat == "UNLABELED") {
        unlabeled = count
    } else {
        cats[cat] = count
        total += count
        if (count > max) max = count
    }
}
END {
    # Define expected cats in order
    split("Boots,Chi,Kappa,Mu,NoCat,Tau,Wolf", expected, ",")

    # Print each cat with bar chart
    for (i = 1; i <= 7; i++) {
        cat = expected[i]
        count = cats[cat] + 0
        pct = (total > 0) ? (count / total * 100) : 0

        # Build bar
        bar_len = (max > 0) ? int(count / max * 30) : 0
        bar = ""
        for (j = 0; j < bar_len; j++) bar = bar "#"
        for (j = bar_len; j < 30; j++) bar = bar "."

        printf "  %-8s %s %5d (%5.1f%%)\n", cat, bar, count, pct
    }

    print ""
    print "--------------------------------------------------"
    printf "  Total labeled:     %d\n", total
    printf "  Unlabeled:         %d\n", unlabeled
    printf "  Total records:     %d\n", total + unlabeled
    print "--------------------------------------------------"
    print ""
    print "=================================================="
}
'
