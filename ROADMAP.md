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

## Next

- Set up the folders automatically in an empty directory
- Choose the folder without an environment variable
- Timer that knows the estimate written in a note
- Basic markdown rendering (bold, bullets, headings)
- Edit a note inside the app

## Later

- Its own small window that stays visible on a screen
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
