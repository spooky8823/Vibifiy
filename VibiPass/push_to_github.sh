#!/bin/bash
# Run this script from inside the extracted VibiPass folder
# It will push everything to your GitHub repo automatically

GITHUB_USER="spookyminecraftgamer-dot"
REPO="VibiPass"

echo "Enter your GitHub Personal Access Token:"
read -s TOKEN
echo ""

cd "$(dirname "$0")"

# Configure git
git init
git config user.email "vibipass@local"
git config user.name "$GITHUB_USER"

# Set remote with token
git remote remove origin 2>/dev/null || true
git remote add origin "https://$GITHUB_USER:$TOKEN@github.com/$GITHUB_USER/$REPO.git"

# Pull existing content first
git fetch origin main 2>/dev/null || true
git checkout -b main 2>/dev/null || git checkout main

# Stage everything
git add -A

# Commit
git commit -m "Update: add icons and fix Windows build"

# Push
git push -u origin main --force

echo ""
echo "✅ Done! Check your GitHub Actions tab — build should start automatically!"
