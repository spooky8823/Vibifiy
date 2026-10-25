use adblock::lists::{ParseOptions, ParsedLine, parse_filter};

use crate::error::VibriumAdblockError;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FilterValidationReport {
    pub parsed_rules: usize,
    pub rejected_rules: usize,
}

pub fn validate_filter_list(contents: &str) -> Result<FilterValidationReport, VibriumAdblockError> {
    let (_, report) = validate_and_clean_filter_list(contents)?;
    Ok(report)
}

pub fn validate_and_clean_filter_list(
    contents: &str,
) -> Result<(String, FilterValidationReport), VibriumAdblockError> {
    reject_non_filter_documents(contents)?;

    let mut cleaned_lines = Vec::new();
    let mut parsed_rules = 0;
    let mut rejected_rules = 0;

    for raw_line in contents.lines() {
        let line = raw_line.trim();

        if line.is_empty() || line.starts_with('!') || line.starts_with('[') {
            cleaned_lines.push(raw_line.to_string());
            continue;
        }

        if looks_like_document_content(line) {
            rejected_rules += 1;
            continue;
        }

        match parse_filter(line, false, ParseOptions::default()) {
            Ok(ParsedLine::Network(_)) | Ok(ParsedLine::Cosmetic(_)) => {
                parsed_rules += 1;
                cleaned_lines.push(raw_line.to_string());
            }
            Err(_) => rejected_rules += 1,
        }
    }

    if parsed_rules == 0 {
        return Err(VibriumAdblockError::FilterListError(
            "Filter list contains no parseable rules".to_string(),
        ));
    }

    // Parser success is not enough: the parser accepts arbitrary text as
    // substring rules. Reject a list if most candidate rules look like prose.
    let candidate_count = parsed_rules + rejected_rules;

    if candidate_count >= 3 && looks_like_prose_heavy_list(contents, candidate_count) {
        return Err(VibriumAdblockError::FilterListError(
            "Filter list contains suspicious prose rather than recognizable filter rules".into(),
        ));
    }

    Ok((
        cleaned_lines.join("\n"),
        FilterValidationReport {
            parsed_rules,
            rejected_rules,
        },
    ))
}

fn reject_non_filter_documents(contents: &str) -> Result<(), VibriumAdblockError> {
    let trimmed = contents.trim_start_matches('\u{feff}').trim_start();
    let lower = trimmed.to_ascii_lowercase();

    let is_html_document = lower.starts_with("<!doctype html")
        || lower.starts_with("<html")
        || lower.starts_with("<head")
        || lower.starts_with("<body")
        || (lower.starts_with("<?xml") && lower.contains("<html"));

    let is_json_object = trimmed.starts_with('{');
    let is_json_array = trimmed.starts_with('[') && looks_like_json_array(trimmed);

    if is_html_document || is_json_object || is_json_array {
        return Err(VibriumAdblockError::FilterListError(
            "Downloaded content appears to be an HTML or JSON document, not a filter list"
                .to_string(),
        ));
    }

    Ok(())
}

fn looks_like_json_array(contents: &str) -> bool {
    let trimmed = contents.trim();

    trimmed.starts_with("[{")
        || trimmed.starts_with("[\"")
        || trimmed.starts_with("[null")
        || trimmed.starts_with("[true")
        || trimmed.starts_with("[false")
        || trimmed.starts_with("[0")
        || trimmed.starts_with("[1")
        || trimmed.starts_with("[-")
}

fn looks_like_document_content(line: &str) -> bool {
    let lower = line.trim_start().to_ascii_lowercase();

    lower.starts_with("<!doctype html")
        || lower.starts_with("<html")
        || lower.starts_with("<head")
        || lower.starts_with("<body")
        || lower.starts_with("<script")
        || lower.starts_with("</html")
        || lower.starts_with("</body")
        || lower.starts_with("<?xml")
        || lower.starts_with("{\"error\"")
        || lower.starts_with("{\"message\"")
        || lower.starts_with("{\"status\"")
        || lower.starts_with("{ \"error\"")
        || lower.starts_with("{ \"message\"")
        || lower.starts_with("{ \"status\"")
}

/// Conservative heuristic for detecting a list made mostly of ordinary prose.
///
/// This intentionally does not classify individual short substring rules as
/// invalid. It is a secondary safeguard, not a replacement for ABP parsing.
fn looks_like_prose_heavy_list(contents: &str, candidate_count: usize) -> bool {
    let mut prose_lines = 0usize;

    for raw_line in contents.lines() {
        let line = raw_line.trim();

        if line.is_empty()
            || line.starts_with('!')
            || line.starts_with('[')
            || looks_like_document_content(line)
        {
            continue;
        }

        // Explicit ABP syntax is strong evidence of a filter rule.
        if line.contains("##")
            || line.contains("#@#")
            || line.contains("#?#")
            || line.contains("#$#")
            || line.starts_with("||")
            || line.starts_with("|")
            || line.starts_with("@@")
            || line.starts_with('/')
            || line.contains('$')
            || line.contains('^')
            || line.contains('*')
            || line.contains('=')
            || line.contains('&')
            || line.contains('.')
            || line.contains('/')
        {
            continue;
        }

        let words = line.split_whitespace().count();
        let alphabetic_words = line
            .split_whitespace()
            .filter(|word| {
                word.chars()
                    .filter(|character| character.is_alphabetic())
                    .count()
                    >= 3
            })
            .count();

        if words >= 4 && alphabetic_words >= 3 {
            prose_lines += 1;
        }
    }

    // Require multiple prose-like lines and a majority of candidates.
    // This avoids rejecting one unusual line inside an otherwise valid list.
    prose_lines >= 2 && prose_lines * 2 > candidate_count
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_valid_network_rules() {
        let report = validate_filter_list("||ads.example.com^\n||tracker.example.com^").unwrap();

        assert_eq!(report.parsed_rules, 2);
        assert_eq!(report.rejected_rules, 0);
    }

    #[test]
    fn accepts_valid_cosmetic_rules() {
        let report = validate_filter_list("example.com##.advertisement").unwrap();

        assert_eq!(report.parsed_rules, 1);
        assert_eq!(report.rejected_rules, 0);
    }

    #[test]
    fn accepts_unusual_but_valid_substring_rules() {
        let contents = [
            ".cfm?ad=",
            ".com/exload.js",
            "&ev=PageView&",
            "&hitType=pageview&",
            "http",
        ]
        .join("\n");

        let report = validate_filter_list(&contents).unwrap();

        assert_eq!(report.parsed_rules, 5);
        assert_eq!(report.rejected_rules, 0);
    }

    #[test]
    fn preserves_comments_headers_and_accepted_rules() {
        let contents = "! Example list\n[Adblock Plus 2.0]\n||ads.example.com^\nthis is not a rule";

        let (cleaned, report) = validate_and_clean_filter_list(contents).unwrap();

        assert_eq!(
            cleaned,
            "! Example list\n[Adblock Plus 2.0]\n||ads.example.com^\nthis is not a rule"
        );
        assert_eq!(report.parsed_rules, 2);
        assert_eq!(report.rejected_rules, 0);
    }

    #[test]
    fn rejects_html_error_documents() {
        assert!(
            validate_filter_list("<!DOCTYPE html>\n<html><body>Access denied</body></html>")
                .is_err()
        );
    }

    #[test]
    fn rejects_html_with_leading_whitespace_and_bom() {
        let contents = "\u{feff}  \n  <!doctype html><html><body>Login required</body></html>";

        assert!(validate_filter_list(contents).is_err());
    }

    #[test]
    fn rejects_json_error_documents() {
        assert!(validate_filter_list(r#"{"error":"unauthorized"}"#).is_err());
        assert!(validate_filter_list(r#"{"message":"rate limited"}"#).is_err());
    }

    #[test]
    fn rejects_json_arrays() {
        assert!(validate_filter_list(r#"[{"id":1},{"id":2}]"#).is_err());
    }

    #[test]
    fn rejects_plain_prose_heavy_responses() {
        let contents = [
            "Access to this resource has been denied",
            "Please contact your network administrator",
            "Your request cannot be completed today",
            "Try signing in to continue using this service",
        ]
        .join("\n");

        assert!(validate_filter_list(&contents).is_err());
    }

    #[test]
    fn rejects_empty_lists_and_comment_only_lists() {
        assert!(validate_filter_list("").is_err());

        assert!(validate_filter_list("! Just a comment\n[Adblock Plus 2.0]\n").is_err());
    }

    #[test]
    fn accepts_a_mixed_list_with_one_unusual_rule() {
        let contents = "||ads.example.com^\nhttp\nexample.com##.advertisement";

        let report = validate_filter_list(contents).unwrap();

        assert_eq!(report.parsed_rules, 3);
        assert_eq!(report.rejected_rules, 0);
    }
}
