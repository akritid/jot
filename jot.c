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
#include <readline/readline.h>

#define DEFAULT_BANNER ""
#define PROGRAM_NAME "jot"

/* Global variables to save original terminal settings */
static struct termios original_termios; /* To store original settings */
static int termios_saved = 0;           /* Flag to check if settings are saved */

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

static int
beginning_of_line_multiline(int count, int key)
{
	/* Find the start of the current line */
	while (rl_point > 0 && rl_line_buffer[rl_point - 1] != '\n') {
		rl_point--;
	}
	rl_redisplay();
	return 0;
}

static int
end_of_line_multiline(int count, int key)
{
	int buffer_len = strlen(rl_line_buffer);
	/* Move rl_point to the end of the current line or buffer */
	while (rl_point < buffer_len && rl_line_buffer[rl_point] != '\n') {
		rl_point++;
	}
	rl_redisplay();
	return 0;
}

static int
kill_line_multiline(int count, int key)
{
	int buffer_len = strlen(rl_line_buffer);
	int start = rl_point;
	int end = rl_point;
	/* Find the end of the current line */
	while (end < buffer_len && rl_line_buffer[end] != '\n') {
		end++;
	}
	/* Kill text from start to end */
	rl_kill_text(start, end);
	rl_redisplay();
	return 0;
}

/* Function to restore terminal settings */
static void
restore_terminal_settings(void)
{
	if (termios_saved) {
		FILE *fp = rl_instream; /* Assuming rl_instream is set to tty_in */
		int fd = fileno(fp);
		if (fd == -1) {
			perror("fileno");
			return;
		}

		/* Restore the original terminal attributes */
		if (tcsetattr(fd, TCSANOW, &original_termios) == -1) {
			perror("tcsetattr");
			return;
		}
		termios_saved = 0; /* Reset the flag */
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
disable_ctrl_u_kill_line(FILE *fp)
{
	struct termios term;

	/* Get the terminal file descriptor */
	int fd = fileno(fp);
	if (fd == -1) {
		perror("fileno");
		return;
	}

	/* Get the current terminal attributes */
	if (tcgetattr(fd, &term) == -1) {
		perror("tcgetattr");
		return;
	}

	if (!termios_saved) {
		/* Save a copy of the original terminal settings */
		original_termios = term;
		termios_saved = 1;
	}

	/* Disable the line-kill character */
	term.c_cc[VKILL] = _POSIX_VDISABLE;

	/* Set the modified attributes immediately */
	if (tcsetattr(fd, TCSANOW, &term) == -1) {
		perror("tcsetattr");
		return;
	}
}

static int
kill_backward_line_multiline(int count, int key)
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
kill_whole_line_multiline(int count, int key)
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
custom_ctrl_d(int count, int key)
{
	if (count <= 0) {
		return 0;
	}
	if (rl_point < rl_end) {
		// Delete the character under the cursor
		return rl_delete(count, key);
	} else {
		return rl_named_function("accept-line")(count, key);
	}
}

/* Function to move the cursor up 'count' lines */
static int
move_cursor_up(int count, int key)
{
	while (count-- > 0) {
		int current_pos = rl_point;
		int line_start = current_pos;
		int line_col = 0;

		/* Find the start of the current line */
		while (line_start > 0 && rl_line_buffer[line_start - 1] != '\n') {
			line_start--;
			line_col++;
		}

		/* If we're at the first line, do nothing */
		if (line_start == 0) {
			rl_ding();
			break;
		}

		/* Find the end of the previous line */
		int prev_line_end = line_start - 1;

		/* Find the start of the previous line */
		int prev_line_start = prev_line_end;
		while (prev_line_start > 0 && rl_line_buffer[prev_line_start - 1] != '\n') {
			prev_line_start--;
		}

		/* Compute the column position in the previous line */
		int prev_line_len = prev_line_end - prev_line_start;
		int new_col = (line_col < prev_line_len) ? line_col : prev_line_len;

		/* Set the new cursor position */
		rl_point = prev_line_start + new_col;
	}
	rl_redisplay();
	return 0;
}

/* Function to move the cursor down 'count' lines */
static int
move_cursor_down(int count, int key)
{
	while (count-- > 0) {
		int buffer_len = strlen(rl_line_buffer);
		int current_pos = rl_point;
		int line_start = current_pos;
		int line_col = 0;

		/* Find the start of the current line */
		while (line_start > 0 && rl_line_buffer[line_start - 1] != '\n') {
			line_start--;
			line_col++;
		}

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

		/* Compute the column position in the next line */
		int next_line_len = next_line_end - next_line_start;
		int new_col = (line_col < next_line_len) ? line_col : next_line_len;

		/* Set the new cursor position */
		rl_point = next_line_start + new_col;
	}
	rl_redisplay();
	return 0;
}

/* Custom function to insert a newline character */
static int
insert_newline(int count, int key)
{
	/* Insert a newline character into the input buffer */
	rl_insert_text("\n");
	/* Redisplay the input line with the new content */
	rl_redisplay();
	return 0; /* Return 0 to indicate the key has been handled */
}

/* Vi command to delete 'count' lines ('dd') */
static int
vi_delete_current_line(int count, int key)
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
vi_delete_to_end_of_line(int count, int key)
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
vi_join_lines(int count, int key)
{
	while (count-- > 0) {
		int buffer_len = strlen(rl_line_buffer);
		int pos = rl_point;

		/* Find the end of the current line */
		while (pos < buffer_len && rl_line_buffer[pos] != '\n') {
			pos++;
		}

		/* If we're at the end of the buffer, nothing to do */
		if (pos >= buffer_len) {
			rl_ding(); /* Beep to indicate no action */
			break;
		}

		/* Delete the newline character */
		rl_delete_text(pos, pos + 1);
		buffer_len--;

		/* Now, possibly insert a space if needed */
		char before_char = (pos > 0) ? rl_line_buffer[pos - 1] : '\0';
		char after_char = (pos < buffer_len) ? rl_line_buffer[pos] : '\0';

		int need_space = 0;
		if (!isspace(before_char) && !isspace(after_char) &&
			before_char != '\0' && after_char != '\0' &&
			!ispunct(before_char) && !ispunct(after_char)) {
			need_space = 1;
		}

		if (need_space) {
			rl_point = pos;
			rl_insert_text(" ");
			buffer_len++;
			pos++;
		}

		/* Remove leading whitespace on the next line */
		while (pos < buffer_len && isspace(rl_line_buffer[pos])) {
			rl_delete_text(pos, pos + 1);
			buffer_len--;
		}

		/* Adjust cursor position */
		rl_point = pos;
	}
	rl_redisplay();
	return 0;
}

/* Vi command to go to line 'count' or end ('G') */
static int
vi_goto_line(int count, int key)
{
	int buffer_len = strlen(rl_line_buffer);
	int pos = 0;
	int current_line = 1;
	int target_line = (count > 0) ? count : INT_MAX;

	/* Move to the desired line number */
	while (pos < buffer_len && current_line < target_line) {
		if (rl_line_buffer[pos++] == '\n') {
			current_line++;
		}
	}

	rl_point = pos;
	rl_redisplay();
	return 0;
}

/* Vi command to go to first line ('gg') */
static int
vi_goto_first_line(int count, int key)
{
	int target_line = (count > 0) ? count : 1;
	return vi_goto_line(target_line, key);
}

/* Vi command to insert a line below current line and enter insert mode ('o') */
static int
vi_insert_line_below(int count, int key)
{
	int buffer_len = strlen(rl_line_buffer);
	int pos = rl_point;

	/* Find the end of the current line */
	while (pos < buffer_len && rl_line_buffer[pos] != '\n') {
		pos++;
	}

	/* Move after the newline character */
	if (pos < buffer_len && rl_line_buffer[pos] == '\n') {
		pos++;
	}

	/* Insert a newline character */
	rl_point = pos;
	rl_insert_text("\n");

	/* Move cursor to the beginning of the new line */
	rl_point = pos;

	/* Switch to Vi insert mode */
	rl_vi_insertion_mode(1, 0);

	rl_redisplay();
	return 0;
}

/* Vi command to insert a line above current line and enter insert mode ('O') */
static int
vi_insert_line_above(int count, int key)
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

/* Global variable to hold the file contents */
char *file_contents = NULL;


/* Function to read file contents into a dynamically allocated buffer */
char *
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


/* Function to initialize the Readline buffer with file contents */
int
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
	char *filename = NULL;
	int opt;
	char *input = NULL;
	FILE *tty_in = NULL, *tty_out = NULL;
	FILE *file_write = NULL;
	int exit_status = EXIT_SUCCESS;
	char *banner = DEFAULT_BANNER;

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
		filename = argv[optind];
	} else {
		filename = NULL;
	}

	/* For conditional processing of inputrc */
	rl_readline_name = PROGRAM_NAME;

	/*
	 * Open /dev/tty for input and output
	 */
	tty_in = fopen("/dev/tty", "r");
	if (!tty_in) {
		perror("fopen /dev/tty for reading");
		exit_status = EXIT_FAILURE;
		goto exit_program;
	}

	tty_out = fopen("/dev/tty", "w");
	if (!tty_out) {
		perror("fopen /dev/tty for writing");
		exit_status = EXIT_FAILURE;
		goto exit_program;
	}

	rl_instream = tty_in;
	rl_outstream = tty_out;

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
	rl_add_defun("move-cursor-up", move_cursor_up, -1);
	rl_add_defun("move-cursor-down", move_cursor_down, -1);
	rl_add_defun("beginning-of-line-multiline", beginning_of_line_multiline, -1);
	rl_add_defun("end-of-line-multiline", end_of_line_multiline, -1);
	rl_add_defun("kill-line-multiline", kill_line_multiline, -1);
	rl_add_defun("kill-backward-line-multiline", kill_backward_line_multiline, -1);
	rl_add_defun("kill-whole-line-multiline", kill_whole_line_multiline, -1);
	rl_add_defun("custom-ctrl-d", custom_ctrl_d, -1);

	/*
	 * Add Vi-specific functions
	 */
	rl_add_defun("vi-join-lines", vi_join_lines, -1);
	rl_add_defun("vi-insert-line-below", vi_insert_line_below, -1);
	rl_add_defun("vi-insert-line-above", vi_insert_line_above, -1);
	rl_add_defun("vi-goto-line", vi_goto_line, -1);
	rl_add_defun("vi-goto-first-line", vi_goto_first_line, -1);
	rl_add_defun("vi-delete-current-line", vi_delete_current_line, -1);
	rl_add_defun("vi-delete-to-end-of-line", vi_delete_to_end_of_line, -1);

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
	bind_func_in_all_keymaps("\r", insert_newline);

	/* Bind Ctrl+n to accept the line */
	bind_func_in_all_keymaps("\\C-n", rl_named_function("accept-line"));

	/* Bind the custom Ctrl-D function */
	bind_func_in_all_keymaps("\\C-d", custom_ctrl_d);

	/* Bind Up/Down arrows to custom cursor movement functions */
	bind_func_in_all_keymaps("\\e[A", move_cursor_up);   /* Up arrow */
	bind_func_in_all_keymaps("\\e[B", move_cursor_down); /* Down arrow */

	/* Bind custom line-oriented functions */
	rl_bind_keyseq("\\C-a", beginning_of_line_multiline);
	rl_bind_keyseq("\\C-e", end_of_line_multiline);
	rl_bind_keyseq("\\C-k", kill_line_multiline);
	/* Prevent the terminal from handling this */
	disable_ctrl_u_kill_line(tty_in);
	rl_bind_keyseq("\\C-u", kill_backward_line_multiline);

	/*
	 * Bind Home and End keys to multiline-aware functions
	 */

	/* Home key */
	rl_bind_keyseq("\\e[1~", beginning_of_line_multiline);
	rl_bind_keyseq("\\e[H", beginning_of_line_multiline);
	rl_bind_keyseq("\\eOH", beginning_of_line_multiline);

	/* End key */
	rl_bind_keyseq("\\e[4~", end_of_line_multiline);
	rl_bind_keyseq("\\e[F", end_of_line_multiline);
	rl_bind_keyseq("\\eOF", end_of_line_multiline);

	/* Bind custom buffer-oriented functions */
	rl_bind_keyseq("\\M-<", rl_beg_of_line); /* M-< */
	rl_bind_keyseq("\\M->", rl_end_of_line);  /* M-> */

	/*
	 * Bind Vi-specific functions in Vi movement keymap
	 */
	bind_func_in_vi_movement_keymap("j", move_cursor_down);
	bind_func_in_vi_movement_keymap("k", move_cursor_up);
	bind_func_in_vi_movement_keymap("J", vi_join_lines);
	bind_func_in_vi_movement_keymap("o", vi_insert_line_below);
	bind_func_in_vi_movement_keymap("O", vi_insert_line_above);
	bind_func_in_vi_movement_keymap("^", beginning_of_line_multiline);
	bind_func_in_vi_movement_keymap("$", end_of_line_multiline);
	bind_func_in_vi_movement_keymap("G", vi_goto_line);
	bind_func_in_vi_movement_keymap("gg", vi_goto_first_line);
	bind_func_in_vi_movement_keymap("dd", vi_delete_current_line);
	bind_func_in_vi_movement_keymap("D", vi_delete_to_end_of_line);

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

		/* Open file for writing (will create if it doesn't exist) */
		file_write = fopen(filename, "w");
		if (!file_write) {
			perror("fopen filename for writing");
			exit_status = EXIT_FAILURE;
			goto exit_program;
		}
	}

	/* Set the startup hook to initialize the Readline buffer */
	rl_startup_hook = initialize_readline_buffer;

	/* Print the banner if it's not an empty string */
	if (banner && banner[0] != '\0') {
		fprintf(tty_out, "%s\n", banner);
		fflush(tty_out);
	}
	/* Prompt for input */
	if ((input = readline("")) != NULL) {
		/* Write the input to file or stdout */
		if (filename) {
			if (fputs(input, file_write) == EOF) {
				perror("fputs");
				exit_status = EXIT_FAILURE;
				goto exit_program;
			}
		} else {
			/* Output to stdout */
			fputs(input, stdout);
		}
		free(input); /* Free the input string allocated by readline */
		input = NULL;
	}
	/* If readline returns NULL (EOF or error), proceed to exit */

exit_program:
	restore_terminal_settings();

	/* Cleanup resources */
	free(input);
	free(file_contents);
	if (tty_in)
		fclose(tty_in);
	if (tty_out)
		fclose(tty_out);
	if (file_write)
		fclose(file_write);

	return exit_status;
}
