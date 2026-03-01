#!/bin/bash
set -euo pipefail

PROJECT_ID="${1:?Usage: $0 <gcp-project-id>}"

echo "Creating Pub/Sub topic..."
gcloud pubsub topics create weatherprobe-readings \
    --project="$PROJECT_ID" \
    2>/dev/null || echo "Topic already exists"

echo "Creating dead-letter topic..."
gcloud pubsub topics create weatherprobe-dead-letter \
    --project="$PROJECT_ID" \
    2>/dev/null || echo "Dead letter topic already exists"

echo "Creating subscription for downstream processing..."
gcloud pubsub subscriptions create weatherprobe-sub \
    --topic=weatherprobe-readings \
    --project="$PROJECT_ID" \
    --ack-deadline=60 \
    --dead-letter-topic=weatherprobe-dead-letter \
    --max-delivery-attempts=5 \
    2>/dev/null || echo "Subscription already exists"

echo "Done. Topic: projects/$PROJECT_ID/topics/weatherprobe-readings"
