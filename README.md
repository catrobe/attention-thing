## Attention Thing

`atthing` one thing at a time. Things don't have to be complicated

<img src="assets/demo.gif" alt="atthing in a terminal: picking three notes, focusing on one, marking one done, the star chart" width="720">

### What
It's an app that helps you to manage your chores and multiple interests in a clever way

### How
Point it at a folder of text/md notes, split by how big each thing is:

```
your-notes/
├── now/        under an hour
├── next/       after now, less urgent
├── try/        experiments, or what comes after next
├── someday/    can wait forever
├── Done/
├── Parked.md
└── Projects.md  optional, see below
```

The first time you open it each day, you pick up to 3 notes (or more, see Setup) from `now/`, `next/` and `try/` by typing their numbers. After that it shows them one at a time.

The keys you use every day:

| key | what it does |
|---|---|
| `d` | done, moves the note to `Done/` |
| `s` | next pick |
| `f` | focus: only the note and a timer (`space` pauses). In a small window, only the timer |
| `l` | back to the list, to see it or change what you picked (`Esc` goes back) |
| `j` `k` / arrows | move on the list, scroll a long note |
| `a` | add a note: type its title, and it's made in the group the `>` marker is in |
| `m` then a letter | move the note to another group's folder: `n` now, `N` next, `t` try. `m` `d` marks it done from the list. The line shows the letters |
| `u` | undo the latest done, park, move, pick or delete from today (asks first); `u` again goes further back. On a focus you forgot to stop, it asks how many minutes it really was |
| `h` | what you did, day by day, under a star chart of the weeks: a bigger star is more focus time, a bright one is a day you got something done |
| `?` | every key for the screen you're on |
| `q` | quit |

<details>
<summary>all the other keys</summary>

| key | what it does |
|---|---|
| `p` | park, moves it to `someday/` and writes it into `Parked.md` with the date (asks first) |
| `J` `K` / Shift+arrows | move a note up or down on the list, to put them in your own order |
| `e` | write in a note with your own editor (`$EDITOR`, e.g. nvim); atthing comes back when you close it |
| `dd` | delete the note: it goes to `.trash/` in your notes folder (hidden in obsidian), and `u` brings it back |
| `:trash` | the deleted notes: `Enter` brings one back, `:empty` deletes them all for good |
| `:set` | change a number without leaving: `:set picks 5`, `:set goal 4`, `:set stars 25 60 120`. `:set` alone shows them |
| `/` | search the titles like vim: the marker jumps while you type and the other notes go grey, `n` / `N` next and previous match, `Esc` clears |
| `t` | show or hide everything but `now/` on the list |
| `Ctrl-R` | redo what `u` undid, like vim |
| `r` | look at the folders again, after changing notes somewhere else. Works on history and projects too, and the list does it by itself each time you go back to it |
| `P` | projects |

</details>

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

<p>
<picture><source media="(prefers-color-scheme: dark)" srcset="assets/the-story-dark.svg"><img src="assets/the-story-light.svg" alt="The Story" height="66"></picture><br>
You know when you have soooOooO many interests and things you want or have to do, and getting overwhelmed is so easy. You can't keep everything in your mind, and you don't want to let go either, because these ideas are really your fuel in life...
</p>

I tried writing everything down, on paper and digitally. It works, but not as much as I wanted.

So I built `atthing`, short for attention-thing.

Now I can let go of the things on my mind in a somewhat organized way and focus on *one thing at a time*. I don't have to think about my Calc III exam when I'm trying to code my game lol

This is not a miracle app that does things for you. It is mostly a symbol, a metaphor that works for me: once a thing is in here, it can leave my mind (mostly doesn't lol but it helps)

A sheet of paper and a pencil can do the same. But I live in terminals, and making this was fun.

### Early
This is so new and there is so much waiting to be made yet stay tuned! See [ROADMAP.md](ROADMAP.md).

---
<sub>Honest: AI help is used on this project</sub>
