## Attention Thing

`atthing` things shouldn't and doesn't have to be complicated

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
├── Parked.md
└── Projects.md  optional, see below
```

The first time you open it each day, you pick up to 3 notes from `now/` and `try/` by typing their numbers. After that it shows them one at a time.

| key | what it does |
|---|---|
| `d` | done, moves the note to `Done/` |
| `p` | park, moves it to `someday/` and writes it into `Parked.md` with the date (asks first) |
| `s` | next pick |
| `f` | focus: only the note and a timer (`space` pauses). In a small window, only the timer |
| `l` | back to the list, to see it or change what you picked (`Esc` goes back) |
| `j` `k` / arrows | move on the list, scroll a long note |
| `J` `K` / Shift+arrows | move a note up or down on the list, to put them in your own order |
| `t` | show or hide the `try/` notes on the list |
| `r` | look at the folders again, after changing notes somewhere else |
| `h` | what you did, day by day |
| `P` | projects |
| `?` | every key for the screen you're on |
| `q` | quit |

Nothing gets deleted. atthing keeps its own memory in a hidden `.atthing/` folder inside your notes folder: today's picks, the day it first saw each note, the order you put them in, and a log of everything you did. If you use obsidian it doesn't show up there.

### Projects
Optional. Write a `Projects.md` in your notes folder:
```
# Garden
- [[water the plants]]
- build a small greenhouse
```
Each `#` is a project, `- [[note]]` links a note (an Obsidian link), and any other `- ` line is an idea you haven't started. `P` shows each project with the focus time it got this week.

### Build
```
make
make install                  # puts it in ~/.local/bin
ATTHING_DIR=~/notes atthing   # without ATTHING_DIR it looks in ~/Life/forgetme
```
Plain C, no extra libraries. Linux for now.

### Early
This is so new and there is so much waiting to be made yet stay tuned! See [ROADMAP.md](ROADMAP.md).

---
<sub>Honest: AI help is used on this project</sub>
