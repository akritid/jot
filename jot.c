/* jot.c - A multiline text editor using GNU Readline

   Copyright (C) 2024 Periklis Akritidis

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>    /* For INT_MAX */
#include <unistd.h>    /* For getopt */
#include <errno.h>     /* For errno */
#include <string.h>    /* For strlen and other string functions */
#include <termios.h>   /* Used by disable_ctrl_u_kill_line() */
#include <signal.h>
#include <alloca.h>
#include <assert.h>
#include <sys/wait.h>  /* For waitpid */
#include <fcntl.h>     /* For open flags */
#include <readline/readline.h>

#define DEFAULT_BANNER ""
#define PROGRAM_NAME "jot"

/* Global variables to save original terminal settings */
static struct termios original_termios; /* To store original settings */
static int termios_saved = 0;           /* Flag to check if settings are saved */

/* Global variable to hold the file contents */
static char *file_contents = NULL;


/* Global variable to hold the edited filename */
static char *filename = NULL;

/*
 * These are the standard Emacs and Vi keymaps provided by Readline.
 */
static Keymap keymaps[] = {
	emacs_standard_keymap,
	emacs_meta_keymap,
	emacs_ctlx_keymap,
	vi_movement_keymap,
	vi_insertion_keymap
};

/*
 * Functions to Bind/Unbind in All Keymaps:
 *  - bind_func_in_all_keymaps(): Binds a function to a key sequence across all keymaps.
 *  - unbind_func_in_all_keymaps(): Unbinds a function from all keymaps.
 *  - Both functions iterate over the keymaps array
 */

static void
bind_func_in_all_keymaps(const char *seq, rl_command_func_t *func)
{
	for (size_t i = 0; i < sizeof(keymaps) / sizeof(keymaps[0]); i++) {
		rl_generic_bind(ISFUNC, seq, (char *)func, keymaps[i]);
	}
}

static void
unbind_func_in_all_keymaps(rl_command_func_t *func)
{
	for (size_t i = 0; i < sizeof(keymaps) / sizeof(keymaps[0]); i++) {
		rl_unbind_function_in_map(func, keymaps[i]);
	}
}

/* Bind only in vi keymaps */
static void
bind_func_in_vi_movement_keymap(const char *seq, rl_command_func_t *func)
{
	rl_generic_bind(ISFUNC, seq, (char *)func, vi_movement_keymap);
}

/* Bind key sequences in both Emacs standard keymap and Vi insertion keymap */
static void
bind_func_in_insert_maps(const char *seq, rl_command_func_t *func)
{
	/* Bind in Emacs standard keymap */
	rl_bind_keyseq_in_map(seq, func, emacs_standard_keymap);
	/* Bind in Vi insertion keymap */
	rl_bind_keyseq_in_map(seq, func, vi_insertion_keymap);
}

static int
jot_beginning_of_line(int count, int key)
{
	/* Move to the start of the current line */
	while (rl_point > 0) {
		/* Peek at previous character */
		int saved_point = rl_point;
		rl_backward_char(1, 0);
		if (rl_line_buffer[rl_point] == '\n') {
			rl_forward_char(1, 0);
			break;
		}
		if (rl_point == 0 || rl_point == saved_point) {
			break;
		}
	}
	rl_redisplay();
	return 0;
}

static int
jot_end_of_line(int count, int key)
{
	int buffer_len = rl_end; /* rl_end is the length of the line buffer */
	/* Move rl_point to the end of the current line or buffer */
	while (rl_point < buffer_len) {
		if (rl_line_buffer[rl_point] == '\n') {
			break;
		}
		rl_forward_char(1, 0);
		if (rl_point >= buffer_len) {
			break;
		}
	}
	rl_redisplay();
	return 0;
}

static int
jot_kill_line(int count, int key)
{
	int start = rl_point;
	int end = rl_point;
	int buffer_len = rl_end; /* Total length of the line buffer */

	/* Save the current point */
	int orig_point = rl_point;

	/* Move the rl_point to the end of the current line, excluding the newline */
	while (rl_point < buffer_len) {
		if (rl_line_buffer[rl_point] == '\n') {
			/* Do not include the newline character */
			break;
		}
		/* Move forward one character (multibyte-aware) */
		rl_forward_char(1, 0);
	}

	end = rl_point; /* Update the end position */

	/* Restore rl_point to its original position */
	rl_point = orig_point;

	/* Delete text from start to end */
	rl_delete_text(start, end);
	rl_point = start; /* Set cursor position back to start */
	rl_redisplay();   /* Update the display */

	return 0;
}



/* Function to restore terminal settings */
static void
restore_terminal_settings(void)
{
	if (termios_saved) {
	/* Open /dev/tty to get the terminal file descriptor */
	int fd = open("/dev/tty", O_RDWR);
	if (fd == -1) {
		perror("open /dev/tty");
		return;
	}

	/* Restore the original terminal attributes */
	if (tcsetattr(fd, TCSANOW, &original_termios) == -1) {
		perror("tcsetattr");
		close(fd);
		return;
		}
	termios_saved = 0; /* Reset the flag */
		close(fd);
	}
}


/* Signal handler to catch signals and restore terminal settings */
static void
signal_handler(int signum)
{
	restore_terminal_settings();

	/* Restore default signal handler and re-raise the signal */
	signal(signum, SIG_DFL);
	raise(signum);
}

static void
save_terminal_settings(void)
{
	/* Open /dev/tty to get the terminal file descriptor */
	int fd = open("/dev/tty", O_RDWR);
	if (fd == -1) {
		perror("open /dev/tty");
		exit(EXIT_FAILURE);
	}

	if (tcgetattr(fd, &original_termios) == -1) {
		perror("tcgetattr");
		close(fd);
		exit(EXIT_FAILURE);
	}
	termios_saved = 1;
	close(fd);
}

static void
disable_ctrl_u_kill_line(void)
{
	struct termios term;

	/* Open /dev/tty to get the terminal file descriptor */
	int fd = open("/dev/tty", O_RDWR);
	if (fd == -1) {
		perror("open /dev/tty");
		return;
	}

	/* Get the current terminal attributes */
	if (tcgetattr(fd, &term) == -1) {
		perror("tcgetattr");
		close(fd);
		return;
	}

	/* Disable the line-kill character */
	term.c_cc[VKILL] = _POSIX_VDISABLE;

	/* Set the modified attributes immediately */
	if (tcsetattr(fd, TCSANOW, &term) == -1) {
		perror("tcsetattr");
		close(fd);
		return;
	}
	close(fd);
}

static FILE *
redirect_stdio_to_tty(void)
{
	/* Save the original stdout */
	int orig_stdout_fd = dup(STDOUT_FILENO);
	if (orig_stdout_fd == -1) {
		perror("dup STDOUT_FILENO");
		_exit(EXIT_FAILURE);
	}

	FILE *orig_stdout = fdopen(orig_stdout_fd, "w");
	if (orig_stdout == NULL) {
		perror("fdopen orig_stdout_fd");
		close(orig_stdout_fd);
		_exit(EXIT_FAILURE);
	}

	/* Open /dev/tty for input and output */
	int tty_fd = open("/dev/tty", O_RDWR);
	if (tty_fd == -1) {
		perror("open /dev/tty");
		_exit(EXIT_FAILURE);
	}

	/* Duplicate tty_fd to stdin, stdout, stderr */
	if (dup2(tty_fd, STDIN_FILENO) == -1) {
		perror("dup2 stdin");
		_exit(EXIT_FAILURE);
	}
	if (dup2(tty_fd, STDOUT_FILENO) == -1) {
		perror("dup2 stdout");
		_exit(EXIT_FAILURE);
	}
	if (dup2(tty_fd, STDERR_FILENO) == -1) {
		perror("dup2 stderr");
		_exit(EXIT_FAILURE);
	}

	/* Close tty_fd if it's not stdin, stdout, or stderr */
	if (tty_fd > 2) {
		close(tty_fd);
	}

	return orig_stdout;
}

static int
jot_kill_backward_line(int count, int key)
{
	int start = rl_point;
	int end = rl_point;
	/* Find the start of the current line */
	while (start > 0 && rl_line_buffer[start - 1] != '\n') {
		start--;
	}
	/* Kill text from start to end */
	rl_kill_text(start, end);
	rl_point = start;
	rl_redisplay();
	return 0;
}

static int
jot_kill_whole_line(int count, int key)
{
	int buffer_len = strlen(rl_line_buffer);
	int start = rl_point;
	int end = rl_point;

	/* Find the start of the current line */
	while (start > 0 && rl_line_buffer[start - 1] != '\n') {
		start--;
	}

	/* Find the end of the current line */
	while (end < buffer_len && rl_line_buffer[end] != '\n') {
		end++;
	}
	/* Include the newline character if present */
	if (end < buffer_len && rl_line_buffer[end] == '\n') {
		end++;
	}

	/* Kill text from start to end */
	rl_kill_text(start, end);
	rl_point = start;
	rl_redisplay();
	return 0;
}

static int
jot_custom_ctrl_d(int count, int key)
{
	if (count <= 0) {
		return 0;
	}
	if (rl_point < rl_end) {
		/* Delete the character under the cursor */
		return rl_delete(count, key);
	} else {
		return rl_named_function("accept-line")(count, key);
	}
}

/* Function to move the cursor up 'count' lines */
static int
jot_move_cursor_up(int count, int key)
{
	while (count-- > 0) {
		int orig_point = rl_point;
		int line_col = 0;

		/* Move to the start of the current line */
		while (rl_point > 0) {
			rl_backward_char(1, 0);
			if (rl_line_buffer[rl_point] == '\n') {
				rl_forward_char(1, 0);
				break;
			}
			line_col++;
		}

		/* If we're at the beginning of the buffer, ring bell and restore position */
		if (rl_point == 0) {
			rl_point = orig_point;
			rl_ding();
			break;
		}

		/* Move back over the newline character to the end of the previous line */
		rl_backward_char(1, 0);

		/* Move to the start of the previous line */
		while (rl_point > 0) {
			rl_backward_char(1, 0);
			if (rl_line_buffer[rl_point] == '\n') {
				rl_forward_char(1, 0);
				break;
			}
		}

		/* Move to the calculated column in the previous line */
		for (int i = 0; i < line_col; i++) {
			if (rl_line_buffer[rl_point] == '\n' || rl_point >= rl_end) {
				break;
			}
			rl_forward_char(1, 0);
		}
	}
	rl_redisplay();
	return 0;
}

/* Function to move the cursor down count lines */
static int
jot_move_cursor_down(int count, int key)
{
	while (count-- > 0) {
		int buffer_len = rl_end; /* Total number of characters in the buffer */
		int orig_point = rl_point; /* Save the original cursor position */
		int line_start = rl_point;
		int line_col = 0;

		/* Find the start of the current line */
		rl_point = line_start;
		while (rl_point > 0) {
			int saved_point = rl_point;
			rl_backward_char(1, 0); /* Move back one character */
			if (rl_line_buffer[rl_point] == '\n') {
				rl_forward_char(1, 0); /* Move forward to the first character of the current line */
				break;
			}
			if (rl_point == saved_point) {
				/* Can't move further back */
				break;
			}
			line_start = rl_point;
			line_col++;
		}

		/* Find the end of the current line */
		int line_end = rl_point;
		rl_point = line_end;
		while (rl_point < buffer_len) {
			if (rl_line_buffer[rl_point] == '\n') {
				break;
			}
			int saved_point = rl_point;
			rl_forward_char(1, 0); /* Move forward one character */
			if (rl_point == saved_point) {
				/* Can't move forward */
				break;
			}
			line_end = rl_point;
		}

		/* Check if we're at the last line */
		if (line_end >= buffer_len) {
			rl_point = orig_point; /* Restore original position */
			rl_ding();			 /* Beep to indicate no more lines below */
			break;
		}

		/* Move to the start of the next line */
		rl_point = line_end;
		if (rl_point < buffer_len && rl_line_buffer[rl_point] == '\n') {
			rl_forward_char(1, 0); /* Move past the newline character */
		}

		int next_line_start = rl_point;

		/* Find the end of the next line */
		int next_line_end = next_line_start;
		rl_point = next_line_start;
		while (rl_point < buffer_len) {
			if (rl_line_buffer[rl_point] == '\n') {
				break;
			}
			int saved_point = rl_point;
			rl_forward_char(1, 0);
			if (rl_point == saved_point) {
				/* Can't move forward */
				break;
			}
			next_line_end = rl_point;
		}

		/* Compute the column position in the next line (line_col may exceed next_line_length) */
		int next_line_length = next_line_end - next_line_start;

		int target_col = (line_col < next_line_length) ? line_col : next_line_length;

		/* Move to the calculated column in the next line */
		rl_point = next_line_start;
		for (int i = 0; i < target_col && rl_point < next_line_end; i++) {
			if (rl_line_buffer[rl_point] == '\n') {
				break;
			}
			int saved_point = rl_point;
			rl_forward_char(1, 0);
			if (rl_point == saved_point) {
				/* Can't move forward */
				break;
			}
		}
		/* If we moved past the end of the line, adjust rl_point back */
		if (rl_point > next_line_end) {
			rl_point = next_line_end;
		}
	}
	rl_redisplay();
	return 0;
}

/* Custom function to insert a newline character */
static int
jot_insert_newline(int count, int key)
{
	/* Insert a newline character into the input buffer */
	rl_insert_text("\n");
	/* Redisplay the input line with the new content */
	rl_redisplay();
	return 0; /* Return 0 to indicate the key has been handled */
}

static int
jot_move_to_first_nonblank_next_line(int count, int key)
{
	while (count-- > 0) {
		int buffer_len = strlen(rl_line_buffer);
		int current_pos = rl_point;

		/* Find the end of the current line */
		int line_end = current_pos;
		while (line_end < buffer_len && rl_line_buffer[line_end] != '\n') {
			line_end++;
		}

		/* If we're at the last line, do nothing */
		if (line_end >= buffer_len) {
			rl_ding();
			break;
		}

		/* Move to the start of the next line */
		int next_line_start = line_end + 1;

		/* Find the end of the next line */
		int next_line_end = next_line_start;
		while (next_line_end < buffer_len && rl_line_buffer[next_line_end] != '\n') {
			next_line_end++;
		}

		/* Move to the first non-blank character in the next line */
		int pos = next_line_start;
		while (pos < next_line_end && isspace((unsigned char)rl_line_buffer[pos])) {
			pos++;
		}

		/* Set the new cursor position */
		rl_point = pos;
	}
	rl_redisplay();
	return 0;
}

/*
 * Vi command to delete 'count' lines ('dd').
 * It behaves slightly differently from jot_kill_line()
 */
static int
jot_vi_delete_current_line(int count, int key)
{
	int buffer_len = strlen(rl_line_buffer);
	int start = rl_point;
	int end = rl_point;

	/* Find the start of the current line */
	while (start > 0 && rl_line_buffer[start - 1] != '\n') {
		start--;
	}

	/* Delete 'count' lines */
	for (int i = 0; i < count; i++) {
		/* Find the end of the current line */
		while (end < buffer_len && rl_line_buffer[end] != '\n') {
			end++;
		}
		/* Include the newline character */
		if (end < buffer_len && rl_line_buffer[end] == '\n') {
			end++;
		}
	}

	/* Remove the text from start to end */
	rl_kill_text(start, end);
	rl_point = start;
	rl_redisplay();
	return 0;
}

/* Vi command to delete to end of line ('D') */
static int
jot_vi_delete_to_end_of_line(int count, int key)
{
	int buffer_len = strlen(rl_line_buffer);
	int start = rl_point;
	int end = rl_point;

	/* Delete to the end of the current line or multiple lines */
	for (int i = 0; i < count; i++) {
		while (end < buffer_len && rl_line_buffer[end] != '\n') {
			end++;
		}
		if (i < count - 1 && end < buffer_len && rl_line_buffer[end] == '\n') {
			end++; /* Include newline if more counts are left */
		}
	}

	/* Remove text from cursor to end */
	rl_kill_text(start, end);
	rl_point = start;
	rl_redisplay();
	return 0;
}

/* Vi command to join lines ('J') */
static int
jot_vi_join_lines(int count, int key)
{
	rl_begin_undo_group();

	while (count-- > 0) {
		int buffer_len = rl_end;  /* rl_end is the length of the line buffer */
		int start_pos = rl_point;

		/* Move rl_point to the end of the current line */
		while (rl_point < buffer_len) {
			if (rl_line_buffer[rl_point] == '\n') {
				break;
			}
			int saved_point = rl_point;
			rl_forward_char(1, 0);  /* Move forward one character */
			if (rl_point == saved_point) {
				/* Can't move further */
				break;
			}
		}

		/* Check if we have a newline to join with */
		if (rl_point >= buffer_len || rl_line_buffer[rl_point] != '\n') {
			rl_point = start_pos; /* Restore cursor position */
			rl_ding(); /* Beep to indicate no action */
			break;
		}

		/* Delete the newline character */
		rl_delete_text(rl_point, rl_point + 1);
		buffer_len--;

		/* Remove leading ASCII spaces and tabs from the next line */
		while (rl_point < buffer_len) {
			char c = rl_line_buffer[rl_point];
			if (c != ' ' && c != '\t') {
				break;
			}
			rl_delete_text(rl_point, rl_point + 1);
			buffer_len--;
		}

		/* Decide whether to insert a space */
		char before_char = '\0', after_char = '\0';

		/* Get the character before the join */
		if (rl_point > 0) {
			int saved_point = rl_point;
			rl_backward_char(1, 0);
			before_char = rl_line_buffer[rl_point];
			rl_point = saved_point; /* Restore rl_point */
		}

		/* Get the character after the join */
		if (rl_point < buffer_len) {
			after_char = rl_line_buffer[rl_point];
		}

		int need_space = 0;
		if (before_char != '\0' && after_char != '\0' &&
			before_char != ' ' && after_char != ' ' &&
			before_char != '\n' && after_char != '\n') {
			need_space = 1;
		}

		if (need_space) {
			rl_insert_text(" ");
			buffer_len++;
			/* rl_point advances after insertion */
		}

		/* Optionally adjust cursor position */
		rl_point = start_pos;
	}

	rl_end_undo_group();
	rl_redisplay();
	return 0;
}

static void
goto_line(int count)
{
	int pos = 0;
	int current_line = 1;
	int target_line = count;

	int buffer_len = rl_end;

	/* Move to the desired line number */
	while (pos < buffer_len && current_line <= target_line) {
		if (rl_line_buffer[pos++] == '\n') {
			current_line++;
		}
	}

	/* Ensure pos is at the start of the target line */

	/* Move back to the start of the line in case pos overshot due to newline */
	if (pos > 0 && rl_line_buffer[pos - 1] == '\n') {
		pos--;
	}
	while (pos > 0 && rl_line_buffer[pos - 1] != '\n') {
		pos--;
	}
	/* Now move forward to the first non-blank character */
	while (pos < buffer_len && isspace((unsigned char)rl_line_buffer[pos]) && rl_line_buffer[pos] != '\n') {
		pos++;
	}
	rl_point = pos;
}

/* Vi command to go to line 'count' or end ('G') */
static int
jot_vi_goto_line(int count, int key)
{
	if (rl_explicit_arg) {
		/* User provided a count, move to that line number */
		goto_line(count);
	} else {
		goto_line(INT_MAX);
	}

	rl_redisplay();
	return 0;
}

/* Vi command to go to first line ('gg') */
static int
jot_vi_goto_first_line(int count, int key)
{
	if (rl_explicit_arg) {
		/* User provided a count, move to that line number */
		goto_line(count);
	} else {
		goto_line(1);
	}

	rl_redisplay();
	return 0;
}

/* Vi command to insert a line below current line and enter insert mode ('o') */
static int
jot_vi_insert_line_below(int count, int key)
{
	int buffer_len = strlen(rl_line_buffer);
	int pos = rl_point;

	/* Find the end of the current line */
	while (pos < buffer_len && rl_line_buffer[pos] != '\n') {
		pos++;
	}

	/* Move after the newline character if there is one */
	if (pos < buffer_len && rl_line_buffer[pos] == '\n') {
		pos++; /* Move after newline */
	}
	/* Else, pos remains at buffer_len (end of buffer) */

	/* Move rl_point to pos */
	rl_point = pos;

	/* Insert a newline character */
	rl_insert_text("\n");

	/* After insertion, rl_point advances by 1 to pos + 1 */

	/* If we are not at the end of the buffer, adjust rl_point back to the start of the new line */
	if (pos < buffer_len) {
		rl_point = pos;
	}
	/* Otherwise, when at the end of the buffer, rl_point is already correctly placed */

	/* Switch to Vi insert mode */
	rl_vi_insertion_mode(1, 0);

	rl_redisplay();
	return 0;
}


/* Vi command to insert a line above current line and enter insert mode ('O') */
static int
jot_vi_insert_line_above(int count, int key)
{
	int pos = rl_point;

	/* Find the start of the current line */
	while (pos > 0 && rl_line_buffer[pos - 1] != '\n') {
		pos--;
	}

	rl_point = pos;

	/* Insert a newline character */
	rl_insert_text("\n");

	/* Move cursor to the beginning of the new line */
	rl_point = pos;

	/* Switch to Vi insert mode */
	rl_vi_insertion_mode(1, 0);

	rl_redisplay();
	return 0;
}

/* Function to read file contents into a dynamically allocated buffer */
static char *
read_file_contents(const char *filename)
{
	char *contents = NULL;
	FILE *file_read = fopen(filename, "r");

	if (!file_read) {
		if (errno != ENOENT) {
			perror("fopen filename for reading");
			return NULL;
		}
		/* If file does not exist, proceed with empty buffer */
		return NULL;
	}

	/* Determine the file size */
	if (fseek(file_read, 0, SEEK_END) != 0) {
		perror("fseek");
		fclose(file_read);
		return NULL;
	}

	long file_size = ftell(file_read);
	if (file_size < 0) {
		perror("ftell");
		fclose(file_read);
		return NULL;
	}

	if (fseek(file_read, 0, SEEK_SET) != 0) {
		perror("fseek");
		fclose(file_read);
		return NULL;
	}

	/* Allocate buffer for file contents */
	contents = malloc(file_size + 1);
	if (!contents) {
		perror("malloc");
		fclose(file_read);
		return NULL;
	}

	/* Read the file contents */
	size_t nread = fread(contents, 1, file_size, file_read);
	if (nread != (size_t)file_size) {
		perror("fread");
		free(contents);
		fclose(file_read);
		return NULL;
	}
	contents[file_size] = '\0'; /* Null-terminate the string */
	fclose(file_read);
	return contents;
}

static int
jot_invoke_fullscreen_editor(int count, int key)
{
	/*
	 * Save the current Readline buffer to a temporary file
	 */

	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL || tmpdir[0] == '\0') {
		tmpdir = "/tmp";
	}

	const char *template = "/jot_edit_XXXXXX";
	size_t tmpdir_len = strlen(tmpdir);
	size_t template_len = strlen(template);

	/* Allocate buffer for temp_filename */
	char *temp_filename = alloca(tmpdir_len + template_len + 1);
	if (temp_filename == NULL) {
		perror("alloca");
		exit(EXIT_FAILURE);
	}

	/* Construct the path */
	strcpy(temp_filename, tmpdir);
	strcat(temp_filename, template);
	
	/* Create a temporary file */
	int fd = mkstemp(temp_filename);
	if (fd == -1) {
		perror("mkstemp");
		exit(EXIT_FAILURE);
	}
	
	/* Write the current Readline buffer to the temporary file */
	FILE *fp = fdopen(fd, "w");
	if (!fp) {
		perror("fdopen");
		close(fd);
		unlink(temp_filename);
		exit(EXIT_FAILURE);
	}
	if (fputs(rl_line_buffer, fp) == EOF) {
		perror("fputs");
		fclose(fp);
		unlink(temp_filename);
		exit(EXIT_FAILURE);
	}
	fclose(fp); /* This also closes the underlying file descriptor */

	/* Restore terminal settings before launching the editor */
	restore_terminal_settings();

	/* Deinitialize Readline terminal settings before launching the editor */
	rl_deprep_terminal();

	/*
	 * Get the editor command from $JOT_EDITOR environment variable,
	 * or default to 'vi'
	 */
	const char *editor = getenv("JOT_EDITOR");
	if (editor == NULL || editor[0] == '\0') {
		editor = "vi";
	}

	/* Build the command string */
	char *cmd = NULL;
	if (asprintf(&cmd, "%s %s", editor, temp_filename) == -1) {
		perror("asprintf");
		unlink(temp_filename);
		exit(EXIT_FAILURE);
	}

	/* Execute the command */
	int ret = system(cmd);
	free(cmd); /* Free the command string */

	if (ret == -1) {
		perror("system");
		unlink(temp_filename);
		exit(EXIT_FAILURE);
	} else {
		/* Check the exit status */
		if (WIFEXITED(ret)) {
			int exit_status = WEXITSTATUS(ret);
			if (exit_status != 0) {
				fprintf(stderr, "%s exited with status %d\n", editor, exit_status);
				unlink(temp_filename);
				exit(EXIT_FAILURE);
			}
		} else if (WIFSIGNALED(ret)) {
			int term_sig = WTERMSIG(ret);
			fprintf(stderr, "%s terminated by signal %d\n", editor, term_sig);
			unlink(temp_filename);
			exit(EXIT_FAILURE);
		}
	}

	/* After the editor exits, read the updated contents */
	char *new_contents = read_file_contents(temp_filename);
	if (!new_contents) {
		perror("Failed to read updated file contents");
		unlink(temp_filename);
		exit(EXIT_FAILURE);
	}

	/* Replace the Readline buffer with the new contents */
	rl_replace_line(new_contents, 0);
	/* Move the cursor to the end of the buffer */
	rl_point = rl_end = strlen(new_contents);
	/* Redisplay the updated buffer */
	rl_redisplay();
	/* Free the allocated buffer */
	free(new_contents);

	/* Remove the temporary file */
	unlink(temp_filename);

	/* Reinitialize Readline terminal settings */
	rl_prep_terminal(1);

	/* Re-disable Ctrl+U if necessary (since terminal settings were restored) */
	disable_ctrl_u_kill_line();

	return 0;
}

/* Function to initialize the Readline buffer with file contents */
static int
initialize_readline_buffer(void)
{
	if (file_contents) {
		/* Insert the file contents into the Readline buffer */
		rl_insert_text(file_contents);
		/* Optionally, move the cursor to the beginning */
		rl_point = 0;
	}

	return 0;
}


int
main(int argc, char **argv)
{
	int opt_e = 0;       /* Whether the -e option is specified */
	int opt;
	char *input = NULL;
	FILE *file_write = NULL;
	FILE *orig_stdout = stdout;
	int exit_status = EXIT_SUCCESS;
	char *banner = DEFAULT_BANNER;

	save_terminal_settings();

	/* Parse command-line options using getopt */
	while ((opt = getopt(argc, argv, "eb:")) != -1) {
		switch (opt) {
		case 'e':
			opt_e = 1;
			break;
		case 'b':
			banner = optarg;
			break;
		default:
			fprintf(stderr, "Usage: %s [-e] [-b banner] [filename]\n", argv[0]);
			exit_status = EXIT_FAILURE;
			goto exit_program;
		}
	}

	/* Get the filename if provided */
	if (optind < argc) {
		filename = argv[optind];  /* Set the global filename */
	} else {
		filename = NULL;
	}

	/* For conditional processing of inputrc */
	rl_readline_name = PROGRAM_NAME;


 	/*
 	 * Open /dev/tty for input and output
 	 * Only open /dev/tty for input and output when necessary
 	 * That is, when not editing a named file and stdin or
 	 * stdout is not a terminal
 	 */
	if (filename == NULL &&
	    (!isatty(fileno(stdin)) || !isatty(fileno(stdout)))) {
		orig_stdout = redirect_stdio_to_tty();
	}

	/*
	 * Set up signal handlers
	 */
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sa.sa_flags = 0; /* or SA_RESTART to restart interrupted system calls */
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) == -1) {
		perror("sigaction SIGINT");
		exit_status = EXIT_FAILURE;
		goto exit_program;
	}

	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		perror("sigaction SIGTERM");
		exit_status = EXIT_FAILURE;
		goto exit_program;
	}

	/*
	 * Add our function names
	 */
	rl_add_defun("jot-insert-newline", jot_insert_newline, -1);
	rl_add_defun("jot-move-cursor-up", jot_move_cursor_up, -1);
	rl_add_defun("jot-move-cursor-down", jot_move_cursor_down, -1);
	rl_add_defun("jot-beginning-of-line", jot_beginning_of_line, -1);
	rl_add_defun("jot-end-of-line", jot_end_of_line, -1);
	rl_add_defun("jot-kill-line", jot_kill_line, -1);
	rl_add_defun("jot-kill-backward-line", jot_kill_backward_line, -1);
	rl_add_defun("jot-kill-whole-line", jot_kill_whole_line, -1);
	rl_add_defun("jot-custom-ctrl-d", jot_custom_ctrl_d, -1);
	rl_add_defun("jot-invoke-fullscreen-editor", jot_invoke_fullscreen_editor, -1);
	rl_add_defun("jot-move-to-first-nonblank-next-line", jot_move_to_first_nonblank_next_line, -1);

	/*
	 * Add Vi-specific functions
	 */
	rl_add_defun("jot-vi-join-lines", jot_vi_join_lines, -1);
	rl_add_defun("jot-vi-insert-line-below", jot_vi_insert_line_below, -1);
	rl_add_defun("jot-vi-insert-line-above", jot_vi_insert_line_above, -1);
	rl_add_defun("jot-vi-goto-line", jot_vi_goto_line, -1);
	rl_add_defun("jot-vi-goto-first-line", jot_vi_goto_first_line, -1);
	rl_add_defun("jot-vi-delete-current-line", jot_vi_delete_current_line, -1);
	rl_add_defun("jot-vi-delete-to-end-of-line", jot_vi_delete_to_end_of_line, -1);

	rl_bind_key('\t', rl_insert); /* disable auto-completion */

	/*
	 * Unbind functions that can mess up formatting
	 */
	unbind_func_in_all_keymaps(rl_insert_comment);
	unbind_func_in_all_keymaps(rl_complete);
	unbind_func_in_all_keymaps(rl_insert_completions);
	unbind_func_in_all_keymaps(rl_possible_completions);
	unbind_func_in_all_keymaps(rl_menu_complete);
	unbind_func_in_all_keymaps(rl_reverse_search_history);
	unbind_func_in_all_keymaps(rl_forward_search_history);
	unbind_func_in_all_keymaps(rl_history_search_forward);
	unbind_func_in_all_keymaps(rl_history_search_backward);
	unbind_func_in_all_keymaps(rl_noninc_forward_search);
	unbind_func_in_all_keymaps(rl_noninc_reverse_search);
	unbind_func_in_all_keymaps(rl_noninc_forward_search_again);
	unbind_func_in_all_keymaps(rl_noninc_reverse_search_again);

	/* Bind the Enter key (usually '\r') to insert a newline character */
	bind_func_in_insert_maps("\r", jot_insert_newline);

	/* Bind Ctrl+n to accept the line */
	bind_func_in_all_keymaps("\\C-n", rl_named_function("accept-line"));

	/* Bind the custom Ctrl-D function */
	bind_func_in_all_keymaps("\\C-d", jot_custom_ctrl_d);

	/* Bind Up/Down arrows to custom cursor movement functions */
	bind_func_in_all_keymaps("\\e[A", jot_move_cursor_up);   /* Up arrow */
	bind_func_in_all_keymaps("\\e[B", jot_move_cursor_down); /* Down arrow */

	/* Bind custom line-oriented functions */
	bind_func_in_insert_maps("\\C-a", jot_beginning_of_line);
	bind_func_in_insert_maps("\\C-e", jot_end_of_line);
	bind_func_in_insert_maps("\\C-k", jot_kill_line);
	/* Prevent the terminal from handling this */
	disable_ctrl_u_kill_line();
	bind_func_in_insert_maps("\\C-u", jot_kill_backward_line);

	/*
	 * Bind Home and End keys to multiline-aware functions
	 */

	/* Home key */
	bind_func_in_insert_maps("\\e[1~", jot_beginning_of_line);
	bind_func_in_insert_maps("\\e[H", jot_beginning_of_line);
	bind_func_in_insert_maps("\\eOH", jot_beginning_of_line);

	/* End key */
	bind_func_in_insert_maps("\\e[4~", jot_end_of_line);
	bind_func_in_insert_maps("\\e[F", jot_end_of_line);
	bind_func_in_insert_maps("\\eOF", jot_end_of_line);

	/* Bind custom buffer-oriented functions */
	bind_func_in_insert_maps("\\M-<", rl_beg_of_line); /* M-< */
	bind_func_in_insert_maps("\\M->", rl_end_of_line);  /* M-> */

	bind_func_in_insert_maps("\\C-x\\C-e", jot_invoke_fullscreen_editor);

	/*
	 * Bind Vi-specific functions in Vi movement keymap
	 */
	bind_func_in_vi_movement_keymap("j", jot_move_cursor_down);
	bind_func_in_vi_movement_keymap("k", jot_move_cursor_up);
	bind_func_in_vi_movement_keymap("J", jot_vi_join_lines);
	bind_func_in_vi_movement_keymap("o", jot_vi_insert_line_below);
	bind_func_in_vi_movement_keymap("O", jot_vi_insert_line_above);
	bind_func_in_vi_movement_keymap("^", jot_beginning_of_line);
	bind_func_in_vi_movement_keymap("$", jot_end_of_line);
	bind_func_in_vi_movement_keymap("G", jot_vi_goto_line);
	bind_func_in_vi_movement_keymap("gg", jot_vi_goto_first_line);
	bind_func_in_vi_movement_keymap("dd", jot_vi_delete_current_line);
	bind_func_in_vi_movement_keymap("D", jot_vi_delete_to_end_of_line);
	bind_func_in_vi_movement_keymap("v", jot_invoke_fullscreen_editor);
	/* Bind '\r' in Vi movement mode to move cursor to next line */
	bind_func_in_vi_movement_keymap("\r", jot_move_to_first_nonblank_next_line);


	if (filename) {
		/* If -e is not specified, read the file contents */
		if (!opt_e) {
			file_contents = read_file_contents(filename);
			if (file_contents == NULL) {
				if (errno != ENOENT) {
					/* An error occured while reading the file */
					exit_status = EXIT_FAILURE;
					goto exit_program;
				}
				/* If file does not exist, proceed with empty buffer */
			}
			/* Set initial content in input buffer using startup hook */
		}
	}

	/* Set the startup hook to initialize the Readline buffer */
	rl_startup_hook = initialize_readline_buffer;

	/* Print the banner if it's not an empty string */
	if (banner && banner[0] != '\0') {
		printf("%s\n", banner);
		fflush(stdout);
	}
	/* Prompt for input */
	if ((input = readline("")) != NULL) {
		/* Write the input to file or stdout */
		if (filename) {
			/* Open file for writing (will create if it doesn't exist) */
			file_write = fopen(filename, "w");
			if (!file_write) {
				perror("fopen filename for writing");
				exit_status = EXIT_FAILURE;
				goto exit_program;
			}

			if (fputs(input, file_write) == EOF) {
				perror("fputs");
				exit_status = EXIT_FAILURE;
				goto exit_program;
			}
		} else {
			/* Output to original stdout */
			fputs(input, orig_stdout);
		}
		free(input); /* Free the input string allocated by readline */
		input = NULL;
	}
	/* If readline returns NULL (EOF or error), proceed to exit */

exit_program:

	/* Restore Readline's terminal settings */
	rl_deprep_terminal();

	/* Restore our saved terminal settings */
	restore_terminal_settings();

	/* Cleanup resources */
	free(input);
	free(file_contents);
	if (file_write)
		fclose(file_write);

	return exit_status;
}
