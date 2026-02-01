#!/bin/bash
# Git Flow Quick Setup Script for B.L.A.S.T. Project
#
# This script helps initialize the repository with GitFlow configuration
# Run: bash .github/gitflow-init.sh

set -e

echo "🚀 B.L.A.S.T. GitFlow Setup"
echo "============================"
echo ""

# Check if git is installed
if ! command -v git &> /dev/null; then
    echo "❌ Git is not installed. Please install git first."
    exit 1
fi

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo "❌ Not in a git repository. Please run this script from the repository root."
    exit 1
fi

echo "✓ Git repository detected"
echo ""

# Configure local git hooks
echo "Setting up local git configuration..."

# Create a pre-commit hook template (optional)
mkdir -p .git/hooks

echo "✓ Git configuration ready"
echo ""

# Show current branches
echo "📊 Current Branches:"
echo "-------------------"
git branch -a
echo ""

# Check if main and develop branches exist
MAIN_EXISTS=$(git branch -a | grep -c "main\|master" || true)
DEVELOP_EXISTS=$(git branch -a | grep -c "develop" || true)

if [ "$DEVELOP_EXISTS" -eq 0 ]; then
    echo "⚠️  'develop' branch not found."
    echo ""
    echo "To initialize GitFlow branches:"
    echo "1. Make sure you're on the main branch: git checkout main"
    echo "2. Create develop branch: git checkout -b develop"
    echo "3. Push it: git push -u origin develop"
    echo ""
    read -p "Would you like to create the 'develop' branch now? (y/n) " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
        if [ "$CURRENT_BRANCH" != "main" ] && [ "$CURRENT_BRANCH" != "master" ]; then
            echo "❌ Please checkout main/master branch first"
            exit 1
        fi
        git checkout -b develop
        git push -u origin develop
        echo "✓ develop branch created and pushed"
    fi
else
    echo "✓ develop branch exists"
fi

echo ""
echo "✨ GitFlow setup complete!"
echo ""
echo "📚 Next Steps:"
echo "1. Read GITFLOW.md for workflow documentation"
echo "2. Set up branch protection rules in GitHub Settings"
echo "3. Start creating features with: git checkout -b feature/my-feature develop"
echo ""
echo "📖 Quick Commands:"
echo "   Feature:  git checkout -b feature/name develop"
echo "   Release:  Use GitHub Actions → GitFlow Release workflow"
echo "   Hotfix:   git checkout -b hotfix/version main"
echo ""
