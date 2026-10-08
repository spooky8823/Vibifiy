 # Vibi Claw

**Vibi Claw** is a next-generation AI-powered project management and development environment designed for building complex software projects with intelligent automation. Built using **Rust**, **JavaScript**, **CSS**, and several **custom-designed domain-specific languages (DSLs)**, Vibi Claw combines modern desktop application performance with advanced AI agent workflows.

Unlike traditional AI coding assistants, Vibi Claw introduces a purpose-built **AI Agent DSL**, a language designed specifically for large language models rather than human programmers. The DSL uses clear, intuitive commands such as `create.file`, `delete.file`, and over a dozen additional operations that allow AI models to express development tasks in a structured, deterministic format. Instead of generating shell scripts or general-purpose programming code, AI models emit this DSL, enabling safe, predictable, and extensible automation.

## WebView-Based AI Integration

Vibi Claw embeds **WebKit 4.0**, allowing users to access web-based AI platforms such as ChatGPT and Gemini directly inside the application.

A lightweight JavaScript layer is injected into supported AI interfaces. Rather than modifying the AI itself, this script acts as an observer that continuously monitors AI responses for emitted DSL commands.

When DSL instructions are detected:

* The JavaScript observer extracts the DSL output.
* The commands are securely transferred to the Rust backend using a chunked hash URL communication mechanism.
* The Rust backend forwards the data to Vibi Claw's execution pipeline.
* The custom compiler, lexer, parser, runtime, and execution engine validate and execute the requested operations.
* Every action passes through a fully functional approval and denial queue, allowing users to review, approve, or reject AI-generated operations before they affect the project.

This architecture keeps AI interaction transparent, controllable, and secure while enabling sophisticated autonomous workflows.

## Native AI Provider Support

In addition to browser-based AI integration, Vibi Claw supports direct communication with AI services through official APIs, unofficial providers, and cloud-hosted compute endpoints.

Its built-in chat interface delivers the same agentic workflow capabilities without relying on a browser, allowing developers to interact with AI models using a dedicated native experience.

## Integrated Development Workspace

Vibi Claw includes a fully integrated code editor and project management system designed for large-scale software development.

Key capabilities include:

* Multi-project workspace management
* Nested projects within other projects
* Built-in source code editing
* AI-assisted project generation and maintenance
* Structured AI task execution using the custom Agent DSL
* Secure execution pipeline with user approval workflows
* High-performance Rust backend for deterministic execution

## Technology Stack

* **Rust** for the core application, execution engine, compiler, parser, runtime, and backend systems
* **JavaScript** for WebKit integration and AI response observation
* **CSS** for the desktop user interface
* **WebKit 4.0** for embedded browser functionality
* **Custom AI Agent DSL** designed specifically for structured AI-driven automation
 <p align="center">
  <a href="https://skillicons.dev">
    <img src="https://skillicons.dev/icons?i=rust,gtk,js,css,github" />
  </a>
</p>

## Vision

Vibi Claw is more than a code editor or AI assistant. It is an intelligent development platform that bridges modern language models with deterministic software execution.

By introducing a language built specifically for AI agents, Vibi Claw transforms natural language interactions into structured, reviewable, and executable workflows. This allows developers to build, manage, and automate ambitious software projects while maintaining complete control over every action performed by the AI.



