pub mod lexer;
pub mod parser;
pub mod compiler;
pub mod runtime;
pub mod cli;

use crate::types::Command;

/// Compile a .v source string into executable Commands
pub fn compile(source: &str) -> Result<Vec<Command>, Vec<String>> {
    let tokens = lexer::tokenize(source)?;
    let ast = parser::parse(tokens)?;
    let commands = compiler::compile(ast)?;
    Ok(commands)
}