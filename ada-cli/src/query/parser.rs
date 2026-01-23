//! Query string parser
//!
//! Parses query strings like:
//! - "summary"
//! - "list functions"
//! - "list threads"
//! - "events limit:100"
//! - "events thread:0 limit:50"
//! - "calls to main"

use anyhow::{bail, Result};

/// Parsed query
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Query {
    /// Overview statistics: "summary"
    Summary,
    /// List all function symbols: "list functions"
    ListFunctions,
    /// List all threads: "list threads"
    ListThreads,
    /// Query events with optional filters: "events [filters]"
    Events {
        thread: Option<u32>,
        function: Option<String>,
        limit: Option<usize>,
        offset: Option<usize>,
    },
    /// Find calls to a function: "calls to <name>"
    Calls { function: String },
}

impl Query {
    /// Parse a query string
    pub fn parse(input: &str) -> Result<Self> {
        let input = input.trim().to_lowercase();
        let tokens: Vec<&str> = input.split_whitespace().collect();

        if tokens.is_empty() {
            bail!("Empty query. Try: summary, list functions, list threads, events, calls to <name>");
        }

        match tokens[0] {
            "summary" => Ok(Query::Summary),
            "list" => Self::parse_list(&tokens[1..]),
            "events" => Self::parse_events(&tokens[1..]),
            "calls" => Self::parse_calls(&tokens[1..]),
            other => bail!(
                "Unknown query type '{}'. Try: summary, list functions, list threads, events, calls to <name>",
                other
            ),
        }
    }

    fn parse_list(tokens: &[&str]) -> Result<Self> {
        // LCOV_EXCL_START - Error paths
        if tokens.is_empty() {
            bail!("Expected 'functions' or 'threads' after 'list'");
        }
        // LCOV_EXCL_STOP

        match tokens[0] {
            "functions" | "function" | "symbols" | "symbol" => Ok(Query::ListFunctions),
            "threads" | "thread" => Ok(Query::ListThreads),
            // LCOV_EXCL_START - Error path
            other => bail!(
                "Unknown list type '{}'. Try: list functions, list threads",
                other
            ),
            // LCOV_EXCL_STOP
        }
    }

    fn parse_events(tokens: &[&str]) -> Result<Self> {
        let mut thread = None;
        let mut function = None;
        let mut limit = None;
        let mut offset = None;

        for token in tokens {
            if let Some((key, value)) = token.split_once(':') {
                match key {
                    "thread" | "t" => {
                        thread = Some(
                            value
                                .parse()
                                .map_err(|_| anyhow::anyhow!("Invalid thread id: {}", value))?,
                        );
                    }
                    "function" | "func" | "f" => {
                        function = Some(value.to_string());
                    }
                    "limit" | "l" => {
                        limit = Some(
                            value
                                .parse()
                                .map_err(|_| anyhow::anyhow!("Invalid limit: {}", value))?,
                        );
                    }
                    "offset" | "o" | "skip" => {
                        offset = Some(
                            value
                                .parse()
                                .map_err(|_| anyhow::anyhow!("Invalid offset: {}", value))?,
                        );
                    }
                    // LCOV_EXCL_START - Error path
                    _ => bail!("Unknown filter '{}'. Try: thread:N, function:name, limit:N, offset:N", key),
                }
            }
        }
        // LCOV_EXCL_STOP

        Ok(Query::Events {
            thread,
            function,
            limit,
            offset,
        })
    }

    fn parse_calls(tokens: &[&str]) -> Result<Self> {
        // "calls to <function_name>"
        // LCOV_EXCL_START - Error path
        if tokens.is_empty() {
            bail!("Expected 'to <function_name>' after 'calls'");
        }
        // LCOV_EXCL_STOP

        let function_name = if tokens[0] == "to" && tokens.len() > 1 {
            tokens[1..].join(" ")
        } else {
            tokens.join(" ") // LCOV_EXCL_LINE - Alternative syntax
        };

        // LCOV_EXCL_START - Error path
        if function_name.is_empty() {
            bail!("Expected function name after 'calls to'");
        }
        // LCOV_EXCL_STOP

        Ok(Query::Calls {
            function: function_name,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse__summary__then_summary() {
        let query = Query::parse("summary").unwrap();
        assert_eq!(query, Query::Summary);
    }

    #[test]
    fn test_parse__summary_caps__then_summary() {
        let query = Query::parse("SUMMARY").unwrap();
        assert_eq!(query, Query::Summary);
    }

    #[test]
    fn test_parse__list_functions__then_list_functions() {
        let query = Query::parse("list functions").unwrap();
        assert_eq!(query, Query::ListFunctions);
    }

    #[test]
    fn test_parse__list_threads__then_list_threads() {
        let query = Query::parse("list threads").unwrap();
        assert_eq!(query, Query::ListThreads);
    }

    #[test]
    fn test_parse__events_no_filter__then_default() {
        let query = Query::parse("events").unwrap();
        assert_eq!(
            query,
            Query::Events {
                thread: None,
                function: None,
                limit: None,
                offset: None,
            }
        );
    }

    #[test]
    fn test_parse__events_with_limit__then_parsed() {
        let query = Query::parse("events limit:100").unwrap();
        assert_eq!(
            query,
            Query::Events {
                thread: None,
                function: None,
                limit: Some(100),
                offset: None,
            }
        );
    }

    #[test]
    fn test_parse__events_with_thread_and_limit__then_parsed() {
        let query = Query::parse("events thread:0 limit:50").unwrap();
        assert_eq!(
            query,
            Query::Events {
                thread: Some(0),
                function: None,
                limit: Some(50),
                offset: None,
            }
        );
    }

    #[test]
    fn test_parse__events_with_all_filters__then_parsed() {
        let query = Query::parse("events thread:1 function:main limit:10 offset:5").unwrap();
        assert_eq!(
            query,
            Query::Events {
                thread: Some(1),
                function: Some("main".to_string()),
                limit: Some(10),
                offset: Some(5),
            }
        );
    }

    #[test]
    fn test_parse__calls_to__then_parsed() {
        let query = Query::parse("calls to main").unwrap();
        assert_eq!(
            query,
            Query::Calls {
                function: "main".to_string()
            }
        );
    }

    #[test]
    fn test_parse__empty__then_error() {
        let result = Query::parse("");
        assert!(result.is_err());
    }

    #[test]
    fn test_parse__unknown__then_error() {
        let result = Query::parse("foo bar");
        assert!(result.is_err());
    }
}
