# Roadmap

Where atthing is going, roughly in order. Nothing here is a promise:
while the version starts with `0.`, anything can still change.

The idea behind all of it: **one place that tells you what's next and helps you
focus on it, instead of everything being open at once.** A feature belongs here
if it closes things, not if it opens more.

## v0.1: one thing at a time (released)

- [x] Find notes in `now/` and `try/`
- [x] Read what's inside each note
- [x] Full-screen view, one note at a time
- [x] Pick up to 3 notes for the day (new day at midnight)
- [x] Done / park a note, and keep a log of what you did
- [x] Focus mode with a timer
- [x] Remember when a note first appeared, not just when it last changed
- [x] Count what's done today (3/3, 4/3...) and keep going
- [x] `.txt` notes, sorting, skip sync-conflict files
- [x] First release

## v0.2: after the first real use (released)

- [x] Go back to the list from a note
- [x] Change what you picked
- [x] Arrow keys, Page Up/Down, mouse wheel
- [x] Scroll long notes

## v0.3: calmer, and a look back (released)

- [x] NOW and TRY groups on the list, TRY folds away
- [x] Put notes in your own order
- [x] Refresh when notes change outside the app
- [x] See what you did, day by day
- [x] Projects from a `Projects.md` file, with time this week
- [x] Only the main keys on screen, `?` for all of them
- [x] Focus shows only the timer in a small window
- [x] Park asks first

## v0.4: easier to set up, and easier to find things (released)

- [x] A NEXT group between NOW and TRY
- [x] First run asks where your notes are and makes the folders
- [x] Settings in a config file, your own group names
- [x] Move a note to another group from the list
- [x] Refresh on history and projects, and when going back to the list
- [x] Search the list with `/`, like vim
- [x] A star chart of the weeks on `h`

## Next

- [x] Undo with `u`, and fix a focus you forgot to stop
- [x] Make a note with `a`, write in it with `e` in your own editor

- Parked and someday notes come back when their time comes
- Timer that knows the estimate written in a note
- Basic markdown rendering (bold, bullets, headings)

## Later

- Its own small window that stays visible on a screen
- Something that grows while you focus, like a night sky
- Time estimates written in notes, and learning from how long things really took
- Split a big task into small steps, with AI help
- A daily to-do list, separate from the inbox
- Habit building, based on how habits actually form
- See where screen time went
- Phone version
- A place to do the work itself, not just decide it

## Languages

v0.1 is plain C. Later, one part gets rewritten in Zig and linked into the same
program, and the window version is written in Rust.
