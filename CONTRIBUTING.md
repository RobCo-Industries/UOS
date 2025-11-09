# Contributing to UOS

Thank you for your interest in contributing to the Unified Operating System project! This document provides guidelines for contributing.

## Code of Conduct

- Be respectful and professional
- Focus on constructive feedback
- Help maintain the project's authenticity to Fallout lore
- Follow coding standards

## How to Contribute

### Reporting Bugs

**Before submitting:**
1. Check if the bug has already been reported
2. Ensure it's actually a bug (not an intentional "feature"!)
3. Test on real hardware if possible

**Bug Report Format:**
```markdown
**Description:**
Brief description of the issue

**Steps to Reproduce:**
1. Step one
2. Step two
3. ...

**Expected Behavior:**
What should happen

**Actual Behavior:**
What actually happens

**Platform:**
- Raspberry Pi model:
- UOS version:
- Build configuration:

**Additional Context:**
Any other relevant information
```

**Note:** Some "bugs" are intentional (like the maintenance mode exploit). Check documentation first!

### Suggesting Features

We welcome feature suggestions that:
- Enhance Fallout terminal authenticity
- Improve usability
- Add lore-accurate functionality
- Optimize performance

**Feature Request Format:**
```markdown
**Feature Description:**
Clear description of the feature

**Use Case:**
Why is this feature needed?

**Lore Accuracy:**
How does this fit with Fallout universe?

**Implementation Ideas:**
Any thoughts on how to implement?
```

### Code Contributions

#### Getting Started

1. Fork the repository
2. Clone your fork:
   ```bash
   git clone https://github.com/your-username/UOS.git
   cd UOS
   ```
3. Create a branch:
   ```bash
   git checkout -b feature/your-feature-name
   ```

#### Coding Standards

**C Code Style:**
```c
// Use clear, descriptive names
void terminal_process_command(const char* command);

// Comment complex logic
// Parse command and extract arguments
char* args = parse_arguments(command);

// Use consistent indentation (4 spaces)
if (condition) {
    do_something();
} else {
    do_something_else();
}

// Keep functions focused and short
// Prefer readability over cleverness
```

**General Guidelines:**
- Follow existing code style
- Add comments for complex logic
- Keep functions under 50 lines when possible
- Use meaningful variable names
- Avoid magic numbers (use constants)
- No trailing whitespace
- End files with newline

**Header Guards:**
```c
#ifndef MODULE_NAME_H
#define MODULE_NAME_H

// ... content ...

#endif /* MODULE_NAME_H */
```

#### Documentation

- Update README.md if adding major features
- Add command documentation to USER_GUIDE.md
- Update ARCHITECTURE.md for system changes
- Include code comments
- Update BUILD.md if changing build process

#### Testing

Before submitting:
1. Build for all platforms:
   ```bash
   make clean && make bcm2835
   make clean && make bcm2836
   make clean && make bcm2837
   ```
2. Test on real hardware if possible
3. Verify documentation is updated
4. Check for compiler warnings

#### Commit Messages

Use clear, descriptive commit messages:

**Good:**
```
Add EDIT command for text file editing

- Implement basic line editor
- Support insert and delete operations
- Add help text for EDIT command
```

**Bad:**
```
fixed stuff
WIP
asdf
```

**Format:**
```
Brief summary (50 chars or less)

More detailed explanation if needed. Wrap at 72 characters.
Explain what and why, not how (code shows how).

- Bullet points for multiple changes
- Reference issues: Fixes #123
```

#### Pull Request Process

1. Update documentation
2. Add tests if applicable
3. Ensure builds succeed
4. Update CHANGELOG (if exists)
5. Submit PR with description:

```markdown
**Description:**
What does this PR do?

**Related Issues:**
Fixes #123, Closes #456

**Changes:**
- Change 1
- Change 2

**Testing:**
How was this tested?

**Screenshots:**
If UI changes, include screenshots
```

6. Respond to review comments
7. Keep PR focused (one feature/fix per PR)

### Areas for Contribution

#### High Priority
- File system implementation (FAT32)
- Additional commands (EDIT, etc.)
- Network stack basics
- CRT visual effects
- Sound effects

#### Medium Priority
- Command history
- Tab completion
- Process management
- More device drivers
- Performance optimization

#### Low Priority
- Easter eggs
- Additional lore documents
- Graphical boot splash
- USB keyboard support

### Development Setup

**Prerequisites:**
```bash
# Install ARM toolchain
sudo apt-get install gcc-arm-none-eabi

# Optional: QEMU for testing
sudo apt-get install qemu-system-arm
```

**Build and Test:**
```bash
# Build for Pi 3
make

# Clean build
make clean

# Build for specific platform
make bcm2835
```

**Development Tips:**
- Use serial console for debugging
- QEMU has limitations (no GPU)
- Real hardware testing is valuable
- Start small, test incrementally

### Lore and Authenticity

When contributing, maintain Fallout authenticity:

**Good:**
- OpenVMS-style commands
- Retro terminal aesthetics
- Pre-war (2077) dated logs
- RobCo branding
- Intentional security flaws

**Bad:**
- Modern UI elements
- Post-2077 references
- Secure authentication
- "Real" security best practices
- Breaking character/lore

### Documentation Contributions

Documentation improvements are highly valued:
- Fix typos and errors
- Improve clarity
- Add examples
- Create tutorials
- Write guides

### Community

- Be patient with reviewers
- Help other contributors
- Share knowledge
- Maintain friendly atmosphere
- Have fun!

## Recognition

Contributors will be:
- Listed in CONTRIBUTORS.md
- Credited in release notes
- Mentioned in documentation
- Appreciated by the community

## Questions?

- Open a GitHub issue
- Check documentation
- Review existing code
- Ask in discussions

## Legal

By contributing, you agree that:
- Your contributions are your own work
- You grant license as per LICENSE file
- You follow project guidelines
- Content is appropriate and legal

---

**RobCo Industries™** - *"The Future of Computing, Yesterday!"*

Thank you for contributing to UOS!
