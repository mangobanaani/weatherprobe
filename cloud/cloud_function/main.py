import json
import logging
import os

import functions_framework
from google.cloud import pubsub_v1

logging.basicConfig(level=logging.INFO)
log = logging.getLogger(__name__)

GCP_PROJECT = os.environ.get("GCP_PROJECT", "")
PUBSUB_TOPIC = os.environ.get("PUBSUB_TOPIC", "weatherprobe-readings")
WEBHOOK_SECRET = os.environ.get("WEBHOOK_SECRET", "")

publisher = pubsub_v1.PublisherClient()
topic_path = publisher.topic_path(GCP_PROJECT, PUBSUB_TOPIC)


@functions_framework.http
def ingest(request):
    """Receives MQTT messages forwarded by HiveMQ Data Hub webhook."""
    if WEBHOOK_SECRET and request.headers.get("X-Webhook-Secret") != WEBHOOK_SECRET:
        return "Unauthorized", 401

    body = request.get_data(as_text=True)
    if not body:
        return "Empty body", 400

    try:
        data = json.loads(body)
    except json.JSONDecodeError:
        return "Invalid JSON", 400

    # HiveMQ webhook wraps the payload; extract it if nested
    payload = data.get("payload", data)
    if isinstance(payload, str):
        payload = json.loads(payload)

    payload_bytes = json.dumps(payload).encode("utf-8")
    future = publisher.publish(topic_path, payload_bytes)
    future.result(timeout=10)

    log.info("Published %d bytes to Pub/Sub", len(payload_bytes))
    return "OK", 200
