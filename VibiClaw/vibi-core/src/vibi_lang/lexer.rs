#[derive(Debug, PartialEq, Clone)]
pub enum Token {
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Pipe,
    Arrow,
    QuestionEq,
    Colon,
    And,
    LParen,
    RParen,
    Dot,
    String(String),
    Identifier(String),
    Eof,
}

pub fn tokenize(source: &str) -> Result<Vec<Token>, Vec<String>> {
    let mut tokens = Vec::new();
    let mut chars = source.chars().peekable();
    
    while let Some(&ch) = chars.peek() {
        match ch {
            '{' => { chars.next(); tokens.push(Token::LBrace); }
            '}' => { chars.next(); tokens.push(Token::RBrace); }
            '[' => { chars.next(); tokens.push(Token::LBracket); }
            ']' => { chars.next(); tokens.push(Token::RBracket); }
            '|' => { chars.next(); tokens.push(Token::Pipe); }
            ':' => { chars.next(); tokens.push(Token::Colon); }
            '(' => { chars.next(); tokens.push(Token::LParen); }
            ')' => { chars.next(); tokens.push(Token::RParen); }
            '.' => { chars.next(); tokens.push(Token::Dot); }
            '=' => {
                chars.next();
                if chars.peek() == Some(&'>') {
                    chars.next();
                    tokens.push(Token::Arrow);
                }
            }
            '?' => {
                chars.next();
                let mut clone = chars.clone();
                // Skip whitespace
                while clone.peek() == Some(&' ') || clone.peek() == Some(&'\t') {
                    clone.next();
                }
                if clone.peek() == Some(&'=') {
                    // Consume whitespace and =
                    while chars.peek() == Some(&' ') || chars.peek() == Some(&'\t') {
                        chars.next();
                    }
                    chars.next(); // skip =
                    tokens.push(Token::QuestionEq);
                } else {
                    tokens.push(Token::Identifier("?".to_string()));
                }
            }
            '&' => {
                chars.next();
                if chars.peek() == Some(&'&') {
                    chars.next();
                    tokens.push(Token::And);
                }
            }
            '"' => {
                chars.next();
                let mut s = String::new();
                while let Some(&c) = chars.peek() {
                    chars.next();
                    if c == '\\' && chars.peek() == Some(&'"') {
                        chars.next();
                        s.push('"');
                    } else if c == '"' {
                        break;
                    } else {
                        s.push(c);
                    }
                }
                tokens.push(Token::String(s));
            }
            '/' => {
                chars.next();
                if chars.peek() == Some(&'/') {
                    while let Some(&c) = chars.peek() {
                        chars.next();
                        if c == '\n' { break; }
                    }
                } else {
                    let mut id = String::from("/");
                    while let Some(&c) = chars.peek() {
                        if c.is_alphanumeric() || c == '.' || c == '_' || c == '-' || c == '/' {
                            id.push(c);
                            chars.next();
                        } else { break; }
                    }
                    tokens.push(Token::Identifier(id));
                }
            }
            ' ' | '\n' | '\r' | '\t' | ',' => { chars.next(); }
            c if c.is_alphanumeric() || c == '_' || c == '-' => {
                let mut id = String::new();
                while let Some(&c) = chars.peek() {
                    if c.is_alphanumeric() || c == '.' || c == '_' || c == '-' || c == '/' {
                        id.push(c);
                        chars.next();
                    } else { break; }
                }
                tokens.push(Token::Identifier(id));
            }
            _ => { chars.next(); }
        }
    }
    tokens.push(Token::Eof);
    Ok(tokens)
}