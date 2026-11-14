## Attention Thing

`atthing` things shouldn't and doesn't have to be complicated

### What
It's an app that helps you to manage your chores and multiple interests in a clever way

### How
Point it at a folder of text/md notes, split by how big each thing is:

```
your-notes/
├── now/        under an hour
├── next/       after now, less urgent
├── try/        experiments
├── someday/    can wait forever
├── Done/
├── Parked.md
└── Projects.md  optional, see below
```

The first time you open it each day, you pick up to 3 notes (or more, see Setup) from `now/`, `next/` and `try/` by typing their numbers. After that it shows them one at a time.

| key | what it does |
|---|---|
| `d` | done, moves the note to `Done/` |
| `p` | park, moves it to `someday/` and writes it into `Parked.md` with the date (asks first) |
| `s` | next pick |
| `f` | focus: only the note and a timer (`space` pauses). In a small window, only the timer |
| `l` | back to the list, to see it or change what you picked (`Esc` goes back) |
| `j` `k` / arrows | move on the list, scroll a long note |
| `J` `K` / Shift+arrows | move a note up or down on the list, to put them in your own order |
| `a` | add a note: type its title, and it's made in the group the `>` marker is in |
| `e` | write in a note with your own editor (`$EDITOR`, e.g. nvim); atthing comes back when you close it |
| `dd` | delete the note: it goes to `.trash/` in your notes folder (hidden in obsidian), and `u` brings it back |
| `:trash` | the deleted notes: `Enter` brings one back, `:empty` deletes them all for good |
| `:set` | change a number without leaving: `:set picks 5`, `:set goal 4`, `:set stars 25 60 120`. `:set` alone shows them |
| `m` then a letter | move the note to another group's folder: `n` now, `N` next, `t` try. `m` `d` marks it done from the list. The line shows the letters |
| `/` | search the titles like vim: the marker jumps while you type and the other notes go grey, `n` / `N` next and previous match, `Esc` clears |
| `t` | show or hide everything but `now/` on the list |
| `u` | undo the latest done, park, move, pick or delete from today (asks first); `u` again goes further back. On a focus you forgot to stop, it asks how many minutes it really was |
| `Ctrl-R` | redo what `u` undid, like vim |
| `r` | look at the folders again, after changing notes somewhere else. Works on history and projects too, and the list does it by itself each time you go back to it |
| `h` | what you did, day by day, under a star chart of the weeks: a bigger star is more focus time, a bright one is a day you got something done |
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

### Setup
```
make
make install    # puts it in ~/.local/bin
atthing
```
The first time, it asks where your notes are (`~/notes` if you just press Enter) and makes the folders above inside it. Then three short questions with the usual answers filled in: how many notes you pick, how many you want done in a day, and the star sizes. Enter keeps each one. `atthing setup` asks again.

If the shell says `command not found`, `~/.local/bin` isn't in your PATH: `make install` prints the line to add. Or install it for everyone with `sudo make install PREFIX=/usr/local`.

The answer is saved in `~/.config/atthing/config`, a plain text file:
```
dir = ~/notes
groups = now next try
picks = 3
goal = 3
stars = 25 60 120
```
You don't have to open this file: setup and `:set` write it. Only `dir` is needed. `groups` is your own folders for the list, top first. `picks` is how many notes you can pick (up to 9), `goal` is the 3 in `done 2/3`, and `stars` is the minutes of focus where a day on the chart becomes ✧, ✦ and ★. `ATTHING_DIR=~/other atthing` uses another folder just this once.

Plain C, no extra libraries. Works on Linux and macOS.

### Early
This is so new and there is so much waiting to be made yet stay tuned! See [ROADMAP.md](ROADMAP.md).

---
<sub>Honest: AI help is used on this project</sub>
