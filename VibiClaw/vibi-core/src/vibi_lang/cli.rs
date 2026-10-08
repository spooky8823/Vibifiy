use crate::vibi_lang;
use crate::executor::Executor;

pub fn test_compile(source: &str) {
    println!("\n=== VibiClaw Compiler Test ===");
    println!("Source:\n{}\n", source);
    

    match vibi_lang::compile(source) {
        Ok(commands) => {
            println!("✅ Compilation successful! {} commands:", commands.len());
            for (i, cmd) in commands.iter().enumerate() {
                println!("  {}. kind={:?}, path={:?}, content={:?}",
                    i + 1, cmd.kind, cmd.path, cmd.content);
            }
            
            // Execute the commands
            println!("\n🔄 Executing commands...");
            let sandbox_path = dirs::config_dir()
                .unwrap_or_else(|| std::path::PathBuf::from("."))
                .join("vibi-ai")
                .join("sandbox");
            
            match Executor::new(sandbox_path.to_str().unwrap(), true) {
                Ok(executor) => {
                    let results = crate::vibi_lang::runtime::execute(commands, &executor, false);
                    for result in &results {
                        println!("  {}", result);
                    }
                }
                Err(e) => println!("❌ Failed to create sandbox: {}", e),
            }
        }
        Err(errors) => {
            println!("❌ Compilation failed with {} errors:", errors.len());
            for e in &errors {
                println!("  - {}", e);
            }
        }
    }
}