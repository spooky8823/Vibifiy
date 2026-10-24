use std::error::Error;
use std::fmt;

#[derive(Debug)]
pub enum VibriumAdblockError {
    InvalidRequest(String),
    InvalidConfiguration(String),
    FilterListError(String),
}

impl fmt::Display for VibriumAdblockError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidRequest(message) => {
                write!(f, "Invalid request: {message}")
            }
            Self::InvalidConfiguration(message) => {
                write!(f, "Invalid configuration: {message}")
            }
            Self::FilterListError(message) => {
                write!(f, "Filter list error: {message}")
            }
        }
    }
}

impl Error for VibriumAdblockError {}