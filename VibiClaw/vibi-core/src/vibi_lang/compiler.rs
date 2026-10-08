use super::parser::VibiProgram;
use crate::types::Command;

pub fn compile(program: VibiProgram) -> Result<Vec<Command>, Vec<String>> {
    Ok(program.commands)
}