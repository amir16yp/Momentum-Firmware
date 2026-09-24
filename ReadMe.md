# Rectum Firmware

## FAQ

### What is Rectum Firmware?

Rectum is a fork of [Momentum Firmware](https://github.com/Next-Flip/Momentum-Firmware) for the Flipper Zero. Credit for the foundation and inherited features belongs to the Momentum team and the upstream developers whose work it builds on.

### Why did you make this fork?

I made this fork because the Momentum team does not allow AI-assisted contributions. I wanted a place where I could work on fixes and accept contributions regardless of the tools used to write them.

I mean no disrespect to the Momentum developers. I appreciate their work, but I disagree with their rigid rules around AI-assisted contributions.

### What does this fork add?

My changes mainly address memory issues: memory leaks, memory fragmentation, and other common C memory errors. The focus is on improving memory management and reliability.

Unfortunately, these AI-assisted changes cannot go upstream under Momentum's contribution rules, so I maintain them here.

### What kinds of contributions are acceptable?

I do not care whether a human, an LLM, or a monkey randomly typing keys wrote a PR. I care whether the result is good.

That still means having standards. A PR should:

- Solve a real problem and clearly explain the change.
- Be readable, maintainable, and consistent with the surrounding code.
- Handle memory ownership, allocation failures, and cleanup correctly where relevant.
- Include appropriate testing or other evidence that it works without introducing regressions.
- Come from someone who understands the change and can respond to review feedback.

Using AI does not disqualify a contribution, and it does not excuse broken code or replace review.

### why did you name it Rectum?

because le funny name
