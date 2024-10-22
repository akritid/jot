AC_PREREQ([2.69])
AC_INIT([jot], [0.1], [jot-bugs@akritidis.org])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_SRCDIR([jot.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
# First, check for the readline library.
AC_CHECK_LIB([readline], [readline], [READLINE_LIBS="-lreadline"], [AC_MSG_ERROR([Cannot find the readline library])])

# Check for terminal capability library required by readline.
AC_SEARCH_LIBS([tgetent], [ncurses curses termcap], [READLINE_LIBS="$READLINE_LIBS $ac_cv_search_tgetent"], [AC_MSG_ERROR([Cannot find the tgetent function in ncurses, curses, or termcap libraries])])

# Checks for header files.
AC_CHECK_HEADERS([readline/readline.h])

# Substitute the READLINE_LIBS variable so it can be used in Makefile.am.
AC_SUBST([READLINE_LIBS])

# Generate the Makefile.
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
