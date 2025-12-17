description: Scaffold a new Epic for the ts-aot project
model: google-gemini-pro-gd
inputVariables:
  - name: title
    description: The title of the Epic
  - name: goal
    description: The high-level goal of this Epic
  - name: parent
    description: The parent Epic file (usually meta_epic.md)
---
# Epic: {{title}}

**Status:** Planned
**Parent:** [Meta Epic](./{{parent}})

## Overview
{{goal}}

## Milestones

### Milestone 1: [Milestone Name]
[Description of the milestone]
- **Goal:** [What does success look like?]

## Action Items

### Task 1.1: [Task Name]
- [ ] [Subtask]
- [ ] [Subtask]
