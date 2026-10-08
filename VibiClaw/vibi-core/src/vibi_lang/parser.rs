use super::lexer::Token;
use crate::types::Command;

#[derive(Debug)]
pub struct VibiProgram {
    pub commands: Vec<Command>,
}

#[derive(Debug)]
struct Job {
    tool_name: String,
    params: Vec<(String, String)>,
    items: Vec<String>,
    mappings: Vec<(String, String)>,
    content_blocks: Vec<(String, String)>,
}

pub fn parse(tokens: Vec<Token>) -> Result<VibiProgram, Vec<String>> {
    let mut iter = tokens.iter().peekable();
    let _errors = Vec::<String>::new();
    
    // Skip main vibi.claw header
    skip_header(&mut iter);
    
    // Expect opening block
    expect_token(&mut iter, &Token::LBrace, "Expected '{'")?;
    expect_identifier(&mut iter, "jobs")?;
    
    let jobs = parse_jobs(&mut iter);
    
    expect_identifier(&mut iter, "jobs")?;
    expect_token(&mut iter, &Token::LParen, "Expected '('")?;
    expect_token(&mut iter, &Token::RParen, "Expected ')'")?;
    expect_token(&mut iter, &Token::RBrace, "Expected '}'")?;
    
    let commands = jobs_to_commands(&jobs);
    
    Ok(VibiProgram { commands })
}

fn skip_header(iter: &mut std::iter::Peekable<std::slice::Iter<Token>>) {
    while let Some(tok) = iter.peek() {
        match tok {
            Token::LBrace => break,
            Token::Eof => break,
            _ => { iter.next(); }
        }
    }
}

fn expect_token(iter: &mut std::iter::Peekable<std::slice::Iter<Token>>, expected: &Token, err: &str) -> Result<(), Vec<String>> {
    match iter.next() {
        Some(t) if t == expected => Ok(()),
        _ => Err(vec![err.to_string()]),
    }
}

fn expect_identifier(iter: &mut std::iter::Peekable<std::slice::Iter<Token>>, expected: &str) -> Result<(), Vec<String>> {
    let got = iter.next();

    match got {
        Some(Token::Identifier(s)) if s == expected => Ok(()),
        other => Err(vec![format!("Expected '{}', got: {:?}", expected, other)]),
    }
}

fn parse_string(iter: &mut std::iter::Peekable<std::slice::Iter<Token>>) -> String {
    match iter.next() {
        Some(Token::String(s)) => s.clone(),
        Some(Token::Identifier(s)) => s.clone(),
        _ => String::new(),
    }
}

fn parse_bracket_string(iter: &mut std::iter::Peekable<std::slice::Iter<Token>>) -> String {
    match iter.next() {
        Some(Token::LBracket) => {
            let s = parse_string(iter);
            iter.next(); // skip RBracket
            s
        }
        Some(Token::String(s)) => s.clone(),
        _ => String::new(),
    }
}

fn parse_jobs(iter: &mut std::iter::Peekable<std::slice::Iter<Token>>) -> Vec<Job> {
    let mut jobs = Vec::new();
    
    loop {
        match iter.peek() {
            Some(Token::LBrace) => {
                iter.next();
                if let Some(Token::Identifier(_)) = iter.peek() {
                    // might be a job or might be jobs()
                    let next_is_vibi = matches!(iter.peek(), Some(Token::Identifier(s)) if s == "vibi.tool");
                    if next_is_vibi {
                        if let Some(job) = parse_job(iter) {
                            jobs.push(job);
                        }
                        // Also handle && chained jobs inside same block
                        loop {
                            match iter.peek() {
                                Some(Token::And) => {
                                    iter.next();
                                    if let Some(job) = parse_job(iter) {
                                        jobs.push(job);
                                    }
                                }
                                _ => break,
                            }
                        }
                        expect_token(iter, &Token::RBrace, "Expected '}'").ok();
                    } else {
                        // End of jobs, backtrack
                        break;
                    }
                } else {
                    break;
                }
            }
            _ => break,
        }
    }
    jobs
}

fn parse_job(iter: &mut std::iter::Peekable<std::slice::Iter<Token>>) -> Option<Job> {
    // vibi.tool = [tool_name]
    expect_identifier(iter, "vibi.tool").ok()?;
    expect_token(iter, &Token::LBracket, "").ok();
    
    let tool_name = parse_string(iter);
    
    expect_token(iter, &Token::RBracket, "").ok();
    
    let mut params = Vec::new();
    let mut items = Vec::new();
    let mut mappings = Vec::new();
    let mut content_blocks = Vec::new();
    
    // Parse params after tool: ?= or = 
    loop {
        match iter.peek() {
            Some(Token::Pipe) => {
                iter.next();
                // Next could be param or item
                match iter.peek() {
                    Some(Token::Identifier(key)) => {
                        let key = key.clone();
                        iter.next();
                        match iter.peek() {
                            Some(Token::QuestionEq) | Some(Token::Colon) => {
                                iter.next();
                                let val = parse_bracket_string(iter);
                                params.push((key, val));
                            }
                            _ => {}
                        }
                    }
                    Some(Token::String(_)) => {
                        // item after pipe
                        let item = parse_string(iter);
                        match iter.peek() {
                            Some(Token::Arrow) => {
                                iter.next();
                                let dest = parse_bracket_string(iter);
                                mappings.push((item, dest));
                            }
                            _ => {
                                items.push(item);
                            }
                        }
                    }
                    Some(Token::LBracket) => {
                        let val = parse_bracket_string(iter);
                        params.push((String::new(), val));
                    }
                    _ => break,
                }
            }
            Some(Token::QuestionEq) => {
                iter.next();
                let val = parse_bracket_string(iter);
                params.push((String::new(), val));
            }
            Some(Token::RBrace) => break,
            Some(Token::And) => break,
            Some(Token::Identifier(k)) => {
                let key = k.clone();
                if key == "vibi.tool" { break; }
                iter.next();
                if matches!(iter.peek(), Some(Token::Colon)) {
                    iter.next();
                    let val = parse_string(iter);
                    content_blocks.push((key, val));
                } else {
                    // It's an action like file.save()
                    break;
                }
            }
            Some(Token::String(_)) => {
                let item = parse_string(iter);
                match iter.peek() {
                    Some(Token::Arrow) => {
                        iter.next();
                        let dest = parse_bracket_string(iter);
                        mappings.push((item, dest));
                    }
                    Some(Token::Pipe) => {
                        let _pipe = iter.next();
                        if let Some(Token::Identifier(key)) = iter.peek() {
                            let key = key.clone();
                            iter.next();
                            if matches!(iter.peek(), Some(Token::QuestionEq) | Some(Token::Colon)) {
                                iter.next();
                                let val = parse_bracket_string(iter);
                                params.push((key, val));
                            }
                        }
                    }
                    _ => {
                        items.push(item);
                    }
                }
            }
            _ => break,
        }
    }
    
    // Skip action like file.save()
    loop {
        match iter.peek() {
            Some(Token::Identifier(_)) | Some(Token::Dot) | Some(Token::LParen) => {
                iter.next();
            }
            Some(Token::RParen) => {
                iter.next();
                break;
            }
            _ => break,
        }
    }
    
    Some(Job {
        tool_name,
        params,
        items,
        mappings,
        content_blocks,
    })
}

fn jobs_to_commands(jobs: &[Job]) -> Vec<Command> {
    let mut commands = Vec::new();
    
    for job in jobs {
        let path = job.params.iter()
            .find(|(k, _)| k == "path")
            .map(|(_, v)| v.clone());
        
        let dir = job.params.iter()
            .find(|(k, _)| k == "dir")
            .map(|(_, v)| v.clone());
        
        match job.tool_name.as_str() {
            "create.file" => {
                for item in &job.items {
                    commands.push(Command::new(
                        crate::types::CommandKind::CreateFile,
                        Some(resolve_path(item, &path)),
                        None,
                    ));
                }
            }
            "edit.file" => {
                let file_name = job.items.first().cloned().or_else(|| {
                    job.params.iter()
                        .find(|(k, _)| k.is_empty())
                        .map(|(_, v)| v.clone())
                }).unwrap_or_default();
                let full_path = resolve_path(&file_name, &path);
                let content = job.content_blocks.iter()
                    .find(|(k, _)| k == "full.file.content")
                    .map(|(_, v)| v.clone());
                let search = job.content_blocks.iter()
                    .find(|(k, _)| k == "search.file.content")
                    .map(|(_, v)| v.clone());
                let replace = job.content_blocks.iter()
                    .find(|(k, _)| k == "replace.file.content")
                    .map(|(_, v)| v.clone());
                
                if let Some(ct) = content {
                    commands.push(Command::new(
                        crate::types::CommandKind::EditFile,
                        Some(full_path),
                        Some(ct),
                    ));
                } else if let (Some(s), Some(r)) = (search, replace) {
                    commands.push(Command::new(
                        crate::types::CommandKind::EditFile,
                        Some(full_path),
                        Some(format!("SEARCH:{}|REPLACE:{}", s, r)),
                    ));
                }
            }
            "delete.file" => {
                for item in &job.items {
                    commands.push(Command::new(
                        crate::types::CommandKind::DeleteFile,
                        Some(resolve_path(item, &path)),
                        None,
                    ));
                }
            }
            "run.command" => {
                for item in &job.items {
                    commands.push(Command::new(
                        crate::types::CommandKind::RunShell,
                        dir.clone(),
                        Some(item.clone()),
                    ));
                }
            }
            "rename.file" => {
                for (old, new) in &job.mappings {
                    commands.push(Command::new(
                        crate::types::CommandKind::RenameFile,
                        Some(resolve_path(old, &path)),
                        Some(new.clone()),
                    ));
                }
            }
            "rename.folder" => {
                let old_dir = job.items.first().cloned().unwrap_or_default();
                let new_dir = job.mappings.first().map(|(_, v)| v.clone()).unwrap_or_default();
                commands.push(Command::new(
                    crate::types::CommandKind::RenameFolder,
                    Some(old_dir),
                    Some(new_dir),
                ));
            }
            "create.directory" => {
                let dir_path = path.or_else(|| {
                    job.params.iter()
                        .find(|(k, _)| k.is_empty())
                        .map(|(_, v)| v.clone())
                });
                if let Some(p) = dir_path {
                    commands.push(Command::new(
                        crate::types::CommandKind::CreateFolder,
                        Some(p),
                        None,
                    ));
                }
                for item in &job.items {
                    commands.push(Command::new(
                        crate::types::CommandKind::CreateFolder,
                        Some(item.clone()),
                        None,
                    ));
                }
            }
            "download.repository" => {
                let url = job.items.first().cloned().or_else(|| {
                    job.params.iter()
                        .find(|(k, _)| k.is_empty())
                        .map(|(_, v)| v.clone())
                }).unwrap_or_default();
                commands.push(Command::new(
                    crate::types::CommandKind::DownloadRepo,
                    path.clone(),
                    Some(url),
                ));
            }
            "download.private.repository" => {
                let url = job.items.first().cloned().or_else(|| {
                    job.params.iter()
                        .find(|(k, _)| k.is_empty())
                        .map(|(_, v)| v.clone())
                }).unwrap_or_default();
                let token = job.params.iter()
                    .find(|(k, _)| k == "git token" || k == "git.token")
                    .map(|(_, v)| v.clone());
                commands.push(Command::new(
                    crate::types::CommandKind::DownloadPrivateRepo,
                    path.clone(),
                    Some(format!("{}|TOKEN:{}", url, token.unwrap_or_default())),
                ));
            }
            "open.folder" => {
                for item in &job.items {
                    commands.push(Command::new(
                        crate::types::CommandKind::OpenFolder,
                        Some(item.clone()),
                        None,
                    ));
                }
            }
            "open.app" => {
                for item in &job.items {
                    commands.push(Command::new(
                        crate::types::CommandKind::OpenApp,
                        None,
                        Some(item.clone()),
                    ));
                }
            }
            "move.file" => {
                for (old, new) in &job.mappings {
                    commands.push(Command::new(
                        crate::types::CommandKind::MoveFile,
                        Some(resolve_path(old, &path)),
                        Some(new.clone()),
                    ));
                }
            }
            "copy.file" => {
                for (old, new) in &job.mappings {
                    commands.push(Command::new(
                        crate::types::CommandKind::CopyFile,
                        Some(resolve_path(old, &path)),
                        Some(new.clone()),
                    ));
                }
            }
            "read.file" => {
                for item in &job.items {
                    commands.push(Command::new(
                        crate::types::CommandKind::ReadFile,
                        Some(resolve_path(item, &path)),
                        None,
                    ));
                }
            }
            "path.tree" => {
                let exclude = job.params.iter()
                    .find(|(k, _)| k == "exclude.folders")
                    .map(|(_, v)| v.clone());
                commands.push(Command::new(
                    crate::types::CommandKind::PathTree,
                    path.clone(),
                    exclude,
                ));
            }
            _ => {}
        }
    }
    commands
}

fn resolve_path(name: &str, path: &Option<String>) -> String {
    match path {
        Some(p) if !p.is_empty() => format!("{}/{}", p.trim_end_matches('/'), name),
        _ => name.to_string(),
    }
}