# jot

*jot* is a simple multiline text editor using the GNU Readline library. It operates inline with the shell prompt, allowing you to enter and edit multiline text directly in the terminal without opening a full-screen editor. Essentially, `jot` functions like `cat` reading from standard input but with Readline support for interactive multiline editing.

![Demo of jot in action](jot-demo.gif)

*Demo by [Colm MacCárthaigh](https://github.com/colmmacc), the originator of this idea.*


## Features

- Multiline text editing in the shell session.
- Supports Emacs and limited Vi modes.
- Customizable key bindings via Readline's `.inputrc` initialization file.
- Can be used as the default editor in Git and other command-line tools.

## Installation

`jot` can be built from a release tarball or directly from the Git repository.

### Building from a Release Tarball

Download the latest release tarball from the [jot releases page](https://github.com/akritid/jot/releases). For example, if the latest version is `jot-1.0.tar.gz`, you can build and install `jot` using the standard `configure` and `make` process:

```bash
wget https://github.com/akritidis/jot/releases/download/v1.0/jot-1.0.tar.gz
tar -xzf jot-1.0.tar.gz
cd jot-1.0
./configure
make
sudo make install
```

### Building from the Git Repository

To build `jot` from the latest source code in the Git repository, you need to generate the configuration script first using `autoreconf`.

Ensure you have GNU Autotools installed (`autoconf`, `automake`, and `libtool`).

Clone the repository:

```bash
git clone https://github.com/akritidis/jot.git
cd jot
```

Generate the configuration script:

```bash
autoreconf --install
```

Configure and install:

```bash
./configure
make
sudo make install
```

## Usage

Start `jot` and redirect the input to a file:

```bash
jot > test.txt
```

Open `jot` to edit an existing file:

```bash
jot file.txt
```

Start `jot` with an empty buffer, ignoring the existing file contents:

```bash
jot -e file.txt
```

### Options

- `-e`: Start with an empty buffer when editing a file. Existing file contents are ignored and overwritten upon saving.
- `-b banner`: Display the specified `banner` message before starting. Useful for providing instructions or context.

## Key Bindings

`jot` supports various key bindings to enhance multiline editing. The default Readline key bindings are available unless specified otherwise.

### Exiting the Editor

- `Ctrl+D`: At the end of the buffer, invokes `accept-line` to complete editing. Otherwise, deletes the character under the cursor.
- `Ctrl+N`: Completes editing and accepts the input.

### Cursor Movement

- `Enter`: Inserts a newline character.
- `Ctrl+A`, `Home`: Moves to the beginning of the current line.
- `Ctrl+E`, `End`: Moves to the end of the current line.
- `Up Arrow`: Moves the cursor up one line.
- `Down Arrow`: Moves the cursor down one line.
- `Alt+<`: Moves to the beginning of the text.
- `Alt+>`: Moves to the end of the text.

### Editing Text

- `Ctrl+K`: Kills (cuts) text from the cursor to the end of the line.
- `Ctrl+U`: Kills (cuts) text from the beginning of the line to the cursor.

### Unbound Default Functions

To prevent interference with multiline editing, several default Readline functions are unbound in `jot`:

- **Completion functions**: Auto-completion and related functions are disabled.
- **History search functions**: Reverse and forward history searches are disabled.
- **Search functions**: Incremental search functions are disabled.

### Vi Mode

In Vi mode, `jot` supports limited Vi commands for multiline editing.

- `j`: Moves the cursor down one line.
- `k`: Moves the cursor up one line.
- `J`: Joins the current line with the next line.
- `o`: Inserts a new line below the current line and enters insert mode.
- `O`: Inserts a new line above the current line and enters insert mode.
- `^`: Moves to the beginning of the current line.
- `$`: Moves to the end of the current line.
- `G`: Goes to the specified line number or the end of the text.
- `gg`: Goes to the beginning of the text.
- `dd`: Deletes the current line.
- `D`: Deletes from the cursor to the end of the line.

To enable Vi mode, add the following to your `~/.inputrc`:

```bash
$if jot
    set editing-mode vi
$endif
```

## Configuration

You can customize `jot`'s key bindings and behavior using the Readline initialization file (`inputrc`), applying settings specifically for `jot` with conditional blocks.

For example, to rebind the accept line key to `Ctrl+X`, add:

```bash
$if jot
    "\C-x": accept-line
$endif
```

To change the key binding for moving to the beginning of the line:

```bash
$if jot
    "\C-b": beginning-of-line-multiline
$endif
```

## Using with Git

To use `jot` as your default Git editor:

```bash
git config --global core.editor jot
```

Or set the `EDITOR` environment variable:

```bash
export EDITOR=jot
```

**Note**: Git might display the message:

```
hint: Waiting for your editor to close the file...
```

Since `jot` operates inline in the terminal and doesn't open a separate window, this message causes display issues during editing. To suppress it:

```bash
git config --global advice.waitingForEditor false
```

This provides a cleaner experience when using `jot` as your Git editor.

## Limitations

- **Vi Mode**: Vi mode is limited and doesn't fully implement all Vi commands.
- **Multibyte Unicode Support**: `jot`'s custom navigation functions currently do not fully support multibyte Unicode characters; input is limited to single-byte characters.
- **Crash Recovery**: Unsaved changes may be lost in case of a crash; no effort is made to preserve contents.
- **Scrolling Large Text**: If input exceeds the terminal's visible area, display artifacts may occur. Press `Ctrl+L` to refresh the display.

## License

`jot` is licensed under the [GNU General Public License v2.0 or later](LICENSE).

## Acknowledgments

The idea for `jot` comes from an early implementation of multiline editing with Readline by [Colm MacCárthaigh](https://github.com/colmmacc) in the `c-hey` tool.

This version of `jot` was written using GPT-4 by Periklis Akritidis.

## Reporting Bugs

Report bugs or feature requests to [jot-bugs@akritidis.org](mailto:jot-bugs@akritidis.org).

