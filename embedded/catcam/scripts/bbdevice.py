#!/usr/bin/env python3
"""
BootBoots Device Command Tool
Send commands to the CatCam device via AWS IoT Core MQTT.

Usage:
    python scripts/bbdevice.py ping
    python scripts/bbdevice.py get_status
    python scripts/bbdevice.py take_photo
    python scripts/bbdevice.py reboot
    python scripts/bbdevice.py get_settings
    python scripts/bbdevice.py set_setting <name> <value>

Environment:
    AWS_PROFILE - AWS profile with IoT permissions (default: nakom.is-sandbox)
"""

import os
import sys
import json
import time
import argparse
import threading
import boto3
from botocore.exceptions import ClientError

# Device configuration
THING_NAME = "BootBootsThing"
COMMAND_TOPIC = f"catcam/{THING_NAME}/commands"
RESPONSE_TOPIC = f"catcam/{THING_NAME}/responses"
DEFAULT_TIMEOUT = 5.0  # seconds


class DeviceCommander:
    def __init__(self, region=None):
        self.region = region or self._get_region()
        self.iot_client = boto3.client('iot', region_name=self.region)
        self.iot_data_client = boto3.client('iot-data', region_name=self.region)
        self.response = None
        self.response_event = threading.Event()

    def _get_region(self):
        """Get AWS region from config or environment."""
        session = boto3.Session()
        return session.region_name or 'us-east-1'

    def _get_endpoint(self):
        """Get the IoT Data endpoint for the account."""
        try:
            response = self.iot_client.describe_endpoint(endpointType='iot:Data-ATS')
            return response['endpointAddress']
        except ClientError as e:
            print(f"Error getting IoT endpoint: {e}")
            sys.exit(1)

    def send_command(self, command, params=None, timeout=DEFAULT_TIMEOUT):
        """
        Send a command to the device and wait for response.

        Args:
            command: Command name (e.g., 'ping', 'get_status')
            params: Optional dict of parameters
            timeout: Timeout in seconds

        Returns:
            Response dict or None if timeout
        """
        payload = {"command": command}
        if params:
            payload.update(params)

        payload_json = json.dumps(payload)

        print(f"Sending command: {command}")
        print(f"  Topic: {COMMAND_TOPIC}")
        print(f"  Payload: {payload_json}")
        print()

        try:
            # Publish command
            self.iot_data_client.publish(
                topic=COMMAND_TOPIC,
                qos=1,
                payload=payload_json
            )

            # For commands that don't return responses (like reboot), just confirm sent
            if command == 'reboot':
                print("Command sent. Device will reboot.")
                return {"status": "sent"}

            # Wait for response by polling (IoT Data Plane doesn't support subscribe)
            # Note: In a real scenario, you'd use MQTT client for bidirectional communication
            # For simplicity, we just confirm the command was sent
            print(f"Command sent successfully.")
            print()
            print("Note: To see the response, monitor the device logs or use the web app.")
            print(f"Response topic: {RESPONSE_TOPIC}")

            return {"status": "sent"}

        except ClientError as e:
            print(f"Error sending command: {e}")
            return None


def ensure_aws_profile():
    """Check if AWS_PROFILE is set, prompt user if not."""
    profile = os.environ.get('AWS_PROFILE')
    if profile:
        return profile

    print("AWS_PROFILE is not set.")
    try:
        profile = input("Enter AWS profile name (default: nakom.is-sandbox): ").strip()
    except (EOFError, KeyboardInterrupt):
        print()
        sys.exit(1)

    if not profile:
        profile = "nakom.is-sandbox"

    os.environ['AWS_PROFILE'] = profile
    return profile


def main():
    parser = argparse.ArgumentParser(
        description='Send commands to BootBoots CatCam device via AWS IoT Core',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Commands:
  ping                      Test device connectivity
  get_status                Get device status (WiFi, camera, etc.)
  take_photo                Trigger photo capture with AI inference
  get_settings              Get current device settings
  set_setting NAME VALUE    Set a device setting
  reboot                    Reboot the device

Examples:
  python bbdevice.py ping
  python bbdevice.py take_photo
  python bbdevice.py set_setting training_mode true
  python bbdevice.py set_setting camera_brightness 1
        '''
    )

    parser.add_argument('command', help='Command to send')
    parser.add_argument('args', nargs='*', help='Command arguments')
    parser.add_argument('--region', help='AWS region (default: from profile)')
    parser.add_argument('--timeout', type=float, default=DEFAULT_TIMEOUT,
                        help=f'Response timeout in seconds (default: {DEFAULT_TIMEOUT})')

    args = parser.parse_args()

    # Ensure AWS profile is set
    profile = ensure_aws_profile()
    print(f"Using AWS profile: {profile}")
    print()

    # Create commander
    commander = DeviceCommander(region=args.region)

    # Build command payload
    command = args.command
    params = None

    if command == 'set_setting':
        if len(args.args) < 2:
            print("Error: set_setting requires NAME and VALUE arguments")
            print("Example: python bbdevice.py set_setting training_mode true")
            sys.exit(1)

        setting_name = args.args[0]
        setting_value = args.args[1]

        # Try to parse value as JSON (for booleans and numbers)
        try:
            setting_value = json.loads(setting_value.lower())
        except json.JSONDecodeError:
            pass  # Keep as string

        params = {"setting": setting_name, "value": setting_value}

    # Send command
    result = commander.send_command(command, params, timeout=args.timeout)

    if result:
        print("Result:")
        print(json.dumps(result, indent=2))
    else:
        print("No response received (timeout)")
        sys.exit(1)


if __name__ == "__main__":
    main()
