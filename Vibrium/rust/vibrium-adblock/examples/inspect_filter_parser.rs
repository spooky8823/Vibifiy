use adblock::lists::{ParseOptions, ParsedLine, parse_filter};

fn main() {
    let candidates = [
        "||ads.example.com^",
        "example.com##.advertisement",
        "not a filter",
        "also not a filter",
        "this is not a filter rule",
        ".cfm?ad=",
        ".com/exload.js",
        "&ev=PageView&",
        "&hitType=pageview&",
    ];

    for line in candidates {
        let result = parse_filter(line, false, ParseOptions::default());

        match result {
            Ok(ParsedLine::Network(_)) => {
                println!("NETWORK : {line}");
            }
            Ok(ParsedLine::Cosmetic(_)) => {
                println!("COSMETIC: {line}");
            }
            Err(error) => {
                println!("REJECTED: {line} | {error:?}");
            }
        }
    }
}
