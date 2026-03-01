import json
import os
import threading

import functions_framework
import paho.mqtt.client as mqtt
from google.cloud import pubsub_v1

MQTT_BROKER = os.environ.get("MQTT_BROKER", "broker.hivemq.com")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "8883"))
MQTT_USER = os.environ.get("MQTT_USER", "")
MQTT_PASS = os.environ.get("MQTT_PASS", "")
MQTT_TOPIC = os.environ.get("MQTT_TOPIC", "weatherprobe/#")

GCP_PROJECT = os.environ.get("GCP_PROJECT", "")
PUBSUB_TOPIC = os.environ.get("PUBSUB_TOPIC", "weatherprobe-readings")

publisher = pubsub_v1.PublisherClient()
topic_path = publisher.topic_path(GCP_PROJECT, PUBSUB_TOPIC)


def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        json.loads(payload)
        future = publisher.publish(topic_path, payload.encode("utf-8"))
        future.result(timeout=10)
        print(f"Published to Pub/Sub: {msg.topic}")
    except Exception as e:
        print(f"Error forwarding message: {e}")


def start_mqtt():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.tls_set()
    client.on_message = on_message
    client.connect(MQTT_BROKER, MQTT_PORT)
    client.subscribe(MQTT_TOPIC, qos=1)
    client.loop_forever()


@functions_framework.http
def health(request):
    return "OK", 200


mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)
mqtt_thread.start()
