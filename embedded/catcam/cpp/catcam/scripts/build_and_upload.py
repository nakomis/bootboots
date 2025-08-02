#!/usr/bin/env python3
"""
BootBoots Firmware Build and Upload Script
Handles version bumping, building firmware, and uploading to S3
"""

import os
import sys
import json
import subprocess
import argparse
import re
from pathlib import Path
import boto3
from botocore.exceptions import ClientError

class FirmwareBuilder:
    def __init__(self, project_root):
        self.project_root = Path(project_root)
        self.version_file = self.project_root / "include" / "version.h"
        self.platformio_ini = self.project_root / "platformio.ini"
        self.s3_bucket = "bootboots-firmware-updates"
        self.project_name = "BootBoots"
        
    def get_current_version(self):
        """Extract current version from version.h"""
        if not self.version_file.exists():
            return "1.0.0"
            
        with open(self.version_file, 'r') as f:
            content = f.read()
            
        match = re.search(r'#define FIRMWARE_VERSION "(\d+\.\d+\.\d+)"', content)
        if match:
            return match.group(1)
        return "1.0.0"
    
    def bump_version(self, version_type="patch"):
        """Bump version number (major, minor, or patch)"""
        current = self.get_current_version()
        major, minor, patch = map(int, current.split('.'))
        
        if version_type == "major":
            major += 1
            minor = 0
            patch = 0
        elif version_type == "minor":
            minor += 1
            patch = 0
        elif version_type == "patch":
            patch += 1
        else:
            raise ValueError("version_type must be 'major', 'minor', or 'patch'")
            
        new_version = f"{major}.{minor}.{patch}"
        self.update_version_file(new_version, major, minor, patch)
        return new_version
    
    def update_version_file(self, version, major, minor, patch):
        """Update version.h with new version information"""
        content = f'''#ifndef VERSION_H
#define VERSION_H

// Auto-generated version information
#define FIRMWARE_VERSION "{version}"
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#define PROJECT_NAME "{self.project_name}"

// Version components for programmatic access
#define VERSION_MAJOR {major}
#define VERSION_MINOR {minor}
#define VERSION_PATCH {patch}

// Build a version string
#define VERSION_STRING PROJECT_NAME " v" FIRMWARE_VERSION " (" BUILD_TIMESTAMP ")"

#endif // VERSION_H'''
        
        with open(self.version_file, 'w') as f:
            f.write(content)
        
        print(f"Updated version to {version}")
    
    def build_firmware(self):
        """Build firmware using PlatformIO"""
        print("Building firmware...")
        
        # Change to project directory
        os.chdir(self.project_root)
        
        # Clean previous build
        result = subprocess.run(["pio", "run", "--target", "clean"], 
                              capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Clean failed: {result.stderr}")
            return False
        
        # Build firmware
        result = subprocess.run(["pio", "run"], capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Build failed: {result.stderr}")
            return False
            
        print("Build successful!")
        return True
    
    def find_firmware_file(self):
        """Find the built firmware file"""
        # Look for .pio/build/*/firmware.bin
        build_dir = self.project_root / ".pio" / "build"
        
        for env_dir in build_dir.iterdir():
            if env_dir.is_dir():
                firmware_file = env_dir / "firmware.bin"
                if firmware_file.exists():
                    return firmware_file
                    
        raise FileNotFoundError("Could not find firmware.bin file")
    
    def upload_to_s3(self, version):
        """Upload firmware to S3 bucket"""
        try:
            firmware_file = self.find_firmware_file()
            
            # Initialize S3 client
            s3_client = boto3.client('s3')
            
            # S3 key path: ProjectName/Version/firmware.bin
            s3_key = f"{self.project_name}/{version}/firmware.bin"
            
            print(f"Uploading {firmware_file} to s3://{self.s3_bucket}/{s3_key}")
            
            # Upload file
            s3_client.upload_file(
                str(firmware_file),
                self.s3_bucket,
                s3_key,
                ExtraArgs={
                    'ContentType': 'application/octet-stream',
                    'Metadata': {
                        'project': self.project_name,
                        'version': version,
                        'build-timestamp': str(subprocess.check_output(['date'], text=True).strip())
                    }
                }
            )
            
            print(f"Successfully uploaded firmware v{version} to S3")
            return True
            
        except ClientError as e:
            print(f"S3 upload failed: {e}")
            return False
        except Exception as e:
            print(f"Upload error: {e}")
            return False
    
    def create_version_manifest(self, version):
        """Create a version manifest file for the web UI"""
        try:
            s3_client = boto3.client('s3')
            
            # Get existing manifest or create new one
            manifest_key = f"{self.project_name}/manifest.json"
            
            try:
                response = s3_client.get_object(Bucket=self.s3_bucket, Key=manifest_key)
                manifest = json.loads(response['Body'].read().decode('utf-8'))
            except ClientError:
                manifest = {
                    "project": self.project_name,
                    "versions": []
                }
            
            # Add new version if not already present
            version_info = {
                "version": version,
                "timestamp": subprocess.check_output(['date', '-Iseconds'], text=True).strip(),
                "firmware_path": f"{self.project_name}/{version}/firmware.bin"
            }
            
            # Remove existing version if present, then add new one
            manifest["versions"] = [v for v in manifest["versions"] if v["version"] != version]
            manifest["versions"].append(version_info)
            
            # Sort versions (newest first)
            manifest["versions"].sort(key=lambda x: x["version"], reverse=True)
            
            # Upload updated manifest
            s3_client.put_object(
                Bucket=self.s3_bucket,
                Key=manifest_key,
                Body=json.dumps(manifest, indent=2),
                ContentType='application/json'
            )
            
            print(f"Updated version manifest with v{version}")
            return True
            
        except Exception as e:
            print(f"Failed to update manifest: {e}")
            return False

def main():
    parser = argparse.ArgumentParser(description='Build and upload BootBoots firmware')
    parser.add_argument('--version-type', choices=['major', 'minor', 'patch'], 
                       default='patch', help='Type of version bump')
    parser.add_argument('--no-bump', action='store_true', 
                       help='Skip version bump, use current version')
    parser.add_argument('--build-only', action='store_true',
                       help='Only build, do not upload')
    
    args = parser.parse_args()
    
    # Find project root (directory containing platformio.ini)
    current_dir = Path.cwd()
    project_root = current_dir
    
    while project_root != project_root.parent:
        if (project_root / "platformio.ini").exists():
            break
        project_root = project_root.parent
    else:
        print("Error: Could not find platformio.ini file")
        sys.exit(1)
    
    builder = FirmwareBuilder(project_root)
    
    # Bump version if requested
    if not args.no_bump:
        version = builder.bump_version(args.version_type)
    else:
        version = builder.get_current_version()
    
    print(f"Building firmware version {version}")
    
    # Build firmware
    if not builder.build_firmware():
        print("Build failed!")
        sys.exit(1)
    
    # Upload to S3 if not build-only
    if not args.build_only:
        if builder.upload_to_s3(version):
            builder.create_version_manifest(version)
            print(f"Firmware v{version} successfully built and uploaded!")
        else:
            print("Upload failed!")
            sys.exit(1)
    else:
        print(f"Firmware v{version} built successfully (not uploaded)")

if __name__ == "__main__":
    main()
