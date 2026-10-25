use std::collections::HashSet;

/// Cosmetic filtering resources returned by the filtering layer.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CosmeticFilterResources {
    pub hide_selectors: HashSet<String>,
    pub procedural_actions: HashSet<String>,
    pub exceptions: HashSet<String>,
}

/// Performs conservative structural validation of a CSS selector.
///
/// This is not a complete CSS parser. Chromium must still validate
/// selectors before applying them to a document.
pub fn is_valid_css_selector(selector: &str) -> bool {
    let selector = selector.trim();

    if selector.is_empty() {
        return false;
    }

    let mut parentheses = 0usize;
    let mut brackets = 0usize;
    let mut quote: Option<char> = None;
    let mut escaped = false;

    for ch in selector.chars() {
        if escaped {
            escaped = false;
            continue;
        }

        if ch == '\\' {
            escaped = true;
            continue;
        }

        if let Some(current_quote) = quote {
            if ch == current_quote {
                quote = None;
            }
            continue;
        }

        match ch {
            '"' | '\'' => quote = Some(ch),

            // Braces and semicolons are rejected outside quoted strings.
            '{' | '}' | ';' => return false,

            '(' => parentheses += 1,
            ')' => {
                if parentheses == 0 {
                    return false;
                }
                parentheses -= 1;
            }

            '[' => brackets += 1,
            ']' => {
                if brackets == 0 {
                    return false;
                }
                brackets -= 1;
            }

            _ => {}
        }
    }

    if escaped || quote.is_some() || parentheses != 0 || brackets != 0 {
        return false;
    }

    !contains_procedural_operator(selector)
}

/// Identifies common adblock procedural operators that aren't ordinary CSS.
///
/// Standard CSS :has() is intentionally not included here.
fn contains_procedural_operator(selector: &str) -> bool {
    const OPERATORS: &[&str] = &[
        ":has-text(",
        ":matches-css(",
        ":matches-css-before(",
        ":matches-css-after(",
        ":xpath(",
        ":remove(",
        ":remove-attr(",
        ":remove-class(",
        ":upward(",
        ":watch-attr(",
        ":style(",
    ];

    let lower = selector.to_ascii_lowercase();

    OPERATORS.iter().any(|operator| lower.contains(operator))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_common_css_selectors() {
        for selector in [
            ".ad",
            "#sponsor",
            "div.ad",
            "div > .banner",
            "[class*=\"advert\"]",
            "section:not(.content)",
            "div:has(> img)",
        ] {
            assert!(
                is_valid_css_selector(selector),
                "Expected valid CSS selector: {selector}"
            );
        }
    }

    #[test]
    fn rejects_empty_selectors() {
        assert!(!is_valid_css_selector(""));
        assert!(!is_valid_css_selector("   "));
    }

    #[test]
    fn rejects_unbalanced_syntax() {
        for selector in [
            "div[",
            "div]",
            "div(",
            "div)",
            "div:not(.ad",
            "div:not(.ad))",
            "[class=\"ad]",
            "div\\",
        ] {
            assert!(
                !is_valid_css_selector(selector),
                "Expected invalid CSS selector: {selector}"
            );
        }
    }

    #[test]
    fn rejects_css_rule_injection_characters() {
        for selector in [
            ".ad { display: none }",
            ".ad; body",
            ".ad}",
            "{ display:none }",
        ] {
            assert!(
                !is_valid_css_selector(selector),
                "Expected unsafe selector to be rejected: {selector}"
            );
        }
    }

    #[test]
    fn rejects_procedural_operators_from_css_path() {
        for selector in [
            "div:has-text(Sponsored)",
            "div:matches-css(color: red)",
            "div:xpath(../div)",
            "div:remove()",
        ] {
            assert!(
                !is_valid_css_selector(selector),
                "Expected procedural selector to be rejected: {selector}"
            );
        }
    }

    #[test]
    fn accepts_valid_selector_with_quoted_punctuation() {
        assert!(is_valid_css_selector("[data-label=\"a;b\"]"));
        assert!(is_valid_css_selector("[data-label=\"a{b}\"]"));
    }

    #[test]
    fn rejects_unquoted_rule_injection() {
        assert!(!is_valid_css_selector(".ad; body"));
        assert!(!is_valid_css_selector(".ad { display:none }"));
    }
}
