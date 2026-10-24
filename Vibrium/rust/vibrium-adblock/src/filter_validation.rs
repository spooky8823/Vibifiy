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
    let mut cleaned_lines = Vec::new();
    let mut parsed_rules = 0;
    let mut rejected_rules = 0;

    for raw_line in contents.lines() {
        let line = raw_line.trim();

        // Preserve blank lines, comments, and list headers.
        if line.is_empty() || line.starts_with('!') || line.starts_with('[') {
            cleaned_lines.push(raw_line.to_string());
            continue;
        }

        match parse_filter(line, false, ParseOptions::default()) {
            Ok(ParsedLine::Network(_)) | Ok(ParsedLine::Cosmetic(_)) => {
                parsed_rules += 1;
                cleaned_lines.push(raw_line.to_string());
            }
            Err(_) => {
                rejected_rules += 1;
            }
        }
    }

    if parsed_rules == 0 {
        return Err(VibriumAdblockError::FilterListError(
            "Filter list contains no parseable rules".to_string(),
        ));
    }

    let cleaned_contents = cleaned_lines.join("\n");

    Ok((
        cleaned_contents,
        FilterValidationReport {
            parsed_rules,
            rejected_rules,
        },
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_valid_network_rules() {
        let contents = "||ads.example.com^\n||tracker.example.com^";

        let report = validate_filter_list(contents).unwrap();

        assert_eq!(report.parsed_rules, 2);
        assert_eq!(report.rejected_rules, 0);
    }

    #[test]
    fn accepts_valid_cosmetic_rules() {
        let contents = "example.com##.advertisement";

        let report = validate_filter_list(contents).unwrap();

        assert_eq!(report.parsed_rules, 1);
        assert_eq!(report.rejected_rules, 0);
    }

    #[test]
    fn accepts_rules_that_do_not_match_the_old_heuristic() {
        let contents = [
            ".cfm?ad=",
            ".com/exload.js",
            "&ev=PageView&",
            "&hitType=pageview&",
        ]
        .join("\n");

        let report = validate_filter_list(&contents).unwrap();

        assert_eq!(report.parsed_rules, 4);
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
    fn rejects_an_empty_list() {
        assert!(validate_filter_list("").is_err());
    }

    #[test]
    fn rejects_a_list_without_rules() {
        let contents = "! Just a comment\n[Adblock Plus 2.0]\n";

        assert!(validate_filter_list(contents).is_err());
    }

    #[test]
    fn rejects_content_when_every_non_comment_line_is_rejected() {
        // This input has no non-comment lines, so there are no parseable rules.
        let contents = "! A comment\n[Adblock Plus 2.0]\n\n";

        assert!(validate_and_clean_filter_list(contents).is_err());
    }
}
