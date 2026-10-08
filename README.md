<p align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&color=6C63FF&height=180&section=header&text=Vibifiy&fontSize=50&fontColor=ffffff&animation=fadeIn&fontAlignY=35&desc=Building%20Software%20That%20Belongs%20to%20Everyone&descAlignY=55"/>
</p>

<p align="center">
  <img src="https://readme-typing-svg.herokuapp.com?font=JetBrains+Mono&weight=600&size=24&pause=1000&color=8B5CF6&center=true&vCenter=true&width=700&lines=Open+Source.;AI+Developer+Tools.;Privacy+First.;Built+in+Public.;Copyleft+Forever"/>
</p>

<p align="center">
  Building modern Open Source software, AI tools, and developer experiences for everyone.
</p>

<p align="center">
  <a href="https://github.com/Vibifiy-c">
    <img src="https://img.shields.io/badge/GitHub-Vibifiy--c-181717?style=for-the-badge&logo=github">
  </a>
  <a href="https://spooky8823.github.io/vibifiy-website/">
    <img src="https://img.shields.io/badge/Website-Vibifiy-6C63FF?style=for-the-badge">
  </a>
  <img src="https://img.shields.io/badge/Open%20Source-100%25-success?style=for-the-badge">
  <img src="https://img.shields.io/badge/Copyleft-Licensed-3A86FF?style=for-the-badge">
</p>

---

At **Vibifiy**, we believe software should belong to everyone.

We build modern developer tools, AI-powered applications, desktop software, libraries, and utilities that are open source and developed in public.

Our projects are designed to be inspected, understood, modified, improved, and shared.

Our goal is simple:

> **Build software that empowers developers through openness, transparency, and freedom.**

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> What We Build

- AI-powered developer tools
- Desktop applications
- Open source libraries
- Command-line utilities
- Automation tools
- Developer SDKs
- Privacy-focused software
- Experimental projects
- Educational resources

Vibifiy is intentionally broad. We build software that solves problems, explores ideas, and gives developers tools they can actually understand and control.

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Technologies We Love Building With

<p align="center">
  <a href="https://skillicons.dev">
    <img src="https://skillicons.dev/icons?i=rust,cpp,python,bash,nodejs,electron,js,html,css,supabase,gtk,github" />
  </a>
</p>

We choose technologies based on the needs of each project.

From systems programming with Rust and C++ to web technologies, desktop frameworks, scripting, and AI tooling, the goal is not to force every project into the same stack.

The right tool should serve the software, not the other way around.

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Our Philosophy

Technology should be open, understandable, and accessible.

Developers should be able to inspect the software they use, understand how it works, modify it when necessary, and build upon it without being trapped inside a closed ecosystem.

We believe great software grows through communities.

Every Vibifiy project is built with these ideas at its core.

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Core Principles

- Open source by default
- Built in public
- Community-driven development
- Privacy and transparency first
- Clean and maintainable code
- No unnecessary vendor lock-in
- Open standards whenever possible
- Learning through open code
- Software freedom over closed ecosystems

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Featured Projects

### VibiClaw

An open-source AI coding assistant focused on developer productivity and transparency.

VibiClaw is being developed as a desktop application with a focus on permission-aware AI workflows, giving developers useful AI assistance while keeping them in control of what the software can access and do.

**Status:** Active development

**Source:** [`VibiClaw/`](./VibiClaw/)

---

### VibiPass

A privacy-focused password and secure information manager designed around local encryption.

VibiPass is built around the idea that sensitive information should remain under the user's control rather than being unnecessarily dependent on external services.

**Status:** Active development

**Source:** [`VibiPass/`](./VibiPass/)

VibiPass currently has automated builds for:

- Linux
- Windows
- macOS

Releases are generated through GitHub Actions.

---

### Vibrium

A desktop web browser built as part of the Vibifiy ecosystem.

Vibrium explores what a modern, open desktop browser experience can look like while remaining part of a broader open-source software ecosystem.

**Status:** Active development

**Source:** [`Vibrium/`](./Vibrium/)

---

### Vibifiy Website

The public website for the Vibifiy ecosystem.

The website provides information about Vibifiy and its projects and is automatically deployed through GitHub Actions whenever changes are made to the website source.

**Source:** [`Vibifiy Website/`](./Vibifiy%20Website/)

**Website:** https://spooky8823.github.io/vibifiy-website/

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Open Source First

Vibifiy projects are developed in public because open development makes software better.

Whether you're fixing a typo, reporting a bug, implementing a feature, improving documentation, or simply exploring the source code, contributions are valuable.

We welcome:

- Pull requests
- Feature suggestions
- Bug reports
- Security reports
- Documentation improvements
- Code reviews
- Community discussions

You do not need to be an expert to contribute.

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Copyleft

Software should stay free.

Where Vibifiy projects use copyleft licenses, those licenses help ensure that the software and its improvements remain available to future developers.

Software freedom means people should be able to:

- Use the software
- Study the source code
- Modify it
- Redistribute it
- Share improvements
- Build new software from it

We believe software becomes stronger when improvements remain part of the commons.

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Automated Development

Vibifiy uses GitHub Actions to automate building, testing, releasing, and deploying projects.

The repository uses a central workflow to determine which projects have changed and run only the workflows that are relevant to those changes.

<p align="center">
  <img src="https://github.com/spooky8823/Vibifiy/blob/main/readmeimage.png?raw=true" alt="Vibifiy GitHub Actions workflow"/>
</p>

> **How the dispatcher works**
>
> When a push or pull request targets `main`, `trigger.yml` checks which project directories were changed.
>
> Changes to `VibiPass` trigger its build and release workflow, changes to `Vibrium` trigger its build workflow, and changes to `Vibifiy Website` trigger the GitHub Pages deployment.
>
> This keeps the monorepo efficient by running only the workflows required for the projects that actually changed.

This keeps the monorepo efficient while allowing each project to maintain its own build process.

The current automated projects are:

- VibiPass
- Vibrium
- Vibifiy Website

VibiClaw is intentionally not part of the production CI/CD pipeline yet while development continues.

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Repository Structure

```text
Vibifiy/
│
├── VibiPass/
│   └── Password manager
│
├── Vibrium/
│   └── Desktop web browser
│
├── VibiClaw/
│   └── AI coding assistant
│
├── Vibifiy Website/
│   └── Vibifiy website
│
└── .github/
    └── workflows/
        ├── trigger.yml
        ├── vibipass.yml
        ├── vibrium.yml
        └── website.yml
```

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Why Vibifiy?

We are not building software simply because there is another market to enter.

We build software because knowledge should be shared, tools should be accessible, and developers should be able to trust and understand the software they use.

Open source is not simply how we publish code.

It is how we think about software.

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Join the Community

Whether you're an experienced contributor or writing your first pull request, there is a place for you at Vibifiy.

You can help by:

- Improving documentation
- Fixing bugs
- Suggesting features
- Reviewing code
- Testing projects
- Improving accessibility
- Exploring the codebase
- Sharing feedback

Every contribution helps make the ecosystem better.

---

## <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="22" align="center"> Connect

- **Website:** https://spooky8823.github.io/vibifiy-website/
- **GitHub:** https://github.com/Vibifiy-c
- **Email:** vibifiyy@gmail.com

---

<p align="center">
  <img src="https://github.com/Vibifiy-c/vibifiy-website/blob/main/logo.jpg?raw=true" width="70">
</p>

<h3 align="center">
  Building software that belongs to everyone.
</h3>

<p align="center">
  Open Source • AI • Developer Tools
</p>
