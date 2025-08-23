/// Simple ping function for testing
pub fn ping() -> &'static str {
    "pong"
}

#[cfg(feature = "python")]
use pyo3::prelude::*;

#[cfg(feature = "python")]
#[pyfunction]
fn ping_py() -> &'static str {
    ping()
}

/// Python module definition
#[cfg(feature = "python")]
#[pymodule]
fn query_engine(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(ping_py, m)?)?;
    m.add("__version__", "0.1.0")?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ping() {
        assert_eq!(ping(), "pong");
    }
}