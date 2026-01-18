#!/usr/bin/env python3
"""
Test the cat inference API endpoint with an image.

Usage:
    ./test_inference.py                     # Use latest image from photos folder
    ./test_inference.py path/to/image.jpg   # Use specific image

Requires:
    pip install boto3 requests
    AWS_PROFILE=nakom.is-sandbox (or set in environment)
"""

import sys
import os
import glob
import boto3
from botocore.auth import SigV4Auth
from botocore.awsrequest import AWSRequest
import requests

# Configuration
API_HOST = "api.bootboots.sandbox.nakomis.com"
API_PATH = "/infer"
AWS_REGION = "eu-west-2"
AWS_PROFILE = os.environ.get("AWS_PROFILE", "nakom.is-sandbox")

# Photos folder relative to script location
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PHOTOS_DIR = os.path.join(SCRIPT_DIR, "..", "ignored", "catcam-images")


def get_latest_image():
    """Find the latest timestamped JPG in the photos folder."""
    if not os.path.exists(PHOTOS_DIR):
        print(f"Error: Photos directory not found: {PHOTOS_DIR}")
        sys.exit(1)

    # Find all jpg files
    pattern = os.path.join(PHOTOS_DIR, "*.jpg")
    jpg_files = glob.glob(pattern)

    if not jpg_files:
        print(f"Error: No JPG files found in {PHOTOS_DIR}")
        sys.exit(1)

    # Sort by modification time (newest first)
    jpg_files.sort(key=os.path.getmtime, reverse=True)
    return jpg_files[0]


def send_image_to_api(image_path):
    """Send image to the inference API and return the response."""
    print(f"Image: {image_path}")
    print(f"Size: {os.path.getsize(image_path)} bytes")
    print(f"API: https://{API_HOST}{API_PATH}")
    print()

    # Read image data
    with open(image_path, 'rb') as f:
        image_data = f.read()

    # Get AWS credentials
    try:
        session = boto3.Session(profile_name=AWS_PROFILE)
        credentials = session.get_credentials()
        if credentials is None:
            print(f"Error: Could not get AWS credentials for profile '{AWS_PROFILE}'")
            sys.exit(1)
    except Exception as e:
        print(f"Error getting AWS credentials: {e}")
        sys.exit(1)

    # Create signed request
    url = f"https://{API_HOST}{API_PATH}"
    headers = {"Content-Type": "image/jpeg"}

    request = AWSRequest(method="POST", url=url, data=image_data, headers=headers)
    SigV4Auth(credentials, "execute-api", AWS_REGION).add_auth(request)

    # Send request
    print("Sending request...")
    try:
        response = requests.post(
            url,
            data=image_data,
            headers=dict(request.headers),
            timeout=30
        )
    except requests.exceptions.RequestException as e:
        print(f"Error sending request: {e}")
        sys.exit(1)

    print(f"Status: {response.status_code}")
    print()
    print("Response:")
    print(response.text)

    return response


def main():
    # Get image path from argument or find latest
    if len(sys.argv) > 1:
        image_path = sys.argv[1]
        if not os.path.exists(image_path):
            print(f"Error: File not found: {image_path}")
            sys.exit(1)
    else:
        print(f"No image specified, finding latest in {PHOTOS_DIR}")
        image_path = get_latest_image()

    print("=" * 60)
    print("  Cat Inference API Test")
    print("=" * 60)
    print()

    send_image_to_api(image_path)


if __name__ == "__main__":
    main()
