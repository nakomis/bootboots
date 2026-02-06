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
        self.s3_bucket = "nakomis-firmware-updates"
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

        print(f"✅ Updated version to {version}")

    def build_firmware(self, environment="esp32s3cam"):
        """Build firmware using PlatformIO"""
        print(f"🔨 Building firmware for {environment}...")

        # Change to project directory
        os.chdir(self.project_root)

        # Clean previous build
        result = subprocess.run(["pio", "run", "-e", environment, "--target", "clean"],
                              capture_output=True, text=True)
        if result.returncode != 0:
            print(f"⚠️  Clean warning: {result.stderr}")

        # Build firmware
        result = subprocess.run(["pio", "run", "-e", environment], capture_output=True, text=True)
        if result.returncode != 0:
            print(f"❌ Build failed: {result.stderr}")
            return False

        print("✅ Build successful!")
        return True

    def find_firmware_file(self, environment="esp32s3cam"):
        """Find the built firmware file for the specified environment"""
        firmware_file = self.project_root / ".pio" / "build" / environment / "firmware.bin"

        if not firmware_file.exists():
            raise FileNotFoundError(f"Could not find firmware.bin for {environment}")

        return firmware_file

    def get_firmware_size(self, firmware_file):
        """Get firmware file size in bytes"""
        return os.path.getsize(firmware_file)

    def upload_to_s3(self, version, environment="esp32s3cam"):
        """Upload firmware to S3 bucket"""
        try:
            firmware_file = self.find_firmware_file(environment)
            firmware_size = self.get_firmware_size(firmware_file)

            # Initialize S3 client
            s3_client = boto3.client('s3')

            # S3 key path: ProjectName/Version/firmware.bin
            s3_key = f"{self.project_name}/{version}/firmware.bin"

            print(f"📤 Uploading {firmware_file} ({firmware_size} bytes)")
            print(f"   → s3://{self.s3_bucket}/{s3_key}")

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
                        'size': str(firmware_size),
                        'build-timestamp': str(subprocess.check_output(['date'], text=True).strip())
                    }
                }
            )

            print(f"✅ Successfully uploaded firmware v{version} to S3")
            return True

        except ClientError as e:
            print(f"❌ S3 upload failed: {e}")
            return False
        except Exception as e:
            print(f"❌ Upload error: {e}")
            return False

def find_git_root(project_root):
    """Find the git repository root from the project directory."""
    result = subprocess.run(
        ["git", "-C", str(project_root), "rev-parse", "--show-toplevel"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        return None
    return Path(result.stdout.strip())


def check_uncommitted_changes(project_root):
    """Check for uncommitted changes in embedded/catcam/ and prompt if found.
    Returns True if the tree was clean, False if there were uncommitted changes."""
    git_root = find_git_root(project_root)
    if not git_root:
        print("⚠️  Not a git repository, skipping uncommitted changes check")
        return True

    # Get the path relative to git root
    try:
        relative_path = project_root.resolve().relative_to(git_root.resolve())
    except ValueError:
        relative_path = Path("embedded/catcam")

    # Check for uncommitted changes (staged + unstaged + untracked)
    result = subprocess.run(
        ["git", "-C", str(git_root), "status", "--porcelain", str(relative_path)],
        capture_output=True, text=True
    )

    changes = result.stdout.strip()
    if not changes:
        return True

    print(f"⚠️  Uncommitted changes in {relative_path}/:")
    for line in changes.splitlines():
        print(f"   {line}")
    print()

    try:
        answer = input("Continue with uncommitted changes? [y/N] ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        print()
        sys.exit(1)

    if answer != 'y':
        print("Aborted.")
        sys.exit(0)

    return False


def commit_version_bump(project_root, version):
    """Commit version.h and push."""
    git_root = find_git_root(project_root)
    if not git_root:
        return False

    try:
        relative_path = project_root.resolve().relative_to(git_root.resolve())
    except ValueError:
        relative_path = Path("embedded/catcam")

    version_file = str(relative_path / "include" / "version.h")

    result = subprocess.run(
        ["git", "-C", str(git_root), "add", version_file],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"❌ Failed to stage {version_file}: {result.stderr}")
        return False

    result = subprocess.run(
        ["git", "-C", str(git_root), "commit", "-m", f"Device version bumped to {version}"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"❌ Failed to commit: {result.stderr}")
        return False

    result = subprocess.run(
        ["git", "-C", str(git_root), "push"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"❌ Failed to push: {result.stderr}")
        return False

    print(f"📝 Committed and pushed version bump to {version}")
    return True


def create_version_tag(project_root, version):
    """Create a git tag for the version and push it."""
    git_root = find_git_root(project_root)
    if not git_root:
        print("⚠️  Not a git repository, skipping tag creation")
        return False

    tag_name = f"device/{version}"

    # Check if tag already exists
    result = subprocess.run(
        ["git", "-C", str(git_root), "tag", "-l", tag_name],
        capture_output=True, text=True
    )
    if result.stdout.strip():
        print(f"⚠️  Tag {tag_name} already exists, skipping tag creation")
        return False

    # Create tag
    result = subprocess.run(
        ["git", "-C", str(git_root), "tag", tag_name],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"❌ Failed to create tag {tag_name}: {result.stderr}")
        return False

    print(f"🏷️  Created tag: {tag_name}")

    # Push tag
    result = subprocess.run(
        ["git", "-C", str(git_root), "push", "origin", tag_name],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"❌ Failed to push tag {tag_name}: {result.stderr}")
        return False

    print(f"🏷️  Pushed tag: {tag_name}")
    return True


def ensure_aws_profile():
    """Check if AWS_PROFILE is set, prompt user if not."""
    profile = os.environ.get('AWS_PROFILE')
    if profile:
        return profile, False

    print("⚠️  AWS_PROFILE is not set.")
    try:
        profile = input("Enter AWS profile name: ").strip()
    except (EOFError, KeyboardInterrupt):
        print()
        sys.exit(1)

    if not profile:
        print("❌ No profile provided. Exiting.")
        sys.exit(1)

    os.environ['AWS_PROFILE'] = profile
    return profile, True


def main():
    parser = argparse.ArgumentParser(
        description='Build and upload BootBoots firmware',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  python build_and_upload.py                    # Build & upload with patch bump
  python build_and_upload.py --version-type minor  # Minor version bump
  python build_and_upload.py --build-only       # Build without uploading
  python build_and_upload.py --no-bump          # Use current version
        '''
    )
    parser.add_argument('--version-type', choices=['major', 'minor', 'patch'],
                       default='patch', help='Type of version bump (default: patch)')
    parser.add_argument('--no-bump', action='store_true',
                       help='Skip version bump, use current version')
    parser.add_argument('--build-only', action='store_true',
                       help='Only build, do not upload to S3')
    parser.add_argument('-e', '--environment', default='esp32s3cam',
                       help='PlatformIO environment to build (default: esp32s3cam)')

    args = parser.parse_args()

    # Ensure AWS_PROFILE is set (needed for S3 upload)
    aws_profile, was_prompted = ensure_aws_profile()

    # Find project root (directory containing platformio.ini)
    current_dir = Path.cwd()
    project_root = current_dir

    while project_root != project_root.parent:
        if (project_root / "platformio.ini").exists():
            break
        project_root = project_root.parent
    else:
        print("❌ Error: Could not find platformio.ini file")
        print("   Run this script from within the project directory")
        sys.exit(1)

    print("=" * 60)
    print("  BootBoots Firmware Build & Upload")
    print("=" * 60)
    print(f"☁️  AWS Profile: {aws_profile}")

    builder = FirmwareBuilder(project_root)

    # Check for uncommitted changes
    tree_was_clean = check_uncommitted_changes(project_root)

    # Show current version
    current_version = builder.get_current_version()
    print(f"📦 Current version: {current_version}")

    # Bump version if requested
    if not args.no_bump:
        version = builder.bump_version(args.version_type)
        print(f"📦 New version: {version}")

        # If tree was clean, commit and push the version bump
        if tree_was_clean:
            commit_version_bump(project_root, version)
    else:
        version = current_version
        print(f"📦 Using current version: {version}")

    print()

    # Build firmware
    if not builder.build_firmware(args.environment):
        print()
        print("❌ Build failed! Fix errors and try again.")
        sys.exit(1)

    print()

    # Upload to S3 if not build-only
    if not args.build_only:
        print("📡 Uploading to S3...")
        if builder.upload_to_s3(version, args.environment):
            print()
            print("=" * 60)
            print(f"✅ Firmware v{version} successfully built and uploaded!")
            print("=" * 60)
            print(f"   S3 Bucket: {builder.s3_bucket}")
            print(f"   Project: {builder.project_name}")
            print(f"   Version: {version}")
            print()
            # Create and push git tag
            create_version_tag(project_root, version)

            print("📝 Note: Manifest will be updated automatically by Lambda")
            print("🎉 Ready for OTA deployment via web interface!")
        else:
            print()
            print("❌ Upload failed! Check AWS credentials and bucket permissions.")
            sys.exit(1)
    else:
        print("=" * 60)
        print(f"✅ Firmware v{version} built successfully")
        print("=" * 60)
        print("   (Upload skipped - build-only mode)")
        firmware_file = builder.find_firmware_file(args.environment)
        print(f"   Firmware: {firmware_file}")

    if was_prompted:
        print()
        print(f"💡 To keep AWS_PROFILE set in your shell, run:")
        print(f"   export AWS_PROFILE={aws_profile}")

if __name__ == "__main__":
    main()
