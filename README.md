## Attention Thing

`atthing` shows you one thing to do next. Not a list, one thing.

### What
It's an app that helps you to manage your chores and multiple interests in a clever way

### How
Point it at a folder of text/md notes, split by how big each thing is:

```
your-notes/
├── now/        under an hour
├── try/        experiments
├── someday/    can wait forever
├── Done/
└── Parked.md
```

The first time you open it each day, you pick up to 3 notes from `now/` and `try/` by typing their numbers. After that it shows them one at a time.

| key | what it does |
|---|---|
| `d` | done, moves the note to `Done/` |
| `p` | park, moves it to `someday/` and writes it into `Parked.md` with the date |
| `s` | next pick |
| `f` | focus: only the note and a timer (`space` pauses) |
| `q` | quit |

Nothing gets deleted. atthing keeps its own memory in a hidden `.atthing/` folder inside your notes folder: today's picks, the day it first saw each note, and a log of everything you did. If you use obsidian it doesn't show up there. `atthing pick` lets you pick again.

### Build
```
make
make install                  # puts it in ~/.local/bin
ATTHING_DIR=~/notes atthing   # without ATTHING_DIR it looks in ~/Life/forgetme
```
Plain C, no extra libraries. Linux for now.

### Early
This is v0.1 and there is so much waiting to be made yet stay tuned! See [ROADMAP.md](ROADMAP.md).

---
<sub>Honest: most of the code is written with AI (Claude). The idea, the design and the decisions are mine.<sub>
