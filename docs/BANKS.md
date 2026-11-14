# Banks and presets

whysho SY1 keeps its sounds in **banks**. A bank is one file, `Something.sy1bank`, holding any
number of presets. A single sound can also be saved on its own as `Something.sy1preset`.
Both are plain JSON text: you can open them in any editor, put them in version control,
and send them to a friend.

## Where banks live

| OS      | Folder                                              |
|---------|-----------------------------------------------------|
| Windows | `%APPDATA%\Whysho\SY1\Banks`                        |
| macOS   | `~/Library/Whysho/SY1/Banks`                        |
| Linux   | `~/.config/Whysho/SY1/Banks` (or `$XDG_CONFIG_HOME`) |

- **Factory** is built into the plugin. It is read-only and has no file.
- **User** (`User.sy1bank`) is your default bank. The file appears the first time you save.
- Every other `*.sy1bank` file in the folder shows up as its own bank.
- `favourites.json` in the same folder remembers your starred presets.

## Sharing

**Send a bank:** use *Export bank* in the browser, or just copy the `.sy1bank` file out of the
folder above. The Factory bank can be exported too, which gives you a full example file.

**Install a bank:** use *Import* in the browser and pick the file. You can also copy the file
into the folder by hand and rescan (or reopen the plugin).

- An imported bank never overwrites one you have: if the name is taken it becomes "Name 2".
- Importing a `.sy1preset` adds that sound to your **User** bank.
- Favourites and DAW projects find presets by *bank name + preset name*. Renaming either one
  outside the plugin breaks those links (the project still sounds the same, since a project
  stores every parameter value itself).

## File format

```json
{
  "format": "whysho-sy1-bank",
  "version": 1,
  "name": "My Trap Pack",
  "author": "you",
  "presets": [
    {
      "name": "Slide 808",
      "category": "808",
      "author": "you",
      "notes": "Overlap notes to glide.",
      "params": {
        "osc1_wave": 3,
        "pitch_amt": 18,
        "pitch_dec": 0.05,
        "amp_d": 3.5,
        "voice_mode": "Legato",
        "glide": 0.11,
        "drv_on": 1
      }
    }
  ]
}
```

A `.sy1preset` file is one preset with the header on top:

```json
{
  "format": "whysho-sy1-preset",
  "version": 1,
  "name": "Slide 808",
  "category": "808",
  "author": "you",
  "notes": "",
  "params": { "osc1_wave": 3, "glide": 0.11 }
}
```

### Fields

| Field      | Meaning |
|------------|---------|
| `format`   | Must be `whysho-sy1-bank` or `whysho-sy1-preset`. Anything else is rejected. |
| `version`  | Format version, currently `1`. Files with a newer version still load; parts this version of the plugin doesn't know are skipped. |
| `name`     | Bank or preset name, up to 64 characters. Preset names are unique inside a bank. |
| `author`   | Free text, up to 64 characters. |
| `category` | One of `808`, `Bass`, `Lead`, `Pluck`, `Keys`, `Piano`, `Bell`, `Synth`, `Pad`, `Pulse`, `Strings`, `Brass`, `FX`. Other text is kept, but such a preset only appears under "All". |
| `notes`    | One-line description, up to 512 characters. |
| `params`   | `"parameter id": value` pairs. |

### Parameter values

Values are the real-world numbers you see on the panel, not 0..1:

- frequencies in Hz, times in seconds, levels 0..1, `master_gain` in dB, tuning in octaves /
  semitones / cents;
- switches are `0` or `1` (`true` / `false` also work);
- menus are the position in the list, counting from 0. You may write the label instead, e.g.
  `"osc1_wave": "Sine"` or `"flt_type": "LP 24"`. The plugin always writes numbers.

The parameter ids, ranges and menu orders are the table in `src/Parameters.cpp`. The simplest
reference is an exported Factory bank, since saved files always list every parameter.

When hand-editing you can leave things out:

- a parameter that is **missing** gets its default value;
- an id the plugin **doesn't know** is ignored, so banks keep loading across plugin versions;
- a value **out of range** is moved to the nearest allowed value, and whole-number parameters
  are rounded.

### Limits

Files from the internet are treated with suspicion. A file is refused, with a message saying
why, if it is not valid UTF-8 JSON in this format, is larger than 8 MB, or holds more than
1024 presets. Names and notes are trimmed to the lengths above and stripped of control
characters. Bank file names are derived from the bank name with unsafe characters replaced,
and imports are only ever written inside the Banks folder.
