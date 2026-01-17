#!/usr/bin/env python3
"""
BootBoots Secrets Generator Script
Fetches AWS IoT certificates and WiFi credentials from AWS Systems Manager Parameter Store
and generates the secrets.h file for the CatCam firmware.

Usage:
    export AWS_PROFILE=nakom.is-sandbox
    python3 generate_secrets.py
"""

import os
import sys
from pathlib import Path
import boto3
from botocore.exceptions import ClientError, NoCredentialsError

# Parameter Store paths
PARAM_CERT_PEM = "/BootsBoots/BootBootsThing/certPem"
PARAM_PRIV_KEY = "/BootsBoots/BootBootsThing/privKey"
PARAM_WIFI_SSID = "/Nakomis/Wifi/SSID"
PARAM_WIFI_PASSWORD = "/Nakomis/Wifi/Password"

# Banner ASCII art
BANNER = r'''

o.oOOOo.                                 o                o.oOOOo.                          oO
o     o                                O                  o     o                          OO
O     O                O           O   o                  O     O                O         oO
oOooOO.               oOo         oOo  O                  oOooOO.               oOo        Oo
o     `O .oOo. .oOo.   o           o   OoOo. .oOo.        o     `O .oOo. .oOo.   o   .oOo  oO
O      o O   o O   o   O           O   o   o OooO'        O      o O   o O   o   O   `Ooo.
o     .O o   O o   O   o           o   o   O O            o     .O o   O o   O   o       O Oo
`OooOO'  `OoO' `OoO'   `oO         `oO O   o `OoO'        `OooOO'  `OoO' `OoO'   `oO `OoO' oO

'''


def get_ssm_parameter(ssm_client, param_name: str, with_decryption: bool = True) -> str:
    """Fetch a parameter from AWS Systems Manager Parameter Store."""
    try:
        response = ssm_client.get_parameter(
            Name=param_name,
            WithDecryption=with_decryption
        )
        return response['Parameter']['Value']
    except ClientError as e:
        error_code = e.response['Error']['Code']
        if error_code == 'ParameterNotFound':
            print(f"Parameter not found: {param_name}")
        else:
            print(f"Error fetching {param_name}: {e}")
        raise


def read_root_ca(project_root: Path) -> str:
    """Read the Amazon Root CA certificate file."""
    ca_file = project_root / "AmazonRootCA1.pem"
    if not ca_file.exists():
        raise FileNotFoundError(f"Root CA file not found: {ca_file}")

    with open(ca_file, 'r') as f:
        return f.read().strip()


def get_iot_endpoint(iot_client) -> str:
    """Get the AWS IoT endpoint for the account."""
    try:
        response = iot_client.describe_endpoint(endpointType='iot:Data-ATS')
        return response['endpointAddress']
    except ClientError as e:
        print(f"Error fetching IoT endpoint: {e}")
        raise


def get_iot_credentials_endpoint(iot_client) -> str:
    """Get the AWS IoT Credentials Provider endpoint for the account."""
    try:
        response = iot_client.describe_endpoint(endpointType='iot:CredentialProvider')
        return response['endpointAddress']
    except ClientError as e:
        print(f"Error fetching IoT credentials endpoint: {e}")
        raise


def generate_secrets_h(wifi_ssid: str, wifi_password: str, iot_endpoint: str,
                       iot_credentials_endpoint: str, root_ca: str,
                       cert_pem: str, priv_key: str) -> str:
    """Generate the contents of secrets.h."""
    return f'''#ifndef CATCAM_SECRETS_H
#define CATCAM_SECRETS_H

const char WIFI_SSID[] = "{wifi_ssid}";
const char WIFI_PASSWORD[] = "{wifi_password}";
const char AWS_IOT_ENDPOINT[] = "{iot_endpoint}";
const char AWS_IOT_CREDENTIALS_ENDPOINT[] = "{iot_credentials_endpoint}";

const char BANNER[] = R"WELCOME({BANNER}
)WELCOME";

// Amazon Root CA 1
static const char AWS_CERT_CA[] = R"EOF(
{root_ca}
)EOF";

// Device Certificate
static const char AWS_CERT_CRT[] = R"KEY(
{cert_pem}
)KEY";

// Device Private Key
static const char AWS_CERT_PRIVATE[] = R"KEY(
{priv_key}
)KEY";

#endif
'''


def main():
    print("=" * 60)
    print("  BootBoots Secrets Generator")
    print("=" * 60)
    print()

    # Find project root (directory containing platformio.ini)
    script_dir = Path(__file__).parent
    project_root = script_dir.parent

    if not (project_root / "platformio.ini").exists():
        print("Error: Could not find platformio.ini in project root")
        print(f"Expected at: {project_root}")
        sys.exit(1)

    secrets_file = project_root / "include" / "secrets.h"
    print(f"Project root: {project_root}")
    print(f"Output file:  {secrets_file}")
    print()

    # Initialize AWS clients
    try:
        ssm_client = boto3.client('ssm')
        iot_client = boto3.client('iot')
        # Test credentials by calling STS
        sts = boto3.client('sts')
        identity = sts.get_caller_identity()
        print(f"AWS Account:  {identity['Account']}")
        print()
    except NoCredentialsError:
        print("Error: AWS credentials not found")
        print("Make sure to set AWS_PROFILE or configure AWS credentials")
        print("  export AWS_PROFILE=nakom.is-sandbox")
        sys.exit(1)

    # Fetch parameters from Parameter Store
    print("Fetching parameters from AWS Systems Manager...")
    try:
        print(f"  - {PARAM_WIFI_SSID}")
        wifi_ssid = get_ssm_parameter(ssm_client, PARAM_WIFI_SSID)

        print(f"  - {PARAM_WIFI_PASSWORD}")
        wifi_password = get_ssm_parameter(ssm_client, PARAM_WIFI_PASSWORD)

        print(f"  - {PARAM_CERT_PEM}")
        cert_pem = get_ssm_parameter(ssm_client, PARAM_CERT_PEM)

        print(f"  - {PARAM_PRIV_KEY}")
        priv_key = get_ssm_parameter(ssm_client, PARAM_PRIV_KEY)

        print("  - AWS IoT endpoint")
        iot_endpoint = get_iot_endpoint(iot_client)

        print("  - AWS IoT credentials endpoint")
        iot_credentials_endpoint = get_iot_credentials_endpoint(iot_client)

    except Exception as e:
        print(f"\nFailed to fetch parameters: {e}")
        sys.exit(1)

    # Read Root CA file
    print(f"Reading Root CA from {project_root / 'AmazonRootCA1.pem'}...")
    try:
        root_ca = read_root_ca(project_root)
    except FileNotFoundError as e:
        print(f"\nError: {e}")
        sys.exit(1)

    # Generate secrets.h content
    print("Generating secrets.h...")
    secrets_content = generate_secrets_h(
        wifi_ssid=wifi_ssid,
        wifi_password=wifi_password,
        iot_endpoint=iot_endpoint,
        iot_credentials_endpoint=iot_credentials_endpoint,
        root_ca=root_ca,
        cert_pem=cert_pem.strip(),
        priv_key=priv_key.strip()
    )

    # Write secrets.h
    with open(secrets_file, 'w') as f:
        f.write(secrets_content)

    print()
    print("=" * 60)
    print("  Secrets generated successfully!")
    print("=" * 60)
    print(f"  WiFi SSID:           {wifi_ssid}")
    print(f"  IoT Endpoint:        {iot_endpoint}")
    print(f"  Credentials Endpoint:{iot_credentials_endpoint}")
    print(f"  Certificate:         {len(cert_pem)} bytes")
    print(f"  Private Key:         {len(priv_key)} bytes")
    print(f"  Root CA:             {len(root_ca)} bytes")
    print()
    print(f"Output written to: {secrets_file}")
    print()
    print("Note: secrets.h is gitignored and should not be committed!")


if __name__ == "__main__":
    main()
