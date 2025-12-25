# CI Certificate Setup Guide

This document explains how to configure Apple Developer certificates for CI/CD pipelines.

## Overview

Since ADA requires code signing for macOS development (including SSH and CI), certificates must be properly configured in CI environments.

## GitHub Actions Setup

### Step 1: Export Your Certificate

On your Mac with the Apple Developer certificate installed:

```bash
# Find your certificate identity
security find-identity -v -p codesigning

# Export to .p12 file (you'll be prompted for passwords)
security export -k ~/Library/Keychains/login.keychain-db \
  -t identities \
  -f pkcs12 \
  -o ~/Desktop/ada_cert.p12

# Convert to base64 for GitHub secrets
base64 -i ~/Desktop/ada_cert.p12 | pbcopy

# Clean up
rm ~/Desktop/ada_cert.p12
```

### Step 2: Add GitHub Secrets

In your repository:
1. Go to Settings → Secrets and variables → Actions
2. Add these secrets:

| Secret Name | Value |
|------------|-------|
| `APPLE_CERT_BASE64` | Paste from clipboard (base64 encoded .p12) |
| `APPLE_CERT_PASSWORD` | Password you used when exporting .p12 |
| `APPLE_DEVELOPER_ID` | Full certificate name (e.g., "Developer ID Application: John Doe (TEAM123)") |

### Step 3: Update GitHub Workflow

Add this to `.github/workflows/coverage.yml`:

```yaml
- name: Install Apple Certificate
  if: runner.os == 'macOS' && env.APPLE_CERT_BASE64
  env:
    APPLE_CERT_BASE64: ${{ secrets.APPLE_CERT_BASE64 }}
    APPLE_CERT_PASSWORD: ${{ secrets.APPLE_CERT_PASSWORD }}
  run: |
    # Create variables
    CERTIFICATE_PATH=$RUNNER_TEMP/build_certificate.p12
    KEYCHAIN_PATH=$RUNNER_TEMP/app-signing.keychain-db
    KEYCHAIN_PASSWORD=$(uuidgen)
    
    # Import certificate from secrets
    echo -n "$APPLE_CERT_BASE64" | base64 --decode -o $CERTIFICATE_PATH
    
    # Create temporary keychain
    security create-keychain -p "$KEYCHAIN_PASSWORD" $KEYCHAIN_PATH
    security set-keychain-settings -lut 21600 $KEYCHAIN_PATH
    security unlock-keychain -p "$KEYCHAIN_PASSWORD" $KEYCHAIN_PATH
    
    # Import certificate to keychain
    security import $CERTIFICATE_PATH \
      -P "$APPLE_CERT_PASSWORD" \
      -A \
      -t cert \
      -f pkcs12 \
      -k $KEYCHAIN_PATH
    
    security list-keychain -d user -s $KEYCHAIN_PATH
    security set-key-partition-list -S apple-tool:,apple: -s \
      -k "$KEYCHAIN_PASSWORD" $KEYCHAIN_PATH
    
    # Clean up
    rm $CERTIFICATE_PATH
    
    # Export for subsequent steps
    echo "APPLE_DEVELOPER_ID=${{ secrets.APPLE_DEVELOPER_ID }}" >> $GITHUB_ENV

- name: Configure code signing for CI
  if: runner.os == 'macOS'
  run: |
    if [[ -z "${APPLE_DEVELOPER_ID:-}" ]]; then
      # Fallback to ad-hoc signing if no certificate
      echo "Warning: No certificate configured, using ad-hoc signing"
      echo "APPLE_DEVELOPER_ID=-" >> $GITHUB_ENV
    fi
```

## GitLab CI Setup

For GitLab, use CI/CD variables:

```yaml
# .gitlab-ci.yml
before_script:
  - |
    if [[ "$CI_RUNNER_OS" == "macOS" ]]; then
      # Certificate is base64 encoded in CI variable
      echo "$APPLE_CERT_BASE64" | base64 --decode > cert.p12
      
      # Import to keychain (similar to GitHub Actions)
      # ... (same keychain commands as above)
      
      export APPLE_DEVELOPER_ID="$APPLE_DEVELOPER_ID"
    fi
```

## Jenkins Setup

For Jenkins, use Credentials plugin:

```groovy
pipeline {
  environment {
    APPLE_CERT = credentials('apple-developer-cert')
    APPLE_DEVELOPER_ID = credentials('apple-developer-id')
  }
  stages {
    stage('Setup Signing') {
      when { expression { env.OS == 'macOS' } }
      steps {
        sh '''
          # Import certificate
          security import $APPLE_CERT -P "$APPLE_CERT_PSW" -A
          export APPLE_DEVELOPER_ID="$APPLE_DEVELOPER_ID"
        '''
      }
    }
  }
}
```

## Self-Hosted Runners

For self-hosted runners, install the certificate once on the machine:

1. Install certificate in System keychain
2. In CI, just unlock keychain:

```yaml
- name: Unlock keychain for signing
  if: runner.os == 'macOS' && runner.labels contains 'self-hosted'
  run: |
    security unlock-keychain -p "${{ secrets.KEYCHAIN_PASSWORD }}" login.keychain
    echo "APPLE_DEVELOPER_ID=${{ secrets.APPLE_DEVELOPER_ID }}" >> $GITHUB_ENV
```

## Team Considerations

### Option 1: Shared CI Certificate
- Create a dedicated "CI Bot" Apple Developer certificate
- Share only with CI system, not developers
- Rotate annually

### Option 2: Fastlane Match
```bash
# Setup (one time)
fastlane match init
fastlane match developer_id

# In CI
fastlane match developer_id --readonly
```

## Security Best Practices

1. **Never commit certificates or passwords**
2. **Use encrypted secrets/variables**
3. **Limit secret access to protected branches**
4. **Rotate certificates before expiration**
5. **Use separate certificates for CI vs development**
6. **Delete temporary keychains after use**

## Troubleshooting

### Certificate Not Found
```bash
# Verify certificate is imported
security find-identity -v -p codesigning

# Check keychain is unlocked
security show-keychain-info
```

### Signing Fails in CI
```bash
# Add verbose output
codesign -vvv --sign "$APPLE_DEVELOPER_ID" binary

# Common fixes:
# - Ensure keychain is in search list
# - Check certificate hasn't expired
# - Verify partition list is set
```

### Ad-hoc Fallback
If certificate setup fails, CI can fall back to ad-hoc signing:
```bash
export APPLE_DEVELOPER_ID="-"  # Ad-hoc signing
```
Note: This has limitations and won't work for all scenarios.

## Cost Consideration

- Apple Developer Program: $99/year
- Covers unlimited certificates
- One membership can support entire team's CI/CD

## References

- [Apple: Notarizing macOS Software](https://developer.apple.com/documentation/security/notarizing_macos_software_before_distribution)
- [GitHub: Encrypted Secrets](https://docs.github.com/en/actions/security-guides/encrypted-secrets)
- [Fastlane Match](https://docs.fastlane.tools/actions/match/)