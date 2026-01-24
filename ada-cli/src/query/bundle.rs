//! Bundle reader for ADA capture bundles
//!
//! Parses .adabundle directories with manifest.json to route
//! queries to the appropriate data sources (trace, screen, voice).

use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use serde::Deserialize;

/// Bundle manifest structure from manifest.json
#[derive(Debug, Deserialize)]
pub struct BundleManifest {
    /// Manifest version
    pub version: u32,
    /// Relative path to trace session directory
    pub trace_session: String,
    /// Relative path to screen recording (optional)
    #[serde(default)]
    pub screen_path: Option<String>,
    /// Relative path to voice recording (optional)
    #[serde(default)]
    pub voice_path: Option<String>,
    /// Relative path to lossless voice recording (optional)
    #[serde(default)]
    pub voice_lossless_path: Option<String>,
}

/// An opened ADA bundle with validated manifest
#[derive(Debug)]
pub struct Bundle {
    /// Path to the bundle directory
    pub path: PathBuf,
    /// Parsed and validated manifest
    pub manifest: BundleManifest,
}

impl Bundle {
    /// Open a bundle from a path
    ///
    /// Validates that:
    /// 1. The path exists
    /// 2. manifest.json exists in the path
    /// 3. The manifest contains required fields
    pub fn open(path: &Path) -> Result<Self> {
        // 1. Verify path exists
        if !path.exists() {
            bail!("Bundle path does not exist: {:?}", path);
        }

        // 2. Read manifest
        let manifest_path = path.join("manifest.json");
        if !manifest_path.exists() {
            bail!("No manifest.json found in bundle: {:?}", path);
        }

        let content = fs::read_to_string(&manifest_path)
            .with_context(|| format!("Failed to read bundle manifest at {:?}", manifest_path))?;

        let manifest: BundleManifest = serde_json::from_str(&content)
            .with_context(|| "Failed to parse bundle manifest")?;

        // 3. Validate required fields
        if manifest.trace_session.is_empty() {
            bail!("Bundle manifest missing trace_session field");
        }

        Ok(Bundle {
            path: path.to_path_buf(),
            manifest,
        })
    }

    /// Get the trace session path for trace queries
    pub fn trace_path(&self) -> PathBuf {
        self.path.join(&self.manifest.trace_session)
    }

    /// Get screen recording path if available
    #[allow(dead_code)]
    pub fn screen_path(&self) -> Option<PathBuf> {
        self.manifest
            .screen_path
            .as_ref()
            .map(|p| self.path.join(p))
    }

    /// Get voice recording path if available
    #[allow(dead_code)]
    pub fn voice_path(&self) -> Option<PathBuf> {
        self.manifest
            .voice_path
            .as_ref()
            .map(|p| self.path.join(p))
    }

    /// Get lossless voice recording path if available
    #[allow(dead_code)]
    pub fn voice_lossless_path(&self) -> Option<PathBuf> {
        self.manifest
            .voice_lossless_path
            .as_ref()
            .map(|p| self.path.join(p))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::TempDir;

    fn create_valid_bundle() -> TempDir {
        let temp_dir = TempDir::new().unwrap();

        // Create trace directory
        let trace_dir = temp_dir.path().join("trace");
        fs::create_dir_all(&trace_dir).unwrap();

        // Create bundle manifest
        let manifest = r#"{
            "version": 1,
            "trace_session": "trace",
            "screen_path": "screen.mp4",
            "voice_path": "voice.m4a"
        }"#;

        let manifest_path = temp_dir.path().join("manifest.json");
        let mut f = fs::File::create(&manifest_path).unwrap();
        f.write_all(manifest.as_bytes()).unwrap();

        temp_dir
    }

    #[test]
    fn test_bundle__open_valid_bundle__succeeds() {
        let temp_dir = create_valid_bundle();
        let bundle = Bundle::open(temp_dir.path()).unwrap();

        assert_eq!(bundle.manifest.version, 1);
        assert_eq!(bundle.manifest.trace_session, "trace");
        assert_eq!(bundle.manifest.screen_path, Some("screen.mp4".to_string()));
        assert_eq!(bundle.manifest.voice_path, Some("voice.m4a".to_string()));
    }

    #[test]
    fn test_bundle__trace_path__returns_joined_path() {
        let temp_dir = create_valid_bundle();
        let bundle = Bundle::open(temp_dir.path()).unwrap();

        let trace_path = bundle.trace_path();
        assert_eq!(trace_path, temp_dir.path().join("trace"));
    }

    #[test]
    fn test_bundle__screen_path__returns_joined_path() {
        let temp_dir = create_valid_bundle();
        let bundle = Bundle::open(temp_dir.path()).unwrap();

        let screen_path = bundle.screen_path().unwrap();
        assert_eq!(screen_path, temp_dir.path().join("screen.mp4"));
    }

    #[test]
    fn test_bundle__missing_manifest__fails() {
        let temp_dir = TempDir::new().unwrap();
        // Don't create manifest.json

        let result = Bundle::open(temp_dir.path());
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("No manifest.json"));
    }

    #[test]
    fn test_bundle__nonexistent_path__fails() {
        let result = Bundle::open(Path::new("/nonexistent/path/to/bundle"));
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("does not exist"));
    }

    #[test]
    fn test_bundle__missing_trace_session__fails() {
        let temp_dir = TempDir::new().unwrap();

        // Create manifest without trace_session
        let manifest = r#"{
            "version": 1,
            "trace_session": ""
        }"#;

        let manifest_path = temp_dir.path().join("manifest.json");
        let mut f = fs::File::create(&manifest_path).unwrap();
        f.write_all(manifest.as_bytes()).unwrap();

        let result = Bundle::open(temp_dir.path());
        assert!(result.is_err());
        assert!(result
            .unwrap_err()
            .to_string()
            .contains("missing trace_session"));
    }

    #[test]
    fn test_bundle__optional_fields_missing__succeeds() {
        let temp_dir = TempDir::new().unwrap();

        // Create manifest with only required fields
        let manifest = r#"{
            "version": 1,
            "trace_session": "trace"
        }"#;

        let manifest_path = temp_dir.path().join("manifest.json");
        let mut f = fs::File::create(&manifest_path).unwrap();
        f.write_all(manifest.as_bytes()).unwrap();

        let bundle = Bundle::open(temp_dir.path()).unwrap();
        assert!(bundle.screen_path().is_none());
        assert!(bundle.voice_path().is_none());
        assert!(bundle.voice_lossless_path().is_none());
    }
}
