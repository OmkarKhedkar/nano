/* $Id$ */
/**************************************************************************
 *   winio.c                                                              *
 *                                                                        *
 *   Copyright (C) 1999-2005 Chris Allegretta                             *
 *   This program is free software; you can redistribute it and/or modify *
 *   it under the terms of the GNU General Public License as published by *
 *   the Free Software Foundation; either version 2, or (at your option)  *
 *   any later version.                                                   *
 *                                                                        *
 *   This program is distributed in the hope that it will be useful, but  *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU    *
 *   General Public License for more details.                             *
 *                                                                        *
 *   You should have received a copy of the GNU General Public License    *
 *   along with this program; if not, write to the Free Software          *
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA            *
 *   02110-1301, USA.                                                     *
 *                                                                        *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include "proto.h"

static int *key_buffer = NULL;
				/* The default keystroke buffer,
				 * containing all the keystrokes we have
				 * at a given point. */
static size_t key_buffer_len = 0;
				/* The length of the default keystroke
				 * buffer. */
static int statusblank = 0;
				/* The number of keystrokes left after
				 * we call statusbar(), before we
				 * actually blank the statusbar. */
static size_t statusbar_x = (size_t)-1;
				/* The cursor position in answer. */
static bool disable_cursorpos = FALSE;
				/* Should we temporarily disable
				 * constant cursor position display? */
static bool resetstatuspos = FALSE;
				/* Should we reset the cursor position
				 * at the statusbar prompt? */

/* Control character compatibility:
 *
 * - NANO_BACKSPACE_KEY is Ctrl-H, which is Backspace under ASCII, ANSI,
 *   VT100, and VT220.
 * - NANO_TAB_KEY is Ctrl-I, which is Tab under ASCII, ANSI, VT100,
 *   VT220, and VT320.
 * - NANO_ENTER_KEY is Ctrl-M, which is Enter under ASCII, ANSI, VT100,
 *   VT220, and VT320.
 * - NANO_XON_KEY is Ctrl-Q, which is XON under ASCII, ANSI, VT100,
 *   VT220, and VT320.
 * - NANO_XOFF_KEY is Ctrl-S, which is XOFF under ASCII, ANSI, VT100,
 *   VT220, and VT320.
 * - NANO_CONTROL_8 is Ctrl-8 (Ctrl-?), which is Delete under ASCII,
 *   ANSI, VT100, and VT220, and which is Backspace under VT320.
 *
 * Note: VT220 and VT320 also generate Esc [ 3 ~ for Delete.  By
 * default, xterm assumes it's running on a VT320 and generates Ctrl-8
 * (Ctrl-?) for Backspace and Esc [ 3 ~ for Delete.  This causes
 * problems for VT100-derived terminals such as the FreeBSD console,
 * which expect Ctrl-H for Backspace and Ctrl-8 (Ctrl-?) for Delete, and
 * on which the VT320 sequences are translated by the keypad to KEY_DC
 * and [nothing].  We work around this conflict via the REBIND_DELETE
 * flag: if it's not set, we assume VT320 compatibility, and if it is,
 * we assume VT100 compatibility.  Thanks to Lee Nelson and Wouter van
 * Hemel for helping work this conflict out.
 *
 * Escape sequence compatibility:
 *
 * We support escape sequences for ANSI, VT100, VT220, VT320, the Linux
 * console, the FreeBSD console, the Mach console (a.k.a. the Hurd
 * console), xterm, rxvt, and Eterm.  Among these, there are several
 * conflicts and omissions, outlined as follows:
 *
 * - Tab on ANSI == PageUp on FreeBSD console; the former is omitted.
 *   (Ctrl-I is also Tab on ANSI, which we already support.)
 * - PageDown on FreeBSD console == Center (5) on numeric keypad with
 *   NumLock off on Linux console; the latter is omitted.  (The editing
 *   keypad key is more important to have working than the numeric
 *   keypad key, because the latter has no value when NumLock is off.)
 * - F1 on FreeBSD console == the mouse key on xterm/rxvt/Eterm; the
 *   latter is omitted.  (Mouse input will only work properly if the
 *   extended keypad value KEY_MOUSE is generated on mouse events
 *   instead of the escape sequence.)
 * - F9 on FreeBSD console == PageDown on Mach console; the former is
 *   omitted.  (The editing keypad is more important to have working
 *   than the function keys, because the functions of the former are not
 *   arbitrary and the functions of the latter are.)
 * - F10 on FreeBSD console == PageUp on Mach console; the former is
 *   omitted.  (Same as above.)
 * - F13 on FreeBSD console == End on Mach console; the former is
 *   omitted.  (Same as above.)
 * - F15 on FreeBSD console == Shift-Up on rxvt/Eterm; the former is
 *   omitted.  (The arrow keys, with or without modifiers, are more
 *   important to have working than the function keys, because the
 *   functions of the former are not arbitrary and the functions of the
 *   latter are.)
 * - F16 on FreeBSD console == Shift-Down on rxvt/Eterm; the former is
 *   omitted.  (Same as above.)
 *
 * Note that Center (5) on the numeric keypad with NumLock off can also
 * be the Begin key. */

#ifndef NANO_SMALL
/* Reset all the input routines that rely on character sequences. */
void reset_kbinput(void)
{
    parse_kbinput(NULL, NULL, NULL, TRUE);
    get_byte_kbinput(0, TRUE);
    get_unicode_kbinput(0, TRUE);
}
#endif

/* Read in a sequence of keystrokes from win and save them in the
 * default keystroke buffer.  This should only be called when the
 * default keystroke buffer is empty. */
void get_key_buffer(WINDOW *win)
{
    int input;

    /* If the keystroke buffer isn't empty, get out. */
    if (key_buffer != NULL)
	return;

    /* Read in the first character using blocking input. */
#ifndef NANO_SMALL
    allow_pending_sigwinch(TRUE);
#endif

    /* Just before reading in the first character, display any pending
     * screen updates. */
    doupdate();

    while ((input = wgetch(win)) == ERR) {
	/* If errno is EIO, it means that the input source that we were
	 * using is gone, so die gracefully. */
	if (errno == EIO)
	    handle_hupterm(0);
    }


#ifndef NANO_SMALL
    allow_pending_sigwinch(FALSE);
#endif

    /* Increment the length of the keystroke buffer, save the value of
     * the keystroke in key, and set key_code to TRUE if the keystroke
     * is an extended keypad value or FALSE if it isn't. */
    key_buffer_len++;
    key_buffer = (int *)nmalloc(sizeof(int));
    key_buffer[0] = input;

    /* Read in the remaining characters using non-blocking input. */
    nodelay(win, TRUE);

    while (TRUE) {
#ifndef NANO_SMALL
	allow_pending_sigwinch(TRUE);
#endif

	input = wgetch(win);

	/* If there aren't any more characters, stop reading. */
	if (input == ERR)
	    break;

	/* Otherwise, increment the length of the keystroke buffer, save
	 * the value of the keystroke in key, and set key_code to TRUE
	 * if the keystroke is an extended keypad value or FALSE if it
	 * isn't. */
	key_buffer_len++;
	key_buffer = (int *)nrealloc(key_buffer, key_buffer_len *
		sizeof(int));
	key_buffer[key_buffer_len - 1] = input;

#ifndef NANO_SMALL
	allow_pending_sigwinch(FALSE);
#endif
    }

    /* Switch back to non-blocking input. */
    nodelay(win, FALSE);

#ifdef DEBUG
    fprintf(stderr, "get_key_buffer(): key_buffer_len = %lu\n", (unsigned long)key_buffer_len);
#endif
}

/* Return the length of the default keystroke buffer. */
size_t get_key_buffer_len(void)
{
    return key_buffer_len;
}

/* Add the contents of the keystroke buffer input to the default
 * keystroke buffer. */
void unget_input(int *input, size_t input_len)
{
#ifndef NANO_SMALL
    allow_pending_sigwinch(TRUE);
    allow_pending_sigwinch(FALSE);
#endif

    /* If input is empty, get out. */
    if (input_len == 0)
	return;

    /* If adding input would put the default keystroke buffer beyond
     * maximum capacity, only add enough of input to put it at maximum
     * capacity. */
    if (key_buffer_len + input_len < key_buffer_len)
	input_len = (size_t)-1 - key_buffer_len;

    /* Add the length of input to the length of the default keystroke
     * buffer, and reallocate the default keystroke buffer so that it
     * has enough room for input. */
    key_buffer_len += input_len;
    key_buffer = (int *)nrealloc(key_buffer, key_buffer_len *
	sizeof(int));

    /* If the default keystroke buffer wasn't empty before, move its
     * beginning forward far enough so that we can add input to its
     * beginning. */
    if (key_buffer_len > input_len)
	memmove(key_buffer + input_len, key_buffer,
		(key_buffer_len - input_len) * sizeof(int));

    /* Copy input to the beginning of the default keystroke buffer. */
    memcpy(key_buffer, input, input_len * sizeof(int));
}

/* Put back the character stored in kbinput, putting it in byte range
 * beforehand.  If meta_key is TRUE, put back the Escape character after
 * putting back kbinput.  If func_key is TRUE, put back the function key
 * (a value outside byte range) without putting it in byte range. */
void unget_kbinput(int kbinput, bool meta_key, bool func_key)
{
    if (!func_key)
	kbinput = (char)kbinput;

    unget_input(&kbinput, 1);

    if (meta_key) {
	kbinput = NANO_CONTROL_3;
	unget_input(&kbinput, 1);
    }
}

/* Try to read input_len characters from the default keystroke buffer.
 * If the default keystroke buffer is empty and win isn't NULL, try to
 * read in more characters from win and add them to the default
 * keystroke buffer before doing anything else.  If the default
 * keystroke buffer is empty and win is NULL, return NULL. */
int *get_input(WINDOW *win, size_t input_len)
{
    int *input;

#ifndef NANO_SMALL
    allow_pending_sigwinch(TRUE);
    allow_pending_sigwinch(FALSE);
#endif

    if (key_buffer_len == 0) {
	if (win != NULL)
	    get_key_buffer(win);

	if (key_buffer_len == 0)
	    return NULL;
    }

    /* If input_len is greater than the length of the default keystroke
     * buffer, only read the number of characters in the default
     * keystroke buffer. */
    if (input_len > key_buffer_len)
	input_len = key_buffer_len;

    /* Subtract input_len from the length of the default keystroke
     * buffer, and allocate the keystroke buffer input so that it
     * has enough room for input_len keystrokes. */
    key_buffer_len -= input_len;
    input = (int *)nmalloc(input_len * sizeof(int));

    /* Copy input_len characters from the beginning of the default
     * keystroke buffer into input. */
    memcpy(input, key_buffer, input_len * sizeof(int));

    /* If the default keystroke buffer is empty, mark it as such. */
    if (key_buffer_len == 0) {
	free(key_buffer);
	key_buffer = NULL;
    /* If the default keystroke buffer isn't empty, move its
     * beginning forward far enough so that the keystrokes in input are
     * no longer at its beginning. */
    } else {
	memmove(key_buffer, key_buffer + input_len, key_buffer_len *
		sizeof(int));
	key_buffer = (int *)nrealloc(key_buffer, key_buffer_len *
		sizeof(int));
    }

    return input;
}

/* Read in a single character.  If it's ignored, swallow it and go on.
 * Otherwise, try to translate it from ASCII, meta key sequences, escape
 * sequences, and/or extended keypad values.  Set meta_key to TRUE when
 * we get a meta key sequence, and set func_key to TRUE when we get an
 * extended keypad value.  Supported extended keypad values consist of
 * [arrow key], Ctrl-[arrow key], Shift-[arrow key], Enter, Backspace,
 * the editing keypad (Insert, Delete, Home, End, PageUp, and PageDown),
 * the function keypad (F1-F16), and the numeric keypad with NumLock
 * off.  Assume nodelay(win) is FALSE. */
int get_kbinput(WINDOW *win, bool *meta_key, bool *func_key)
{
    int kbinput;

    /* Read in a character and interpret it.  Continue doing this until
     * we get a recognized value or sequence. */
    while ((kbinput = parse_kbinput(win, meta_key, func_key
#ifndef NANO_SMALL
		, FALSE
#endif
		)) == ERR);

    return kbinput;
}

/* Translate ASCII characters, extended keypad values, and escape
 * sequences into their corresponding key values.  Set meta_key to TRUE
 * when we get a meta key sequence, and set func_key to TRUE when we get
 * a function key.  Assume nodelay(win) is FALSE. */
int parse_kbinput(WINDOW *win, bool *meta_key, bool *func_key
#ifndef NANO_SMALL
	, bool reset
#endif
	)

{
    static int escapes = 0, byte_digits = 0;
    int *kbinput, retval = ERR;

#ifndef NANO_SMALL
    if (reset) {
	escapes = 0;
	byte_digits = 0;
	return ERR;
    }
#endif

    *meta_key = FALSE;
    *func_key = FALSE;

    /* Read in a character. */
    while ((kbinput = get_input(win, 1)) == NULL);

    switch (*kbinput) {
	case ERR:
	    break;
	case NANO_CONTROL_3:
	    /* Increment the escape counter. */
	    escapes++;
	    switch (escapes) {
		case 1:
		    /* One escape: wait for more input. */
		case 2:
		    /* Two escapes: wait for more input. */
		    break;
		default:
		    /* More than two escapes: reset the escape counter
		     * and wait for more input. */
		    escapes = 0;
	    }
	    break;
#if !defined(NANO_SMALL) && defined(KEY_RESIZE)
	/* Since we don't change the default SIGWINCH handler when
	 * NANO_SMALL is defined, KEY_RESIZE is never generated.  Also,
	 * Slang and SunOS 5.7-5.9 don't support KEY_RESIZE. */
	case KEY_RESIZE:
	    break;
#endif
#ifdef PDCURSES
	case KEY_SHIFT_L:
	case KEY_SHIFT_R:
	case KEY_CONTROL_L:
	case KEY_CONTROL_R:
	case KEY_ALT_L:
	case KEY_ALT_R:
	    break;
#endif
	default:
	    switch (escapes) {
		case 0:
		    switch (*kbinput) {
			case NANO_CONTROL_8:
			    retval = ISSET(REBIND_DELETE) ?
				NANO_DELETE_KEY : NANO_BACKSPACE_KEY;
			    break;
			case KEY_DOWN:
			    retval = NANO_NEXTLINE_KEY;
			    break;
			case KEY_UP:
			    retval = NANO_PREVLINE_KEY;
			    break;
			case KEY_LEFT:
			    retval = NANO_BACK_KEY;
			    break;
			case KEY_RIGHT:
			    retval = NANO_FORWARD_KEY;
			    break;
#ifdef KEY_HOME
			/* HP-UX 10 and 11 don't support KEY_HOME. */
			case KEY_HOME:
			    retval = NANO_HOME_KEY;
			    break;
#endif
			case KEY_BACKSPACE:
			    retval = NANO_BACKSPACE_KEY;
			    break;
			case KEY_DC:
			    retval = ISSET(REBIND_DELETE) ?
				NANO_BACKSPACE_KEY : NANO_DELETE_KEY;
			    break;
			case KEY_IC:
			    retval = NANO_INSERTFILE_KEY;
			    break;
			case KEY_NPAGE:
			    retval = NANO_NEXTPAGE_KEY;
			    break;
			case KEY_PPAGE:
			    retval = NANO_PREVPAGE_KEY;
			    break;
			case KEY_ENTER:
			    retval = NANO_ENTER_KEY;
			    break;
			case KEY_A1:	/* Home (7) on numeric keypad
					 * with NumLock off. */
			    retval = NANO_HOME_KEY;
			    break;
			case KEY_A3:	/* PageUp (9) on numeric keypad
					 * with NumLock off. */
			    retval = NANO_PREVPAGE_KEY;
			    break;
			case KEY_B2:	/* Center (5) on numeric keypad
					 * with NumLock off. */
			    break;
			case KEY_C1:	/* End (1) on numeric keypad
					 * with NumLock off. */
			    retval = NANO_END_KEY;
			    break;
			case KEY_C3:	/* PageDown (4) on numeric
					 * keypad with NumLock off. */
			    retval = NANO_NEXTPAGE_KEY;
			    break;
#ifdef KEY_BEG
			/* Slang doesn't support KEY_BEG. */
			case KEY_BEG:	/* Center (5) on numeric keypad
					 * with NumLock off. */
			    break;
#endif
#ifdef KEY_END
			/* HP-UX 10 and 11 don't support KEY_END. */
			case KEY_END:
			    retval = NANO_END_KEY;
			    break;
#endif
#ifdef KEY_SUSPEND
			/* Slang doesn't support KEY_SUSPEND. */
			case KEY_SUSPEND:
			    retval = NANO_SUSPEND_KEY;
			    break;
#endif
#ifdef KEY_SLEFT
			/* Slang doesn't support KEY_SLEFT. */
			case KEY_SLEFT:
			    retval = NANO_BACK_KEY;
			    break;
#endif
#ifdef KEY_SRIGHT
			/* Slang doesn't support KEY_SRIGHT. */
			case KEY_SRIGHT:
			    retval = NANO_FORWARD_KEY;
			    break;
#endif
			default:
			    retval = *kbinput;
			    break;
		    }
		    break;
		case 1:
		    /* One escape followed by a non-escape: escape
		     * sequence mode.  Reset the escape counter.  If
		     * there aren't any other keys waiting, we have a
		     * meta key sequence, so set meta_key to TRUE and
		     * save the lowercase version of the non-escape
		     * character as the result.  If there are other keys
		     * waiting, we have a true escape sequence, so
		     * interpret it. */
		    escapes = 0;
		    if (get_key_buffer_len() == 0) {
			*meta_key = TRUE;
			retval = tolower(*kbinput);
		    } else {
			int *seq;
			size_t seq_len;
			bool ignore_seq;

			/* Put back the non-escape character, get the
			 * complete escape sequence, translate the
			 * sequence into its corresponding key value,
			 * and save that as the result. */
			unget_input(kbinput, 1);
			seq_len = get_key_buffer_len();
			seq = get_input(NULL, seq_len);
			retval = get_escape_seq_kbinput(seq, seq_len,
				&ignore_seq);

			/* If the escape sequence is unrecognized and
			 * not ignored, put back all of its characters
			 * except for the initial escape. */
			if (retval == ERR && !ignore_seq)
			    unget_input(seq, seq_len);

			free(seq);
		    }
		    break;
		case 2:
		    /* Two escapes followed by one or more decimal
		     * digits: byte sequence mode.  If the byte
		     * sequence's range is limited to 2XX (the first
		     * digit is in the '0' to '2' range and it's the
		     * first digit, or it's in the '0' to '9' range and
		     * it's not the first digit), increment the byte
		     * sequence counter and interpret the digit.  If the
		     * byte sequence's range is not limited to 2XX, fall
		     * through. */
		    if (('0' <= *kbinput && *kbinput <= '6' &&
			byte_digits == 0) || ('0' <= *kbinput &&
			*kbinput <= '9' && byte_digits > 0)) {
			int byte;

			byte_digits++;
			byte = get_byte_kbinput(*kbinput
#ifndef NANO_SMALL
				, FALSE
#endif
				);

			if (byte != ERR) {
			    char *byte_mb;
			    int byte_mb_len, *seq, i;

			    /* If we've read in a complete byte
			     * sequence, reset the byte sequence counter
			     * and the escape counter, and put back the
			     * corresponding byte value. */
			    byte_digits = 0;
			    escapes = 0;

			    /* Put back the multibyte equivalent of the
			     * byte value. */
			    byte_mb = make_mbchar((long)byte,
				&byte_mb_len);

			    seq = (int *)nmalloc(byte_mb_len *
				sizeof(int));

			    for (i = 0; i < byte_mb_len; i++)
				seq[i] = (unsigned char)byte_mb[i];

			    unget_input(seq, byte_mb_len);

			    free(byte_mb);
			    free(seq);
			}
		    } else {
			/* Reset the escape counter. */
			escapes = 0;
			if (byte_digits == 0)
			    /* Two escapes followed by a non-decimal
			     * digit or a decimal digit that would
			     * create a byte sequence greater than 2XX,
			     * and we're not in the middle of a byte
			     * sequence: control character sequence
			     * mode.  Interpret the control sequence and
			     * save the corresponding control character
			     * as the result. */
			    retval = get_control_kbinput(*kbinput);
			else {
			    /* If we're in the middle of a byte
			     * sequence, reset the byte sequence counter
			     * and save the character we got as the
			     * result. */
			    byte_digits = 0;
			    retval = *kbinput;
			}
		    }
		    break;
	    }
    }

    /* If we have a result and it's an extended keypad value (i.e, a
     * value outside of byte range), set func_key to TRUE. */
    if (retval != ERR)
	*func_key = !is_byte(retval);

#ifdef DEBUG
    fprintf(stderr, "parse_kbinput(): kbinput = %d, meta_key = %d, func_key = %d, escapes = %d, byte_digits = %d, retval = %d\n", *kbinput, (int)*meta_key, (int)*func_key, escapes, byte_digits, retval);
#endif

    /* Return the result. */
    return retval;
}

/* Translate escape sequences, most of which correspond to extended
 * keypad values, into their corresponding key values.  These sequences
 * are generated when the keypad doesn't support the needed keys.  If
 * the escape sequence is recognized but we want to ignore it, return
 * ERR and set ignore_seq to TRUE; if it's unrecognized, return ERR and
 * set ignore_seq to FALSE.  Assume that Escape has already been read
 * in. */
int get_escape_seq_kbinput(const int *seq, size_t seq_len, bool
	*ignore_seq)
{
    int retval = ERR;

    *ignore_seq = FALSE;

    if (seq_len > 1) {
	switch (seq[0]) {
	    case 'O':
		switch (seq[1]) {
		    case '2':
			if (seq_len >= 3) {
			    switch (seq[2]) {
				case 'P': /* Esc O 2 P == F13 on
					   * xterm. */
				    retval = KEY_F(13);
				    break;
				case 'Q': /* Esc O 2 Q == F14 on
					   * xterm. */
				    retval = KEY_F(14);
				    break;
				case 'R': /* Esc O 2 R == F15 on
					   * xterm. */
				    retval = KEY_F(15);
				    break;
				case 'S': /* Esc O 2 S == F16 on
					   * xterm. */
				    retval = KEY_F(16);
				    break;
			    }
			}
			break;
		    case 'A': /* Esc O A == Up on VT100/VT320/xterm. */
		    case 'B': /* Esc O B == Down on
			       * VT100/VT320/xterm. */
		    case 'C': /* Esc O C == Right on
			       * VT100/VT320/xterm. */
		    case 'D': /* Esc O D == Left on
			       * VT100/VT320/xterm. */
			retval = get_escape_seq_abcd(seq[1]);
			break;
		    case 'E': /* Esc O E == Center (5) on numeric keypad
			       * with NumLock off on xterm. */
			*ignore_seq = TRUE;
			break;
		    case 'F': /* Esc O F == End on xterm. */
			retval = NANO_END_KEY;
			break;
		    case 'H': /* Esc O H == Home on xterm. */
			retval = NANO_HOME_KEY;
			break;
		    case 'M': /* Esc O M == Enter on numeric keypad with
			       * NumLock off on VT100/VT220/VT320/xterm/
			       * Eterm. */
			retval = NANO_ENTER_KEY;
			break;
		    case 'P': /* Esc O P == F1 on VT100/VT220/VT320/Mach
			       * console. */
			retval = KEY_F(1);
			break;
		    case 'Q': /* Esc O Q == F2 on VT100/VT220/VT320/Mach
			       * console. */
			retval = KEY_F(2);
			break;
		    case 'R': /* Esc O R == F3 on VT100/VT220/VT320/Mach
			       * console. */
			retval = KEY_F(3);
			break;
		    case 'S': /* Esc O S == F4 on VT100/VT220/VT320/Mach
			       * console. */
			retval = KEY_F(4);
			break;
		    case 'T': /* Esc O T == F5 on Mach console. */
			retval = KEY_F(5);
			break;
		    case 'U': /* Esc O U == F6 on Mach console. */
			retval = KEY_F(6);
			break;
		    case 'V': /* Esc O V == F7 on Mach console. */
			retval = KEY_F(7);
			break;
		    case 'W': /* Esc O W == F8 on Mach console. */
			retval = KEY_F(8);
			break;
		    case 'X': /* Esc O X == F9 on Mach console. */
			retval = KEY_F(9);
			break;
		    case 'Y': /* Esc O Y == F10 on Mach console. */
			retval = KEY_F(10);
			break;
		    case 'a': /* Esc O a == Ctrl-Up on rxvt. */
		    case 'b': /* Esc O b == Ctrl-Down on rxvt. */
		    case 'c': /* Esc O c == Ctrl-Right on rxvt. */
		    case 'd': /* Esc O d == Ctrl-Left on rxvt. */
			retval = get_escape_seq_abcd(seq[1]);
			break;
		    case 'j': /* Esc O j == '*' on numeric keypad with
			       * NumLock off on VT100/VT220/VT320/xterm/
			       * rxvt. */
			retval = '*';
			break;
		    case 'k': /* Esc O k == '+' on numeric keypad with
			       * NumLock off on VT100/VT220/VT320/xterm/
			       * rxvt. */
			retval = '+';
			break;
		    case 'l': /* Esc O l == ',' on numeric keypad with
			       * NumLock off on VT100/VT220/VT320/xterm/
			       * rxvt. */
			retval = ',';
			break;
		    case 'm': /* Esc O m == '-' on numeric keypad with
			       * NumLock off on VT100/VT220/VT320/xterm/
			       * rxvt. */
			retval = '-';
			break;
		    case 'n': /* Esc O n == Delete (.) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * xterm/rxvt. */
			retval = NANO_DELETE_KEY;
			break;
		    case 'o': /* Esc O o == '/' on numeric keypad with
			       * NumLock off on VT100/VT220/VT320/xterm/
			       * rxvt. */
			retval = '/';
			break;
		    case 'p': /* Esc O p == Insert (0) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * rxvt. */
			retval = NANO_INSERTFILE_KEY;
			break;
		    case 'q': /* Esc O q == End (1) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * rxvt. */
			retval = NANO_END_KEY;
			break;
		    case 'r': /* Esc O r == Down (2) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * rxvt. */
			retval = NANO_NEXTLINE_KEY;
			break;
		    case 's': /* Esc O s == PageDown (3) on numeric
			       * keypad with NumLock off on VT100/VT220/
			       * VT320/rxvt. */
			retval = NANO_NEXTPAGE_KEY;
			break;
		    case 't': /* Esc O t == Left (4) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * rxvt. */
			retval = NANO_BACK_KEY;
			break;
		    case 'u': /* Esc O u == Center (5) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * rxvt/Eterm. */
			*ignore_seq = TRUE;
			break;
		    case 'v': /* Esc O v == Right (6) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * rxvt. */
			retval = NANO_FORWARD_KEY;
			break;
		    case 'w': /* Esc O w == Home (7) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * rxvt. */
			retval = NANO_HOME_KEY;
			break;
		    case 'x': /* Esc O x == Up (8) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * rxvt. */
			retval = NANO_PREVLINE_KEY;
			break;
		    case 'y': /* Esc O y == PageUp (9) on numeric keypad
			       * with NumLock off on VT100/VT220/VT320/
			       * rxvt. */
			retval = NANO_PREVPAGE_KEY;
			break;
		}
		break;
	    case 'o':
		switch (seq[1]) {
		    case 'a': /* Esc o a == Ctrl-Up on Eterm. */
		    case 'b': /* Esc o b == Ctrl-Down on Eterm. */
		    case 'c': /* Esc o c == Ctrl-Right on Eterm. */
		    case 'd': /* Esc o d == Ctrl-Left on Eterm. */
			retval = get_escape_seq_abcd(seq[1]);
			break;
		}
		break;
	    case '[':
		switch (seq[1]) {
		    case '1':
			if (seq_len >= 3) {
			    switch (seq[2]) {
				case '1': /* Esc [ 1 1 ~ == F1 on rxvt/
					   * Eterm. */
				    retval = KEY_F(1);
				    break;
				case '2': /* Esc [ 1 2 ~ == F2 on rxvt/
					   * Eterm. */
				    retval = KEY_F(2);
				    break;
				case '3': /* Esc [ 1 3 ~ == F3 on rxvt/
					   * Eterm. */
				    retval = KEY_F(3);
				    break;
				case '4': /* Esc [ 1 4 ~ == F4 on rxvt/
					   * Eterm. */
				    retval = KEY_F(4);
				    break;
				case '5': /* Esc [ 1 5 ~ == F5 on xterm/
					   * rxvt/Eterm. */
				    retval = KEY_F(5);
				    break;
				case '7': /* Esc [ 1 7 ~ == F6 on
					   * VT220/VT320/Linux console/
					   * xterm/rxvt/Eterm. */
				    retval = KEY_F(6);
				    break;
				case '8': /* Esc [ 1 8 ~ == F7 on
					   * VT220/VT320/Linux console/
					   * xterm/rxvt/Eterm. */
				    retval = KEY_F(7);
				    break;
				case '9': /* Esc [ 1 9 ~ == F8 on
					   * VT220/VT320/Linux console/
					   * xterm/rxvt/Eterm. */
				    retval = KEY_F(8);
				    break;
				case ';':
    if (seq_len >= 4) {
	switch (seq[3]) {
	    case '2':
		if (seq_len >= 5) {
		    switch (seq[4]) {
			case 'A': /* Esc [ 1 ; 2 A == Shift-Up on
				   * xterm. */
			case 'B': /* Esc [ 1 ; 2 B == Shift-Down on
				   * xterm. */
			case 'C': /* Esc [ 1 ; 2 C == Shift-Right on
				   * xterm. */
			case 'D': /* Esc [ 1 ; 2 D == Shift-Left on
				   * xterm. */
			    retval = get_escape_seq_abcd(seq[4]);
			    break;
		    }
		}
		break;
	    case '5':
		if (seq_len >= 5) {
		    switch (seq[4]) {
			case 'A': /* Esc [ 1 ; 5 A == Ctrl-Up on
				   * xterm. */
			case 'B': /* Esc [ 1 ; 5 B == Ctrl-Down on
				   * xterm. */
			case 'C': /* Esc [ 1 ; 5 C == Ctrl-Right on
				   * xterm. */
			case 'D': /* Esc [ 1 ; 5 D == Ctrl-Left on
				   * xterm. */
			    retval = get_escape_seq_abcd(seq[4]);
			    break;
		    }
		}
		break;
	}
    }
				    break;
				default: /* Esc [ 1 ~ == Home on
					  * VT320/Linux console. */
				    retval = NANO_HOME_KEY;
				    break;
			    }
			}
			break;
		    case '2':
			if (seq_len >= 3) {
			    switch (seq[2]) {
				case '0': /* Esc [ 2 0 ~ == F9 on
					   * VT220/VT320/Linux console/
					   * xterm/rxvt/Eterm. */
				    retval = KEY_F(9);
				    break;
				case '1': /* Esc [ 2 1 ~ == F10 on
					   * VT220/VT320/Linux console/
					   * xterm/rxvt/Eterm. */
				    retval = KEY_F(10);
				    break;
				case '3': /* Esc [ 2 3 ~ == F11 on
					   * VT220/VT320/Linux console/
					   * xterm/rxvt/Eterm. */
				    retval = KEY_F(11);
				    break;
				case '4': /* Esc [ 2 4 ~ == F12 on
					   * VT220/VT320/Linux console/
					   * xterm/rxvt/Eterm. */
				    retval = KEY_F(12);
				    break;
				case '5': /* Esc [ 2 5 ~ == F13 on
					   * VT220/VT320/Linux console/
					   * rxvt/Eterm. */
				    retval = KEY_F(13);
				    break;
				case '6': /* Esc [ 2 6 ~ == F14 on
					   * VT220/VT320/Linux console/
					   * rxvt/Eterm. */
				    retval = KEY_F(14);
				    break;
				case '8': /* Esc [ 2 8 ~ == F15 on
					   * VT220/VT320/Linux console/
					   * rxvt/Eterm. */
				    retval = KEY_F(15);
				    break;
				case '9': /* Esc [ 2 9 ~ == F16 on
					   * VT220/VT320/Linux console/
					   * rxvt/Eterm. */
				    retval = KEY_F(16);
				    break;
				default: /* Esc [ 2 ~ == Insert on
					  * VT220/VT320/Linux console/
					  * xterm. */
				    retval = NANO_INSERTFILE_KEY;
				    break;
			    }
			}
			break;
		    case '3': /* Esc [ 3 ~ == Delete on VT220/VT320/
			       * Linux console/xterm. */
			retval = NANO_DELETE_KEY;
			break;
		    case '4': /* Esc [ 4 ~ == End on VT220/VT320/Linux
			       * console/xterm. */
			retval = NANO_END_KEY;
			break;
		    case '5': /* Esc [ 5 ~ == PageUp on VT220/VT320/
			       * Linux console/xterm; Esc [ 5 ^ ==
			       * PageUp on Eterm. */
			retval = NANO_PREVPAGE_KEY;
			break;
		    case '6': /* Esc [ 6 ~ == PageDown on VT220/VT320/
			       * Linux console/xterm; Esc [ 6 ^ ==
			       * PageDown on Eterm. */
			retval = NANO_NEXTPAGE_KEY;
			break;
		    case '7': /* Esc [ 7 ~ == Home on rxvt. */
			retval = NANO_HOME_KEY;
			break;
		    case '8': /* Esc [ 8 ~ == End on rxvt. */
			retval = NANO_END_KEY;
			break;
		    case '9': /* Esc [ 9 == Delete on Mach console. */
			retval = NANO_DELETE_KEY;
			break;
		    case '@': /* Esc [ @ == Insert on Mach console. */
			retval = NANO_INSERTFILE_KEY;
			break;
		    case 'A': /* Esc [ A == Up on ANSI/VT220/Linux
			       * console/FreeBSD console/Mach console/
			       * rxvt/Eterm. */
		    case 'B': /* Esc [ B == Down on ANSI/VT220/Linux
			       * console/FreeBSD console/Mach console/
			       * rxvt/Eterm. */
		    case 'C': /* Esc [ C == Right on ANSI/VT220/Linux
			       * console/FreeBSD console/Mach console/
			       * rxvt/Eterm. */
		    case 'D': /* Esc [ D == Left on ANSI/VT220/Linux
			       * console/FreeBSD console/Mach console/
			       * rxvt/Eterm. */
			retval = get_escape_seq_abcd(seq[1]);
			break;
		    case 'E': /* Esc [ E == Center (5) on numeric keypad
			       * with NumLock off on FreeBSD console. */
			*ignore_seq = TRUE;
			break;
		    case 'F': /* Esc [ F == End on FreeBSD
			       * console/Eterm. */
			retval = NANO_END_KEY;
			break;
		    case 'G': /* Esc [ G == PageDown on FreeBSD
			       * console. */
			retval = NANO_NEXTPAGE_KEY;
			break;
		    case 'H': /* Esc [ H == Home on ANSI/VT220/FreeBSD
			       * console/Mach console/Eterm. */
			retval = NANO_HOME_KEY;
			break;
		    case 'I': /* Esc [ I == PageUp on FreeBSD
			       * console. */
			retval = NANO_PREVPAGE_KEY;
			break;
		    case 'L': /* Esc [ L == Insert on ANSI/FreeBSD
			       * console. */
			retval = NANO_INSERTFILE_KEY;
			break;
		    case 'M': /* Esc [ M == F1 on FreeBSD console. */
			retval = KEY_F(1);
			break;
		    case 'N': /* Esc [ N == F2 on FreeBSD console. */
			retval = KEY_F(2);
			break;
		    case 'O':
			if (seq_len >= 3) {
			    switch (seq[2]) {
				case 'P': /* Esc [ O P == F1 on
					   * xterm. */
				    retval = KEY_F(1);
				    break;
				case 'Q': /* Esc [ O Q == F2 on
					   * xterm. */
				    retval = KEY_F(2);
				    break;
				case 'R': /* Esc [ O R == F3 on
					   * xterm. */
				    retval = KEY_F(3);
				    break;
				case 'S': /* Esc [ O S == F4 on
					   * xterm. */
				    retval = KEY_F(4);
				    break;
			    }
			} else
			    /* Esc [ O == F3 on FreeBSD console. */
			    retval = KEY_F(3);
			break;
		    case 'P': /* Esc [ P == F4 on FreeBSD console. */
			retval = KEY_F(4);
			break;
		    case 'Q': /* Esc [ Q == F5 on FreeBSD console. */
			retval = KEY_F(5);
			break;
		    case 'R': /* Esc [ R == F6 on FreeBSD console. */
			retval = KEY_F(6);
			break;
		    case 'S': /* Esc [ S == F7 on FreeBSD console. */
			retval = KEY_F(7);
			break;
		    case 'T': /* Esc [ T == F8 on FreeBSD console. */
			retval = KEY_F(8);
			break;
		    case 'U': /* Esc [ U == PageDown on Mach console. */
			retval = NANO_NEXTPAGE_KEY;
			break;
		    case 'V': /* Esc [ V == PageUp on Mach console. */
			retval = NANO_PREVPAGE_KEY;
			break;
		    case 'W': /* Esc [ W == F11 on FreeBSD console. */
			retval = KEY_F(11);
			break;
		    case 'X': /* Esc [ X == F12 on FreeBSD console. */
			retval = KEY_F(12);
			break;
		    case 'Y': /* Esc [ Y == End on Mach console. */
			retval = NANO_END_KEY;
			break;
		    case 'Z': /* Esc [ Z == F14 on FreeBSD console. */
			retval = KEY_F(14);
			break;
		    case 'a': /* Esc [ a == Shift-Up on rxvt/Eterm. */
		    case 'b': /* Esc [ b == Shift-Down on rxvt/Eterm. */
		    case 'c': /* Esc [ c == Shift-Right on rxvt/
			       * Eterm. */
		    case 'd': /* Esc [ d == Shift-Left on rxvt/Eterm. */
			retval = get_escape_seq_abcd(seq[1]);
			break;
		    case '[':
			if (seq_len >= 3) {
			    switch (seq[2]) {
				case 'A': /* Esc [ [ A == F1 on Linux
					   * console. */
				    retval = KEY_F(1);
				    break;
				case 'B': /* Esc [ [ B == F2 on Linux
					   * console. */
				    retval = KEY_F(2);
				    break;
				case 'C': /* Esc [ [ C == F3 on Linux
					   * console. */
				    retval = KEY_F(3);
				    break;
				case 'D': /* Esc [ [ D == F4 on Linux
					   * console. */
				    retval = KEY_F(4);
				    break;
				case 'E': /* Esc [ [ E == F5 on Linux
					   * console. */
				    retval = KEY_F(5);
				    break;
			    }
			}
			break;
		}
		break;
	}
    }

#ifdef DEBUG
    fprintf(stderr, "get_escape_seq_kbinput(): retval = %d, ignore_seq = %d\n", retval, (int)*ignore_seq);
#endif

    return retval;
}

/* Return the equivalent arrow key value for the case-insensitive
 * letters A (up), B (down), C (right), and D (left).  These are common
 * to many escape sequences. */
int get_escape_seq_abcd(int kbinput)
{
    switch (tolower(kbinput)) {
	case 'a':
	    return NANO_PREVLINE_KEY;
	case 'b':
	    return NANO_NEXTLINE_KEY;
	case 'c':
	    return NANO_FORWARD_KEY;
	case 'd':
	    return NANO_BACK_KEY;
	default:
	    return ERR;
    }
}

/* Translate a byte sequence: turn a three-digit decimal number from
 * 000 to 255 into its corresponding byte value. */
int get_byte_kbinput(int kbinput
#ifndef NANO_SMALL
	, bool reset
#endif
	)
{
    static int byte_digits = 0, byte = 0;
    int retval = ERR;

#ifndef NANO_SMALL
    if (reset) {
	byte_digits = 0;
	byte = 0;
	return ERR;
    }
#endif

    /* Increment the byte digit counter. */
    byte_digits++;

    switch (byte_digits) {
	case 1:
	    /* One digit: reset the byte sequence holder and add the
	     * digit we got to the 100's position of the byte sequence
	     * holder. */
	    byte = 0;
	    if ('0' <= kbinput && kbinput <= '2')
		byte += (kbinput - '0') * 100;
	    else
		/* If the character we got isn't a decimal digit, or if
		 * it is and it would put the byte sequence out of byte
		 * range, save it as the result. */
		retval = kbinput;
	    break;
	case 2:
	    /* Two digits: add the digit we got to the 10's position of
	     * the byte sequence holder. */
	    if (('0' <= kbinput && kbinput <= '5') || (byte < 200 &&
		'6' <= kbinput && kbinput <= '9'))
		byte += (kbinput - '0') * 10;
	    else
		/* If the character we got isn't a decimal digit, or if
		 * it is and it would put the byte sequence out of byte
		 * range, save it as the result. */
		retval = kbinput;
	    break;
	case 3:
	    /* Three digits: add the digit we got to the 1's position of
	     * the byte sequence holder, and save the corresponding word
	     * value as the result. */
	    if (('0' <= kbinput && kbinput <= '5') || (byte < 250 &&
		'6' <= kbinput && kbinput <= '9')) {
		byte += (kbinput - '0');
		retval = byte;
	    } else
		/* If the character we got isn't a decimal digit, or if
		 * it is and it would put the byte sequence out of word
		 * range, save it as the result. */
		retval = kbinput;
	    break;
	default:
	    /* More than three digits: save the character we got as the
	     * result. */
	    retval = kbinput;
	    break;
    }

    /* If we have a result, reset the byte digit counter and the byte
     * sequence holder. */
    if (retval != ERR) {
	byte_digits = 0;
	byte = 0;
    }

#ifdef DEBUG
    fprintf(stderr, "get_byte_kbinput(): kbinput = %d, byte_digits = %d, byte = %d, retval = %d\n", kbinput, byte_digits, byte, retval);
#endif

    return retval;
}

/* Translate a Unicode sequence: turn a six-digit hexadecimal number
 * from 000000 to 10FFFF (case-insensitive) into its corresponding
 * multibyte value. */
long get_unicode_kbinput(int kbinput
#ifndef NANO_SMALL
	, bool reset
#endif
	)
{
    static int uni_digits = 0;
    static long uni = 0;
    long retval = ERR;

#ifndef NANO_SMALL
    if (reset) {
	uni_digits = 0;
	uni = 0;
	return ERR;
    }
#endif

    /* Increment the Unicode digit counter. */
    uni_digits++;

    switch (uni_digits) {
	case 1:
	    /* One digit: reset the Unicode sequence holder and add the
	     * digit we got to the 0x100000's position of the Unicode
	     * sequence holder. */
	    uni = 0;
	    if ('0' <= kbinput && kbinput <= '1')
		uni += (kbinput - '0') * 0x100000;
	    else
		/* If the character we got isn't a hexadecimal digit, or
		 * if it is and it would put the Unicode sequence out of
		 * valid range, save it as the result. */
		retval = kbinput;
	    break;
	case 2:
	    /* Two digits: add the digit we got to the 0x10000's
	     * position of the Unicode sequence holder. */
	    if ('0' == kbinput || (uni < 0x100000 && '1' <= kbinput &&
		kbinput <= '9'))
		uni += (kbinput - '0') * 0x10000;
	    else if (uni < 0x100000 && 'a' <= tolower(kbinput) &&
		tolower(kbinput) <= 'f')
		uni += (tolower(kbinput) + 10 - 'a') * 0x10000;
	    else
		/* If the character we got isn't a hexadecimal digit, or
		 * if it is and it would put the Unicode sequence out of
		 * valid range, save it as the result. */
		retval = kbinput;
	    break;
	case 3:
	    /* Three digits: add the digit we got to the 0x1000's
	     * position of the Unicode sequence holder. */
	    if ('0' <= kbinput && kbinput <= '9')
		uni += (kbinput - '0') * 0x1000;
	    else if ('a' <= tolower(kbinput) && tolower(kbinput) <= 'f')
		uni += (tolower(kbinput) + 10 - 'a') * 0x1000;
	    else
		/* If the character we got isn't a hexadecimal digit, or
		 * if it is and it would put the Unicode sequence out of
		 * valid range, save it as the result. */
		retval = kbinput;
	    break;
	case 4:
	    /* Four digits: add the digit we got to the 0x100's position
	     * of the Unicode sequence holder. */
	    if ('0' <= kbinput && kbinput <= '9')
		uni += (kbinput - '0') * 0x100;
	    else if ('a' <= tolower(kbinput) && tolower(kbinput) <= 'f')
		uni += (tolower(kbinput) + 10 - 'a') * 0x100;
	    else
		/* If the character we got isn't a hexadecimal digit, or
		 * if it is and it would put the Unicode sequence out of
		 * valid range, save it as the result. */
		retval = kbinput;
	    break;
	case 5:
	    /* Five digits: add the digit we got to the 0x10's position
	     * of the Unicode sequence holder. */
	    if ('0' <= kbinput && kbinput <= '9')
		uni += (kbinput - '0') * 0x10;
	    else if ('a' <= tolower(kbinput) && tolower(kbinput) <= 'f')
		uni += (tolower(kbinput) + 10 - 'a') * 0x10;
	    else
		/* If the character we got isn't a hexadecimal digit, or
		 * if it is and it would put the Unicode sequence out of
		 * valid range, save it as the result. */
		retval = kbinput;
	    break;
	case 6:
	    /* Six digits: add the digit we got to the 1's position of
	     * the Unicode sequence holder, and save the corresponding
	     * Unicode value as the result. */
	    if ('0' <= kbinput && kbinput <= '9') {
		uni += (kbinput - '0');
		retval = uni;
	    } else if ('a' <= tolower(kbinput) && tolower(kbinput) <=
		'f') {
		uni += (tolower(kbinput) + 10 - 'a');
		retval = uni;
	    } else
		/* If the character we got isn't a hexadecimal digit, or
		 * if it is and it would put the Unicode sequence out of
		 * valid range, save it as the result. */
		retval = kbinput;
	    break;
	default:
	    /* More than six digits: save the character we got as the
	     * result. */
	    retval = kbinput;
	    break;
    }

    /* If we have a result, reset the Unicode digit counter and the
     * Unicode sequence holder. */
    if (retval != ERR) {
	uni_digits = 0;
	uni = 0;
    }

#ifdef DEBUG
    fprintf(stderr, "get_unicode_kbinput(): kbinput = %d, uni_digits = %d, uni = %ld, retval = %ld\n", kbinput, uni_digits, uni, retval);
#endif

    return retval;
}

/* Translate a control character sequence: turn an ASCII non-control
 * character into its corresponding control character. */
int get_control_kbinput(int kbinput)
{
    int retval;

     /* Ctrl-2 (Ctrl-Space, Ctrl-@, Ctrl-`) */
    if (kbinput == '2' || kbinput == ' ' || kbinput == '@' ||
	kbinput == '`')
	retval = NANO_CONTROL_SPACE;
    /* Ctrl-3 (Ctrl-[, Esc) to Ctrl-7 (Ctrl-_) */
    else if ('3' <= kbinput && kbinput <= '7')
	retval = kbinput - 24;
    /* Ctrl-8 (Ctrl-?) */
    else if (kbinput == '8' || kbinput == '?')
	retval = NANO_CONTROL_8;
    /* Ctrl-A to Ctrl-_ */
    else if ('A' <= kbinput && kbinput <= '_')
	retval = kbinput - 64;
    /* Ctrl-a to Ctrl-~ */
    else if ('a' <= kbinput && kbinput <= '~')
	retval = kbinput - 96;
    else
	retval = kbinput;

#ifdef DEBUG
    fprintf(stderr, "get_control_kbinput(): kbinput = %d, retval = %d\n", kbinput, retval);
#endif

    return retval;
}

/* Put the output-formatted characters in output back into the default
 * keystroke buffer, so that they can be parsed and displayed as output
 * again. */
void unparse_kbinput(char *output, size_t output_len)
{
    int *input;
    size_t i;

    if (output_len == 0)
	return;

    input = (int *)nmalloc(output_len * sizeof(int));
    for (i = 0; i < output_len; i++)
	input[i] = (int)output[i];
    unget_input(input, output_len);
    free(input);
}

/* Read in a stream of characters verbatim, and return the length of the
 * string in kbinput_len.  Assume nodelay(win) is FALSE. */
int *get_verbatim_kbinput(WINDOW *win, size_t *kbinput_len)
{
    int *retval;

    /* Turn off flow control characters if necessary so that we can type
     * them in verbatim, and turn the keypad off if necessary so that we
     * don't get extended keypad values. */
    if (ISSET(PRESERVE))
	disable_flow_control();
    if (!ISSET(REBIND_KEYPAD))
	keypad(win, FALSE);

    /* Read in a stream of characters and interpret it if possible. */
    retval = parse_verbatim_kbinput(win, kbinput_len);

    /* Turn flow control characters back on if necessary and turn the
     * keypad back on if necessary now that we're done. */
    if (ISSET(PRESERVE))
	enable_flow_control();
    if (!ISSET(REBIND_KEYPAD))
	keypad(win, TRUE);

    return retval;
}

/* Read in a stream of all available characters, and return the length
 * of the string in kbinput_len.  Translate the first few characters of
 * the input into the corresponding multibyte value if possible.  After
 * that, leave the input as-is. */ 
int *parse_verbatim_kbinput(WINDOW *win, size_t *kbinput_len)
{
    int *kbinput, *retval;
    long uni;

    /* Read in the first keystroke. */
    while ((kbinput = get_input(win, 1)) == NULL);

    /* Check whether the first keystroke is a hexadecimal digit. */
    uni = get_unicode_kbinput(*kbinput
#ifndef NANO_SMALL
	, FALSE
#endif
	);

    /* If the first keystroke isn't a hexadecimal digit, put back the
     * first keystroke. */
    if (uni != ERR)
	unget_input(kbinput, 1);
    /* Otherwise, read in keystrokes until we have a complete word
     * sequence, and put back the corresponding word value. */
    else {
	char *uni_mb;
	int uni_mb_len, *seq, i;

	while (uni == ERR) {
	    while ((kbinput = get_input(win, 1)) == NULL);

	    uni = get_unicode_kbinput(*kbinput
#ifndef NANO_SMALL
		, FALSE
#endif
		);
	}

	/* Put back the multibyte equivalent of the Unicode value. */
	uni_mb = make_mbchar(uni, &uni_mb_len);

	seq = (int *)nmalloc(uni_mb_len * sizeof(int));

	for (i = 0; i < uni_mb_len; i++)
	    seq[i] = (unsigned char)uni_mb[i];

	unget_input(seq, uni_mb_len);

	free(seq);
	free(uni_mb);
    }

    /* Get the complete sequence, and save the characters in it as the
     * result. */
    *kbinput_len = get_key_buffer_len();
    retval = get_input(NULL, *kbinput_len);

    return retval;
}

#ifndef DISABLE_MOUSE
/* Check for a mouse event, and if one's taken place, save the
 * coordinates where it took place in mouse_x and mouse_y.  After that,
 * assuming allow_shortcuts is FALSE, if the shortcut list on the
 * bottom two lines of the screen is visible and the mouse event took
 * place on it, figure out which shortcut was clicked and put back the
 * equivalent keystroke(s).  Return FALSE if no keystrokes were
 * put back, or TRUE if at least one was.  Assume that KEY_MOUSE has
 * already been read in. */
bool get_mouseinput(int *mouse_x, int *mouse_y, bool allow_shortcuts)
{
    MEVENT mevent;

    *mouse_x = -1;
    *mouse_y = -1;

    /* First, get the actual mouse event. */
    if (getmouse(&mevent) == ERR)
	return FALSE;

    /* Save the screen coordinates where the mouse event took place. */
    *mouse_x = mevent.x;
    *mouse_y = mevent.y;

    /* If we're allowing shortcuts, the current shortcut list is being
     * displayed on the last two lines of the screen, and the mouse
     * event took place inside it, we need to figure out which shortcut
     * was clicked and put back the equivalent keystroke(s) for it. */
    if (allow_shortcuts && !ISSET(NO_HELP) && wenclose(bottomwin,
	*mouse_y, *mouse_x)) {
	int i, j;
	size_t currslen;
	    /* The number of shortcuts in the current shortcut list. */
	const shortcut *s = currshortcut;
	    /* The actual shortcut we clicked on, starting at the first
	     * one in the current shortcut list. */

	/* Get the shortcut lists' length. */
	if (currshortcut == main_list)
	    currslen = MAIN_VISIBLE;
	else {
	    currslen = length_of_list(currshortcut);

	    /* We don't show any more shortcuts than the main list
	     * does. */
	    if (currslen > MAIN_VISIBLE)
		currslen = MAIN_VISIBLE;
	}

	/* Calculate the width of each shortcut in the list.  It's the
	 * same for all of them. */
	if (currslen < 2)
	    i = COLS / 6;
	else
	    i = COLS / ((currslen / 2) + (currslen % 2));

	/* Calculate the y-coordinate relative to the beginning of
	 * bottomwin. */
	j = *mouse_y - ((2 - no_more_space()) + 1) - editwinrows;

	/* If we're on the statusbar, beyond the end of the shortcut
	 * list, or beyond the end of a shortcut on the right side of
	 * the screen, don't do anything. */
	if (j < 0 || (*mouse_x / i) >= currslen)
	    return FALSE;
	j = (*mouse_x / i) * 2 + j;
	if (j >= currslen)
	    return FALSE;

	/* Go through the shortcut list to determine which shortcut was
	 * clicked. */
	for (; j > 0; j--)
	    s = s->next;

	/* And put back the equivalent key.  Assume that each shortcut
	 * has, at the very least, an equivalent control key, an
	 * equivalent primary meta key sequence, or both. */
	if (s->ctrlval != NANO_NO_KEY) {
	    unget_kbinput(s->ctrlval, FALSE, FALSE);
	    return TRUE;
	} else if (s->metaval != NANO_NO_KEY) {
	    unget_kbinput(s->metaval, TRUE, FALSE);
	    return TRUE;
	}
    }
    return FALSE;
}
#endif /* !DISABLE_MOUSE */

const shortcut *get_shortcut(const shortcut *s_list, int *kbinput, bool
	*meta_key, bool *func_key)
{
    const shortcut *s = s_list;
    size_t slen = length_of_list(s_list);

#ifdef DEBUG
    fprintf(stderr, "get_shortcut(): kbinput = %d, meta_key = %d, func_key = %d\n", *kbinput, (int)*meta_key, (int)*func_key);
#endif

    /* Check for shortcuts. */
    for (; slen > 0; slen--) {
	/* We've found a shortcut if:
	 *
	 * 1. The key exists.
	 * 2. The key is a control key in the shortcut list.
	 * 3. meta_key is TRUE and the key is the primary or
	 *    miscellaneous meta sequence in the shortcut list.
	 * 4. func_key is TRUE and the key is a function key in the
	 *    shortcut list. */

	if (*kbinput != NANO_NO_KEY && (*kbinput == s->ctrlval ||
		(*meta_key == TRUE && (*kbinput == s->metaval ||
		*kbinput == s->miscval)) || (*func_key == TRUE &&
		*kbinput == s->funcval))) {
	    break;
	}

	s = s->next;
    }

    /* Translate the shortcut to either its control key or its meta key
     * equivalent.  Assume that the shortcut has an equivalent control
     * key, an equivalent primary meta key sequence, or both. */
    if (slen > 0) {
	if (s->ctrlval != NANO_NO_KEY) {
	    *meta_key = FALSE;
	    *func_key = FALSE;
	    *kbinput = s->ctrlval;
	    return s;
	} else if (s->metaval != NANO_NO_KEY) {
	    *meta_key = TRUE;
	    *func_key = FALSE;
	    *kbinput = s->metaval;
	    return s;
	}
    }

    return NULL;
}

#ifndef NANO_SMALL
const toggle *get_toggle(int kbinput, bool meta_key)
{
    const toggle *t = toggles;

#ifdef DEBUG
    fprintf(stderr, "get_toggle(): kbinput = %d, meta_key = %d\n", kbinput, (int)meta_key);
#endif

    /* Check for toggles. */
    for (; t != NULL; t = t->next) {
	/* We've found a toggle if meta_key is TRUE and the key is in
	 * the meta key toggle list. */
	if (meta_key && kbinput == t->val)
	    break;
    }

    return t;
}
#endif /* !NANO_SMALL */

int do_statusbar_input(bool *meta_key, bool *func_key, bool *s_or_t,
	bool *ran_func, bool *finished, bool allow_funcs)
{
    int input;
	/* The character we read in. */
    static int *kbinput = NULL;
	/* The input buffer. */
    static size_t kbinput_len = 0;
	/* The length of the input buffer. */
    const shortcut *s;
    bool have_shortcut;

    *s_or_t = FALSE;
    *ran_func = FALSE;
    *finished = FALSE;

    /* Read in a character. */
    input = get_kbinput(bottomwin, meta_key, func_key);

#ifndef DISABLE_MOUSE
    /* If we got a mouse click and it was on a shortcut, read in the
     * shortcut character. */
    if (allow_funcs && *func_key == TRUE && input == KEY_MOUSE) {
	if (do_statusbar_mouse())
	    input = get_kbinput(bottomwin, meta_key, func_key);
	else
	    input = ERR;
    }
#endif

    /* Check for a shortcut in the current list. */
    s = get_shortcut(currshortcut, &input, meta_key, func_key);

    /* If we got a shortcut from the current list, or a "universal"
     * statusbar prompt shortcut, set have_shortcut to TRUE. */
    have_shortcut = (s != NULL || input == NANO_REFRESH_KEY ||
	input == NANO_HOME_KEY || input == NANO_END_KEY ||
	input == NANO_FORWARD_KEY || input == NANO_BACK_KEY ||
	input == NANO_BACKSPACE_KEY || input == NANO_DELETE_KEY ||
	input == NANO_CUT_KEY ||
#ifndef NANO_SMALL
		input == NANO_NEXTWORD_KEY ||
#endif
		(*meta_key == TRUE && (
#ifndef NANO_SMALL
		input == NANO_PREVWORD_KEY ||
#endif
		input == NANO_VERBATIM_KEY)));

    /* Set s_or_t to TRUE if we got a shortcut. */
    *s_or_t = have_shortcut;

    if (allow_funcs) {
	/* If we got a character, and it isn't a shortcut or toggle,
	 * it's a normal text character.  Display the warning if we're
	 * in view mode, or add the character to the input buffer if
	 * we're not. */
	if (input != ERR && *s_or_t == FALSE) {
	    /* If we're using restricted mode, the filename isn't blank,
	     * and we're at the "Write File" prompt, disable text
	     * input. */
	    if (!ISSET(RESTRICTED) || openfile->filename[0] == '\0' ||
		currshortcut != writefile_list) {
		kbinput_len++;
		kbinput = (int *)nrealloc(kbinput, kbinput_len *
			sizeof(int));
		kbinput[kbinput_len - 1] = input;
	    }
	}

	/* If we got a shortcut, or if there aren't any other characters
	 * waiting after the one we read in, we need to display all the
	 * characters in the input buffer if it isn't empty. */
	 if (*s_or_t == TRUE || get_key_buffer_len() == 0) {
	    if (kbinput != NULL) {

		/* Display all the characters in the input buffer at
		 * once, filtering out control characters. */
		char *output = charalloc(kbinput_len + 1);
		size_t i;
		bool got_enter;
			/* Whether we got the Enter key. */

		for (i = 0; i < kbinput_len; i++)
		    output[i] = (char)kbinput[i];
		output[i] = '\0';

		do_statusbar_output(output, kbinput_len, &got_enter,
			FALSE);

		free(output);

		/* Empty the input buffer. */
		kbinput_len = 0;
		free(kbinput);
		kbinput = NULL;
	    }
	}

	if (have_shortcut) {
	    switch (input) {
		/* Handle the "universal" statusbar prompt shortcuts. */
		case NANO_REFRESH_KEY:
		    total_refresh();
		    break;
		case NANO_HOME_KEY:
		    do_statusbar_home();
		    break;
		case NANO_END_KEY:
		    do_statusbar_end();
		    break;
		case NANO_FORWARD_KEY:
		    do_statusbar_right();
		    break;
		case NANO_BACK_KEY:
		    do_statusbar_left();
		    break;
		case NANO_BACKSPACE_KEY:
		    /* If we're using restricted mode, the filename
		     * isn't blank, and we're at the "Write File"
		     * prompt, disable Backspace. */
		    if (!ISSET(RESTRICTED) || openfile->filename[0] ==
			'\0' || currshortcut != writefile_list)
			do_statusbar_backspace();
		    break;
		case NANO_DELETE_KEY:
		    /* If we're using restricted mode, the filename
		     * isn't blank, and we're at the "Write File"
		     * prompt, disable Delete. */
		    if (!ISSET(RESTRICTED) || openfile->filename[0] ==
			'\0' || currshortcut != writefile_list)
			do_statusbar_delete();
		    break;
		case NANO_CUT_KEY:
		    /* If we're using restricted mode, the filename
		     * isn't blank, and we're at the "Write File"
		     * prompt, disable Cut. */
		    if (!ISSET(RESTRICTED) || openfile->filename[0] ==
			'\0' || currshortcut != writefile_list)
			do_statusbar_cut_text();
		    break;
#ifndef NANO_SMALL
		case NANO_NEXTWORD_KEY:
		    do_statusbar_next_word(FALSE);
		    break;
		case NANO_PREVWORD_KEY:
		    if (*meta_key == TRUE)
			do_statusbar_prev_word(FALSE);
		    break;
#endif
		case NANO_VERBATIM_KEY:
		    if (*meta_key == TRUE) {
			/* If we're using restricted mode, the filename
			 * isn't blank, and we're at the "Write File"
			 * prompt, disable verbatim input. */
			if (!ISSET(RESTRICTED) ||
				openfile->filename[0] == '\0' ||
				currshortcut != writefile_list) {
			    bool got_enter;
				/* Whether we got the Enter key. */

			    do_statusbar_verbatim_input(&got_enter);

			    /* If we got the Enter key, set input to the
			     * key value for Enter, and set finished to
			     * TRUE to indicate that we're done. */
			    if (got_enter) {
				input = NANO_ENTER_KEY;
				*finished = TRUE;
			    }
			}
			break;
		    }
		/* Handle the normal statusbar prompt shortcuts, setting
		 * ran_func to TRUE if we try to run their associated
		 * functions and setting finished to TRUE to indicate
		 * that we're done after trying to run their associated
		 * functions. */
		default:
		    if (s->func != NULL) {
			*ran_func = TRUE;
			if (!ISSET(VIEW_MODE) || s->viewok)
			    s->func();
		    }
		    *finished = TRUE;
	    }
	}
    }

    return input;
}

#ifndef DISABLE_MOUSE
bool do_statusbar_mouse(void)
{
    /* FIXME: If we clicked on a location in the statusbar, the cursor
     * should move to the location we clicked on.  This functionality
     * should be in this function. */
    int mouse_x, mouse_y;
    return get_mouseinput(&mouse_x, &mouse_y, TRUE);
}
#endif

/* The user typed output_len multibyte characters.  Add them to the
 * statusbar prompt, setting got_enter to TRUE if we get a newline, and
 * filtering out all control characters if allow_cntrls is TRUE. */
void do_statusbar_output(char *output, size_t output_len, bool
	*got_enter, bool allow_cntrls)
{
    size_t answer_len, i = 0;
    char *char_buf = charalloc(mb_cur_max());
    int char_buf_len;

    assert(answer != NULL);

    answer_len = strlen(answer);
    *got_enter = FALSE;

    while (i < output_len) {
	/* If allow_cntrls is FALSE, filter out nulls and newlines,
	 * since they're control characters. */
	if (allow_cntrls) {
	    /* Null to newline, if needed. */
	    if (output[i] == '\0')
		output[i] = '\n';
	    /* Newline to Enter, if needed. */
	    else if (output[i] == '\n') {
		/* Set got_enter to TRUE to indicate that we got the
		 * Enter key, put back the rest of the characters in
		 * output so that they can be parsed and output again,
		 * and get out. */
		*got_enter = TRUE;
		unparse_kbinput(output + i, output_len - i);
		return;
	    }
	}

	/* Interpret the next multibyte character. */
	char_buf_len = parse_mbchar(output + i, char_buf, NULL);

	i += char_buf_len;

	/* If allow_cntrls is FALSE, filter out a control character. */
	if (!allow_cntrls && is_cntrl_mbchar(output + i - char_buf_len))
	    continue;

	/* More dangerousness fun =) */
	answer = charealloc(answer, answer_len + (char_buf_len * 2));

	assert(statusbar_x <= answer_len);

	charmove(&answer[statusbar_x + char_buf_len],
		&answer[statusbar_x], answer_len - statusbar_x +
		char_buf_len);
	strncpy(&answer[statusbar_x], char_buf, char_buf_len);
	answer_len += char_buf_len;

	statusbar_x += char_buf_len;
    }

    free(char_buf);
}

void do_statusbar_home(void)
{
#ifndef NANO_SMALL
    if (ISSET(SMART_HOME)) {
	size_t statusbar_x_save = statusbar_x;

	statusbar_x = indent_length(answer);

	if (statusbar_x == statusbar_x_save ||
		statusbar_x == strlen(answer))
	    statusbar_x = 0;
    } else
#endif
	statusbar_x = 0;
}

void do_statusbar_end(void)
{
    statusbar_x = strlen(answer);
}

void do_statusbar_right(void)
{
    if (statusbar_x < strlen(answer))
	statusbar_x = move_mbright(answer, statusbar_x);
}

void do_statusbar_left(void)
{
    if (statusbar_x > 0)
	statusbar_x = move_mbleft(answer, statusbar_x);
}

void do_statusbar_backspace(void)
{
    if (statusbar_x > 0) {
	do_statusbar_left();
	do_statusbar_delete();
    }
}

void do_statusbar_delete(void)
{
    if (answer[statusbar_x] != '\0') {
	int char_buf_len = parse_mbchar(answer + statusbar_x, NULL,
		NULL);
	size_t line_len = strlen(answer + statusbar_x);

	assert(statusbar_x < strlen(answer));

	charmove(answer + statusbar_x, answer + statusbar_x +
		char_buf_len, strlen(answer) - statusbar_x -
		char_buf_len + 1);

	null_at(&answer, statusbar_x + line_len - char_buf_len);
    }
}

/* Move text from the statusbar prompt into oblivion. */
void do_statusbar_cut_text(void)
{
    assert(answer != NULL);

#ifndef NANO_SMALL
    if (ISSET(CUT_TO_END))
	null_at(&answer, statusbar_x);
    else {
#endif
	null_at(&answer, 0);
	statusbar_x = 0;
#ifndef NANO_SMALL
    }
#endif
}

#ifndef NANO_SMALL
/* Move to the next word at the statusbar prompt.  If allow_punct is
 * TRUE, treat punctuation as part of a word.  Return TRUE if we started
 * on a word, and FALSE otherwise. */
bool do_statusbar_next_word(bool allow_punct)
{
    char *char_mb;
    int char_mb_len;
    bool end_line = FALSE, started_on_word = FALSE;

    assert(answer != NULL);

    char_mb = charalloc(mb_cur_max());

    /* Move forward until we find the character after the last letter of
     * the current word. */
    while (!end_line) {
	char_mb_len = parse_mbchar(answer + statusbar_x, char_mb, NULL);

	/* If we've found it, stop moving forward through the current
	 * line. */
	if (!is_word_mbchar(char_mb, allow_punct))
	    break;

	/* If we haven't found it, then we've started on a word, so set
	 * started_on_word to TRUE. */
	started_on_word = TRUE;

	if (answer[statusbar_x] == '\0')
	    end_line = TRUE;
	else
	    statusbar_x += char_mb_len;
    }

    /* Move forward until we find the first letter of the next word. */
    if (answer[statusbar_x] == '\0')
	end_line = TRUE;
    else
	statusbar_x += char_mb_len;

    while (!end_line) {
	char_mb_len = parse_mbchar(answer + statusbar_x, char_mb, NULL);

	/* If we've found it, stop moving forward through the current
	 * line. */
	if (is_word_mbchar(char_mb, allow_punct))
	    break;

	if (answer[statusbar_x] == '\0')
	    end_line = TRUE;
	else
	    statusbar_x += char_mb_len;
    }

    free(char_mb);

    /* Return whether we started on a word. */
    return started_on_word;
}

/* Move to the previous word at the statusbar prompt.  If allow_punct is
 * TRUE, treat punctuation as part of a word.  Return TRUE if we started
 * on a word, and FALSE otherwise. */
bool do_statusbar_prev_word(bool allow_punct)
{
    char *char_mb;
    int char_mb_len;
    bool begin_line = FALSE, started_on_word = FALSE;

    assert(answer != NULL);

    char_mb = charalloc(mb_cur_max());

    /* Move backward until we find the character before the first letter
     * of the current word. */
    while (!begin_line) {
	char_mb_len = parse_mbchar(answer + statusbar_x, char_mb, NULL);

	/* If we've found it, stop moving backward through the current
	 * line. */
	if (!is_word_mbchar(char_mb, allow_punct))
	    break;

	/* If we haven't found it, then we've started on a word, so set
	 * started_on_word to TRUE. */
	started_on_word = TRUE;

	if (statusbar_x == 0)
	    begin_line = TRUE;
	else
	    statusbar_x = move_mbleft(answer, statusbar_x);
    }

    /* Move backward until we find the last letter of the previous
     * word. */
    if (statusbar_x == 0)
	begin_line = TRUE;
    else
	statusbar_x = move_mbleft(answer, statusbar_x);

    while (!begin_line) {
	char_mb_len = parse_mbchar(answer + statusbar_x, char_mb, NULL);

	/* If we've found it, stop moving backward through the current
	 * line. */
	if (is_word_mbchar(char_mb, allow_punct))
	    break;

	if (statusbar_x == 0)
	    begin_line = TRUE;
	else
	    statusbar_x = move_mbleft(answer, statusbar_x);
    }

    /* If we've found it, move backward until we find the character
     * before the first letter of the previous word. */
    if (!begin_line) {
	if (statusbar_x == 0)
	    begin_line = TRUE;
	else
	    statusbar_x = move_mbleft(answer, statusbar_x);

	while (!begin_line) {
	    char_mb_len = parse_mbchar(answer + statusbar_x, char_mb,
		NULL);

	    /* If we've found it, stop moving backward through the
	     * current line. */
	    if (!is_word_mbchar(char_mb, allow_punct))
		break;

	    if (statusbar_x == 0)
		begin_line = TRUE;
	    else
		statusbar_x = move_mbleft(answer, statusbar_x);
	}

	/* If we've found it, move forward to the first letter of the
	 * previous word. */
	if (!begin_line)
	    statusbar_x += char_mb_len;
    }

    free(char_mb);

    /* Return whether we started on a word. */
    return started_on_word;
}
#endif /* !NANO_SMALL */

void do_statusbar_verbatim_input(bool *got_enter)
{
    int *kbinput;
    size_t kbinput_len, i;
    char *output;

    *got_enter = FALSE;

    /* Read in all the verbatim characters. */
    kbinput = get_verbatim_kbinput(bottomwin, &kbinput_len);

    /* Display all the verbatim characters at once, not filtering out
     * control characters. */
    output = charalloc(kbinput_len + 1);

    for (i = 0; i < kbinput_len; i++)
	output[i] = (char)kbinput[i];
    output[i] = '\0';

    do_statusbar_output(output, kbinput_len, got_enter, TRUE);

    free(output);
}

/* Return the placewewant associated with current_x, i.e, the zero-based
 * column position of the cursor.  The value will be no smaller than
 * current_x. */
size_t xplustabs(void)
{
    return strnlenpt(openfile->current->data, openfile->current_x);
}

/* actual_x() gives the index in str of the character displayed at
 * column xplus.  That is, actual_x() is the largest value such that
 * strnlenpt(str, actual_x(str, xplus)) <= xplus. */
size_t actual_x(const char *str, size_t xplus)
{
    size_t i = 0;
	/* The position in str, returned. */
    size_t length = 0;
	/* The screen display width to str[i]. */

    assert(str != NULL);

    while (*str != '\0') {
	int str_len = parse_mbchar(str, NULL, &length);

	if (length > xplus)
	    break;

	i += str_len;
	str += str_len;
    }

    return i;
}

/* A strlen() with tabs factored in, similar to xplustabs().  How many
 * columns wide are the first size characters of str? */
size_t strnlenpt(const char *str, size_t size)
{
    size_t length = 0;
	/* The screen display width to str[i]. */

    if (size == 0)
	return 0;

    assert(str != NULL);

    while (*str != '\0') {
	int str_len = parse_mbchar(str, NULL, &length);

	str += str_len;

	if (size <= str_len)
	    break;

	size -= str_len;
    }

    return length;
}

/* How many columns wide is buf? */
size_t strlenpt(const char *buf)
{
    return strnlenpt(buf, (size_t)-1);
}

/* Move to (x, y) in win, and display a line of n spaces with the
 * current attributes. */
void blank_line(WINDOW *win, int y, int x, int n)
{
    wmove(win, y, x);
    for (; n > 0; n--)
	waddch(win, ' ');
}

void blank_titlebar(void)
{
    blank_line(topwin, 0, 0, COLS);
}

void blank_topbar(void)
{
    if (!ISSET(MORE_SPACE))
	blank_line(topwin, 1, 0, COLS);
}

void blank_edit(void)
{
    int i;
    for (i = 0; i < editwinrows; i++)
	blank_line(edit, i, 0, COLS);
}

void blank_statusbar(void)
{
    blank_line(bottomwin, 0, 0, COLS);
}

void blank_bottombars(void)
{
    if (!ISSET(NO_HELP)) {
	blank_line(bottomwin, 1, 0, COLS);
	blank_line(bottomwin, 2, 0, COLS);
    }
}

void check_statusblank(void)
{
    if (statusblank > 0)
	statusblank--;

    if (statusblank == 0 && !ISSET(CONST_UPDATE)) {
	blank_statusbar();
	wnoutrefresh(bottomwin);
	reset_cursor();
	wnoutrefresh(edit);
    }
}

/* Convert buf into a string that can be displayed on screen.  The
 * caller wants to display buf starting with column start_col, and
 * extending for at most len columns.  start_col is zero-based.  len is
 * one-based, so len == 0 means you get "" returned.  The returned
 * string is dynamically allocated, and should be freed.  If dollars is
 * TRUE, the caller might put "$" at the beginning or end of the line if
 * it's too long. */
char *display_string(const char *buf, size_t start_col, size_t len, bool
	dollars)
{
    size_t start_index;
	/* Index in buf of the first character shown. */
    size_t column;
	/* Screen column that start_index corresponds to. */
    size_t alloc_len;
	/* The length of memory allocated for converted. */
    char *converted;
	/* The string we return. */
    size_t index;
	/* Current position in converted. */

    char *buf_mb = charalloc(mb_cur_max());
    int buf_mb_len;

    /* If dollars is TRUE, make room for the "$" at the end of the
     * line. */
    if (dollars && len > 0 && strlenpt(buf) > start_col + len)
	len--;

    if (len == 0)
	return mallocstrcpy(NULL, "");

    start_index = actual_x(buf, start_col);
    column = strnlenpt(buf, start_index);

    assert(column <= start_col);

    /* Allocate enough space for the entire line. */
    alloc_len = (mb_cur_max() * (COLS + 1));

    converted = charalloc(alloc_len + 1);
    index = 0;

    if (buf[start_index] != '\t' && (column < start_col || (dollars &&
	column > 0))) {
	/* We don't display all of buf[start_index] since it starts to
	 * the left of the screen. */
	buf_mb_len = parse_mbchar(buf + start_index, buf_mb, NULL);

	if (is_cntrl_mbchar(buf_mb)) {
	    if (column < start_col) {
		char *ctrl_buf_mb = charalloc(mb_cur_max());
		int ctrl_buf_mb_len, i;

		ctrl_buf_mb = control_mbrep(buf_mb, ctrl_buf_mb,
			&ctrl_buf_mb_len);

		for (i = 0; i < ctrl_buf_mb_len; i++)
		    converted[index++] = ctrl_buf_mb[i];

		start_col += mbwidth(ctrl_buf_mb);

		free(ctrl_buf_mb);

		start_index += buf_mb_len;
	    }
	}
#ifdef ENABLE_UTF8
	else if (ISSET(USE_UTF8) && mbwidth(buf_mb) > 1) {
	    converted[index++] = ' ';
	    start_col++;

	    start_index += buf_mb_len;
	}
#endif
    }

    while (index < alloc_len - 1 && buf[start_index] != '\0') {
	buf_mb_len = parse_mbchar(buf + start_index, buf_mb, NULL);

	/* If buf contains a tab character, interpret it. */
	if (*buf_mb == '\t') {
#if !defined(NANO_SMALL) && defined(ENABLE_NANORC)
	    if (ISSET(WHITESPACE_DISPLAY)) {
		int i;

		for (i = 0; i < whitespace_len[0]; i++)
		    converted[index++] = whitespace[i];
	    } else
#endif
		converted[index++] = ' '; 
	    start_col++;
	    while (start_col % tabsize != 0) {
		converted[index++] = ' ';
		start_col++;
	    }
	/* If buf contains a control character, interpret it.  If buf
	 * contains an invalid multibyte control character, display it
	 * as such.*/
	} else if (is_cntrl_mbchar(buf_mb)) {
	    char *ctrl_buf_mb = charalloc(mb_cur_max());
	    int ctrl_buf_mb_len, i;

	    converted[index++] = '^';
	    start_col++;

	    ctrl_buf_mb = control_mbrep(buf_mb, ctrl_buf_mb,
		&ctrl_buf_mb_len);

	    for (i = 0; i < ctrl_buf_mb_len; i++)
		converted[index++] = ctrl_buf_mb[i];

	    start_col += mbwidth(ctrl_buf_mb);

	    free(ctrl_buf_mb);
	/* If buf contains a space character, interpret it. */
	} else if (*buf_mb == ' ') {
#if !defined(NANO_SMALL) && defined(ENABLE_NANORC)
	    if (ISSET(WHITESPACE_DISPLAY)) {
		int i;

		for (i = whitespace_len[0]; i < whitespace_len[0] +
			whitespace_len[1]; i++)
		    converted[index++] = whitespace[i];
	    } else
#endif
		converted[index++] = ' '; 
	    start_col++;
	/* If buf contains a non-control character, interpret it.  If
	 * buf contains an invalid multibyte non-control character,
	 * display it as such. */
	} else {
	    char *nctrl_buf_mb = charalloc(mb_cur_max());
	    int nctrl_buf_mb_len, i;

	    nctrl_buf_mb = mbrep(buf_mb, nctrl_buf_mb,
		&nctrl_buf_mb_len);

	    for (i = 0; i < nctrl_buf_mb_len; i++)
		converted[index++] = nctrl_buf_mb[i];

	    start_col += mbwidth(nctrl_buf_mb);

	    free(nctrl_buf_mb);
	}

	start_index += buf_mb_len;
    }

    free(buf_mb);

    if (index < alloc_len - 1)
	converted[index] = '\0';

    /* Make sure converted takes up no more than len columns. */
    index = actual_x(converted, len);
    null_at(&converted, index);

    return converted;
}

/* Repaint the statusbar when getting a character in nanogetstr().  buf
 * should be no longer than max(0, COLS - 4).
 *
 * Note that we must turn on A_REVERSE here, since do_help() turns it
 * off! */
void nanoget_repaint(const char *buf, const char *inputbuf, size_t x)
{
    size_t x_real = strnlenpt(inputbuf, x);
    int wid = COLS - strlenpt(buf) - 2;

    assert(x <= strlen(inputbuf));

    wattron(bottomwin, A_REVERSE);
    blank_statusbar();

    mvwaddnstr(bottomwin, 0, 0, buf, actual_x(buf, COLS - 2));
    waddch(bottomwin, ':');

    if (COLS > 1)
	waddch(bottomwin, x_real < wid ? ' ' : '$');
    if (COLS > 2) {
	size_t page_start = x_real - x_real % wid;
	char *expanded = display_string(inputbuf, page_start, wid,
		FALSE);

	assert(wid > 0);
	assert(strlenpt(expanded) <= wid);

	waddstr(bottomwin, expanded);
	free(expanded);
	wmove(bottomwin, 0, COLS - wid + x_real - page_start);
    } else
	wmove(bottomwin, 0, COLS - 1);

    wattroff(bottomwin, A_REVERSE);
}

/* Get the input from the keyboard; this should only be called from
 * statusq(). */
int nanogetstr(bool allow_tabs, const char *buf, const char *curranswer,
#ifndef NANO_SMALL
	filestruct **history_list,
#endif
	const shortcut *s
#ifndef DISABLE_TABCOMP
	, bool *list
#endif
	)
{
    int kbinput;
    bool meta_key, func_key, s_or_t, ran_func, finished;
    size_t curranswer_len;
#ifndef DISABLE_TABCOMP
    bool tabbed = FALSE;
	/* Whether we've pressed Tab. */
#endif
#ifndef NANO_SMALL
    char *history = NULL;
	/* The current history string. */
    char *magichistory = NULL;
	/* The temporary string typed at the bottom of the history, if
	 * any. */
#ifndef DISABLE_TABCOMP
    int last_kbinput = ERR;
	/* The key we pressed before the current key. */
    size_t complete_len = 0;
	/* The length of the original string that we're trying to
	 * tab complete, if any. */
#endif
#endif /* !NANO_SMALL */

    answer = mallocstrcpy(answer, curranswer);
    curranswer_len = strlen(answer);

    /* Only put statusbar_x at the end of the string if it's
     * uninitialized, if it would be past the end of curranswer, or if
     * resetstatuspos is TRUE.  Otherwise, leave it alone.  This is so
     * the cursor position stays at the same place if a prompt-changing
     * toggle is pressed. */
    if (statusbar_x == (size_t)-1 || statusbar_x > curranswer_len ||
		resetstatuspos)
	statusbar_x = curranswer_len;

    currshortcut = s;

    nanoget_repaint(buf, answer, statusbar_x);

    /* Refresh the edit window and the statusbar before getting
     * input. */
    wnoutrefresh(edit);
    wnoutrefresh(bottomwin);

    /* If we're using restricted mode, we aren't allowed to change the
     * name of a file once it has one because that would allow writing
     * to files not specified on the command line.  In this case,
     * disable all keys that would change the text if the filename isn't
     * blank and we're at the "Write File" prompt. */
    while ((kbinput = do_statusbar_input(&meta_key, &func_key,
	&s_or_t, &ran_func, &finished, TRUE)) != NANO_CANCEL_KEY &&
	kbinput != NANO_ENTER_KEY) {

	assert(statusbar_x <= strlen(answer));

#ifndef DISABLE_TABCOMP
	if (kbinput != NANO_TAB_KEY)
	    tabbed = FALSE;
#endif

	switch (kbinput) {
	    case NANO_TAB_KEY:
#ifndef DISABLE_TABCOMP
#ifndef NANO_SMALL
		if (history_list != NULL) {
		    if (last_kbinput != NANO_TAB_KEY)
			complete_len = strlen(answer);

		    if (complete_len > 0) {
			answer = mallocstrcpy(answer,
				get_history_completion(history_list,
				answer, complete_len));
			statusbar_x = strlen(answer);
		    }
		} else
#endif /* !NANO_SMALL */
		if (allow_tabs)
		    answer = input_tab(answer, &statusbar_x, &tabbed,
			list);
#endif /* !DISABLE_TABCOMP */
		break;
	    case NANO_PREVLINE_KEY:
#ifndef NANO_SMALL
		if (history_list != NULL) {
		    /* If we're scrolling up at the bottom of the
		     * history list and answer isn't blank, save answer
		     * in magichistory. */
		    if ((*history_list)->next == NULL &&
			answer[0] != '\0')
			magichistory = mallocstrcpy(magichistory,
				answer);

		    /* Get the older search from the history list and
		     * save it in answer.  If there is no older search,
		     * don't do anything. */
		    if ((history =
			get_history_older(history_list)) != NULL) {
			answer = mallocstrcpy(answer, history);
			statusbar_x = strlen(answer);
		    }

		    /* This key has a shortcut list entry when it's used
		     * to move to an older search, which means that
		     * finished has been set to TRUE.  Set it back to
		     * FALSE here, so that we aren't kicked out of the
		     * statusbar prompt. */
		    finished = FALSE;
		}
#endif /* !NANO_SMALL */
		break;
	    case NANO_NEXTLINE_KEY:
#ifndef NANO_SMALL
		if (history_list != NULL) {
		    /* Get the newer search from the history list and
		     * save it in answer.  If there is no newer search,
		     * don't do anything. */
		    if ((history =
			get_history_newer(history_list)) != NULL) {
			answer = mallocstrcpy(answer, history);
			statusbar_x = strlen(answer);
		    }

		    /* If, after scrolling down, we're at the bottom of
		     * the history list, answer is blank, and
		     * magichistory is set, save magichistory in
		     * answer. */
		    if ((*history_list)->next == NULL &&
			answer[0] == '\0' && magichistory != NULL) {
			answer = mallocstrcpy(answer, magichistory);
			statusbar_x = strlen(answer);
		    }
		}
#endif /* !NANO_SMALL */
		break;
	}

	/* If we have a shortcut with an associated function, break out
	 * if we're finished after running or trying to run the
	 * function. */
	if (finished)
	    break;

#if !defined(NANO_SMALL) && !defined(DISABLE_TABCOMP)
	last_kbinput = kbinput;
#endif

	nanoget_repaint(buf, answer, statusbar_x);
	wnoutrefresh(bottomwin);
    }

#ifndef NANO_SMALL
    /* Set the current position in the history list to the bottom and
     * free magichistory, if we need to. */
    if (history_list != NULL) {
	history_reset(*history_list);

	if (magichistory != NULL)
	    free(magichistory);
    }
#endif

    /* We finished putting in an answer or ran a normal shortcut's
     * associated function, so reset statusbar_x. */
    if (kbinput == NANO_CANCEL_KEY || kbinput == NANO_ENTER_KEY ||
	ran_func)
	statusbar_x = (size_t)-1;

    return kbinput;
}

/* Ask a question on the statusbar.  Answer will be stored in answer
 * global.  Returns -1 on aborted enter, -2 on a blank string, and 0
 * otherwise, the valid shortcut key caught.  curranswer is any editable
 * text that we want to put up by default.
 *
 * The allow_tabs parameter indicates whether we should allow tabs to be
 * interpreted. */
int statusq(bool allow_tabs, const shortcut *s, const char *curranswer,
#ifndef NANO_SMALL
	filestruct **history_list,
#endif
	const char *msg, ...)
{
    va_list ap;
    char *foo = charalloc(((COLS - 4) * mb_cur_max()) + 1);
    int retval;
#ifndef DISABLE_TABCOMP
    bool list = FALSE;
#endif

    bottombars(s);

    va_start(ap, msg);
    vsnprintf(foo, (COLS - 4) * mb_cur_max(), msg, ap);
    va_end(ap);
    null_at(&foo, actual_x(foo, COLS - 4));

    retval = nanogetstr(allow_tabs, foo, curranswer,
#ifndef NANO_SMALL
		history_list,
#endif
		s
#ifndef DISABLE_TABCOMP
		, &list
#endif
		);
    free(foo);
    resetstatuspos = FALSE;

    switch (retval) {
	case NANO_CANCEL_KEY:
	    retval = -1;
	    resetstatuspos = TRUE;
	    break;
	case NANO_ENTER_KEY:
	    retval = (answer[0] == '\0') ? -2 : 0;
	    resetstatuspos = TRUE;
	    break;
    }

    blank_statusbar();
    wnoutrefresh(bottomwin);

#ifdef DEBUG
    fprintf(stderr, "answer = \"%s\"\n", answer);
#endif

#ifndef DISABLE_TABCOMP
    /* If we've done tab completion, there might be a list of filename
     * matches on the edit window at this point.  Make sure that they're
     * cleared off. */
    if (list)
	edit_refresh();
#endif

    return retval;
}

void statusq_abort(void)
{
    resetstatuspos = TRUE;
}

void titlebar(const char *path)
{
    int space = COLS;
	/* The space we have available for display. */
    size_t verlen = strlenpt(VERMSG) + 1;
	/* The length of the version message in columns, plus one for
	 * padding. */
    const char *prefix;
	/* "DIR:", "File:", or "New Buffer".  Goes before filename. */
    size_t prefixlen;
	/* The length of the prefix in columns, plus one for padding. */
    const char *state;
	/* "Modified", "View", or "".  Shows the state of this
	 * buffer. */
    size_t statelen = 0;
	/* The length of the state in columns, or the length of
	 * "Modified" if the state is blank. */
    char *exppath = NULL;
	/* The file name, expanded for display. */
    bool newfie = FALSE;
	/* Do we say "New Buffer"? */
    bool dots = FALSE;
	/* Do we put an ellipsis before the path? */

    assert(path != NULL || openfile->filename != NULL);
    assert(COLS >= 0);

    wattron(topwin, A_REVERSE);
    blank_titlebar();

    /* space has to be at least 4: two spaces before the version message,
     * at least one character of the version message, and one space
     * after the version message. */
    if (space < 4)
	space = 0;
    else {
	/* Limit verlen to 1/3 the length of the screen in columns,
	 * minus three columns for spaces. */
	if (verlen > (COLS / 3) - 3)
	    verlen = (COLS / 3) - 3;
    }

    if (space >= 4) {
	/* Add a space after the version message, and account for both
	 * it and the two spaces before it. */
	mvwaddnstr(topwin, 0, 2, VERMSG, actual_x(VERMSG, verlen));
	verlen += 3;

	/* Account for the full length of the version message. */
	space -= verlen;
    }

#ifndef DISABLE_BROWSER
    /* Don't display the state if we're in the file browser. */
    if (path != NULL)
	state = "";
    else
#endif
	state = openfile->modified ? _("Modified") : ISSET(VIEW_MODE) ?
		_("View") : "";

    statelen = strlenpt((state[0] != '\0') ? state : _("Modified"));

    /* If possible, add a space before state. */
    if (space > 0 && statelen < space)
	statelen++;
    else
	goto the_end;

#ifndef DISABLE_BROWSER
    /* path should be a directory if we're in the file browser. */
    if (path != NULL)
	prefix = _("DIR:");
    else
#endif
    if (openfile->filename[0] == '\0') {
	prefix = _("New Buffer");
	newfie = TRUE;
    } else
	prefix = _("File:");

    prefixlen = strnlenpt(prefix, space - statelen) + 1;

    /* If newfie is FALSE, add a space after prefix. */
    if (!newfie && prefixlen + statelen < space)
	prefixlen++;

    /* If we're not in the file browser, path should be the current
     * filename. */
    if (path == NULL)
	path = openfile->filename;

    /* Account for the full lengths of the prefix and the state. */
    if (space >= prefixlen + statelen)
	space -= prefixlen + statelen;
    else
	space = 0;
	/* space is now the room we have for the filename. */

    if (!newfie) {
	size_t lenpt = strlenpt(path), start_col;

	dots = (lenpt >= space);

	if (dots) {
	    start_col = lenpt - space + 3;
	    space -= 3;
	} else
	    start_col = 0;

	exppath = display_string(path, start_col, space, FALSE);
    }

    if (!dots) {
	size_t exppathlen = newfie ? 0 : strlenpt(exppath);
	    /* The length of the expanded filename. */

	/* There is room for the whole filename, so we center it. */
	mvwaddnstr(topwin, 0, verlen + ((space - exppathlen) / 3),
		prefix, actual_x(prefix, prefixlen));
	if (!newfie) {
	    waddch(topwin, ' ');
	    waddstr(topwin, exppath);
	}
    } else {
	/* We will say something like "File: ...ename". */
	mvwaddnstr(topwin, 0, verlen - 1, prefix, actual_x(prefix,
		prefixlen));
	if (space <= -3 || newfie)
	    goto the_end;
	waddch(topwin, ' ');
	waddnstr(topwin, "...", space + 3);
	if (space <= 0)
	    goto the_end;
	waddstr(topwin, exppath);
    }

  the_end:
    free(exppath);

    if (state[0] != '\0') {
	if (COLS <= 1 || statelen >= COLS - 1)
	    mvwaddnstr(topwin, 0, 0, state, actual_x(state, COLS));
	else {
	    assert(COLS - statelen - 1 >= 0);

	    mvwaddnstr(topwin, 0, COLS - statelen - 1, state,
		actual_x(state, statelen));
	}
    }

    wattroff(topwin, A_REVERSE);

    wnoutrefresh(topwin);
    reset_cursor();
    wnoutrefresh(edit);
}

/* Set the modified flag if it isn't already set, and then update the
 * titlebar. */
void set_modified(void)
{
    if (!openfile->modified) {
	openfile->modified = TRUE;
	titlebar(NULL);
    }
}

/* Display a message on the statusbar, and set disable_cursorpos to
 * TRUE, so that the message won't be immediately overwritten if
 * constant cursor position display is on. */
void statusbar(const char *msg, ...)
{
    va_list ap;

    va_start(ap, msg);

    /* Curses mode is turned off.  If we use wmove() now, it will muck
     * up the terminal settings.  So we just use vfprintf(). */
    if (curses_ended) {
	vfprintf(stderr, msg, ap);
	va_end(ap);
	return;
    }

    /* Blank out the line. */
    blank_statusbar();

    if (COLS >= 4) {
	char *bar, *foo;
	size_t start_x = 0, foo_len;
#if !defined(NANO_SMALL) && defined(ENABLE_NANORC)
	bool old_whitespace = ISSET(WHITESPACE_DISPLAY);

	UNSET(WHITESPACE_DISPLAY);
#endif
	bar = charalloc(mb_cur_max() * (COLS - 3));
	vsnprintf(bar, mb_cur_max() * (COLS - 3), msg, ap);
	va_end(ap);
	foo = display_string(bar, 0, COLS - 4, FALSE);
#if !defined(NANO_SMALL) && defined(ENABLE_NANORC)
	if (old_whitespace)
	    SET(WHITESPACE_DISPLAY);
#endif
	free(bar);
	foo_len = strlenpt(foo);
	start_x = (COLS - foo_len - 4) / 2;

	wmove(bottomwin, 0, start_x);
	wattron(bottomwin, A_REVERSE);

	waddstr(bottomwin, "[ ");
	waddstr(bottomwin, foo);
	free(foo);
	waddstr(bottomwin, " ]");
	wattroff(bottomwin, A_REVERSE);
	wnoutrefresh(bottomwin);
	reset_cursor();
	wnoutrefresh(edit);
	    /* Leave the cursor at its position in the edit window, not
	     * in the statusbar. */
    }

    disable_cursorpos = TRUE;

    /* If we're doing quick statusbar blanking, and constant cursor
     * position display is off, blank the statusbar after only one
     * keystroke.  Otherwise, blank it after twenty-five keystrokes,
     * as Pico does. */
    statusblank =
#ifndef NANO_SMALL
	ISSET(QUICK_BLANK) && !ISSET(CONST_UPDATE) ? 1 :
#endif
	25;
}

void bottombars(const shortcut *s)
{
    size_t i, colwidth, slen;

    if (ISSET(NO_HELP))
	return;

    if (s == main_list) {
	slen = MAIN_VISIBLE;

	assert(slen <= length_of_list(s));
    } else {
	slen = length_of_list(s);

	/* Don't show any more shortcuts than the main list does. */
	if (slen > MAIN_VISIBLE)
	    slen = MAIN_VISIBLE;
    }

    /* There will be this many characters per column.  We need at least
     * 3 to display anything properly. */
    colwidth = COLS / ((slen / 2) + (slen % 2));

    blank_bottombars();

    for (i = 0; i < slen; i++, s = s->next) {
	const char *keystr;
	char foo[4] = "";

	/* Yucky sentinel values that we can't handle a better way. */
	if (s->ctrlval == NANO_CONTROL_SPACE)
	    strcpy(foo, "^ ");
	else if (s->ctrlval == NANO_CONTROL_8)
	    strcpy(foo, "^?");
	/* Normal values.  Assume that the shortcut has an equivalent
	 * control key, meta key sequence, or both. */
	else if (s->ctrlval != NANO_NO_KEY)
	    sprintf(foo, "^%c", s->ctrlval + 64);
	else if (s->metaval != NANO_NO_KEY)
	    sprintf(foo, "M-%c", toupper(s->metaval));

	keystr = foo;

	wmove(bottomwin, 1 + i % 2, (i / 2) * colwidth);
	onekey(keystr, s->desc, colwidth);
    }

    wnoutrefresh(bottomwin);
    reset_cursor();
    wnoutrefresh(edit);
}

/* Write a shortcut key to the help area at the bottom of the window.
 * keystroke is e.g. "^G" and desc is e.g. "Get Help".  We are careful
 * to write at most len characters, even if len is very small and
 * keystroke and desc are long.  Note that waddnstr(,,(size_t)-1) adds
 * the whole string!  We do not bother padding the entry with blanks. */
void onekey(const char *keystroke, const char *desc, size_t len)
{
    size_t keystroke_len = strlenpt(keystroke) + 1;

    assert(keystroke != NULL && desc != NULL);

    wattron(bottomwin, A_REVERSE);
    waddnstr(bottomwin, keystroke, actual_x(keystroke, len));
    wattroff(bottomwin, A_REVERSE);

    if (len > keystroke_len)
	len -= keystroke_len;
    else
	len = 0;

    if (len > 0) {
	waddch(bottomwin, ' ');
	waddnstr(bottomwin, desc, actual_x(desc, len));
    }
}

/* nano scrolls horizontally within a line in chunks.  This function
 * returns the column number of the first character displayed in the
 * window when the cursor is at the given column.  Note that
 * 0 <= column - get_page_start(column) < COLS. */
size_t get_page_start(size_t column)
{
    assert(COLS > 0);

    if (column == 0 || column < COLS - 1)
	return 0;
    else if (COLS > 9)
	return column - 7 - (column - 7) % (COLS - 8);
    else if (COLS > 2)
	return column - (COLS - 2);
    else
	return column - (COLS - 1);
}

/* Resets current_y, based on the position of current, and puts the
 * cursor in the edit window at (current_y, current_x). */
void reset_cursor(void)
{
    /* If we haven't opened any files yet, put the cursor in the top
     * left corner of the edit window and get out. */
    if (openfile == NULL) {
	wmove(edit, 0, 0);
	return;
    }

    openfile->current_y = openfile->current->lineno -
	openfile->edittop->lineno;
    if (openfile->current_y < editwinrows) {
	size_t x = xplustabs();
	wmove(edit, openfile->current_y, x - get_page_start(x));
     }
}

/* edit_add() takes care of the job of actually painting a line into the
 * edit window.  fileptr is the line to be painted, at row yval of the
 * window.  converted is the actual string to be written to the window,
 * with tabs and control characters replaced by strings of regular
 * characters.  start is the column number of the first character of
 * this page.  That is, the first character of converted corresponds to
 * character number actual_x(fileptr->data, start) of the line. */
void edit_add(const filestruct *fileptr, const char *converted, int
	yval, size_t start)
{
#if !defined(NANO_SMALL) || defined(ENABLE_COLOR)
    size_t startpos = actual_x(fileptr->data, start);
	/* The position in fileptr->data of the leftmost character
	 * that displays at least partially on the window. */
    size_t endpos = actual_x(fileptr->data, start + COLS - 1) + 1;
	/* The position in fileptr->data of the first character that is
	 * completely off the window to the right.
	 *
	 * Note that endpos might be beyond the null terminator of the
	 * string. */
#endif

    assert(openfile != NULL && fileptr != NULL && converted != NULL);
    assert(strlenpt(converted) <= COLS);

    /* Just paint the string in any case (we'll add color or reverse on
     * just the text that needs it). */
    mvwaddstr(edit, yval, 0, converted);

#ifdef ENABLE_COLOR
    /* If color syntaxes are available and turned on, we need to display
     * them. */
    if (openfile->colorstrings != NULL && !ISSET(NO_COLOR_SYNTAX)) {
	const colortype *tmpcolor = openfile->colorstrings;

	for (; tmpcolor != NULL; tmpcolor = tmpcolor->next) {
	    int x_start;
		/* Starting column for mvwaddnstr.  Zero-based. */
	    int paintlen;
		/* Number of chars to paint on this line.  There are COLS
		 * characters on a whole line. */
	    size_t index;
		/* Index in converted where we paint. */
	    regmatch_t startmatch;
		/* Match position for start_regex. */
	    regmatch_t endmatch;
		/* Match position for end_regex. */

	    if (tmpcolor->bright)
		wattron(edit, A_BOLD);
	    wattron(edit, COLOR_PAIR(tmpcolor->pairnum));
	    /* Two notes about regexec().  Return value 0 means there is
	     * a match.  Also, rm_eo is the first non-matching character
	     * after the match. */

	    /* First case, tmpcolor is a single-line expression. */
	    if (tmpcolor->end == NULL) {
		size_t k = 0;

		/* We increment k by rm_eo, to move past the end of the
		 * last match.  Even though two matches may overlap, we
		 * want to ignore them, so that we can highlight
		 * C-strings correctly. */
		while (k < endpos) {
		    /* Note the fifth parameter to regexec().  It says
		     * not to match the beginning-of-line character
		     * unless k is 0.  If regexec() returns REG_NOMATCH,
		     * there are no more matches in the line. */
		    if (regexec(tmpcolor->start, &fileptr->data[k], 1,
			&startmatch, (k == 0) ? 0 : REG_NOTBOL) ==
			REG_NOMATCH)
			break;
		    /* Translate the match to the beginning of the
		     * line. */
		    startmatch.rm_so += k;
		    startmatch.rm_eo += k;
		    if (startmatch.rm_so == startmatch.rm_eo) {
			startmatch.rm_eo++;
			statusbar(
				_("Refusing zero-length regex match"));
		    } else if (startmatch.rm_so < endpos &&
			startmatch.rm_eo > startpos) {
			if (startmatch.rm_so <= startpos)
			    x_start = 0;
			else
			    x_start = strnlenpt(fileptr->data,
				startmatch.rm_so) - start;

			index = actual_x(converted, x_start);

			paintlen = actual_x(converted + index,
				strnlenpt(fileptr->data,
				startmatch.rm_eo) - start - x_start);

			assert(0 <= x_start && 0 <= paintlen);

			mvwaddnstr(edit, yval, x_start,
				converted + index, paintlen);
		    }
		    k = startmatch.rm_eo;
		}
	    } else {
		/* This is a multi-line regex.  There are two steps.
		 * First, we have to see if the beginning of the line is
		 * colored by a start on an earlier line, and an end on
		 * this line or later.
		 *
		 * We find the first line before fileptr matching the
		 * start.  If every match on that line is followed by an
		 * end, then go to step two.  Otherwise, find the next
		 * line after start_line matching the end.  If that line
		 * is not before fileptr, then paint the beginning of
		 * this line. */
		const filestruct *start_line = fileptr->prev;
		    /* The first line before fileptr matching start. */
		regoff_t start_col;
		    /* Where it starts in that line. */
		const filestruct *end_line;

		while (start_line != NULL && regexec(tmpcolor->start,
			start_line->data, 1, &startmatch, 0) ==
			REG_NOMATCH) {
		    /* If there is an end on this line, there is no need
		     * to look for starts on earlier lines. */
		    if (regexec(tmpcolor->end, start_line->data, 0,
			NULL, 0) == 0)
			goto step_two;
		    start_line = start_line->prev;
		}
		/* No start found, so skip to the next step. */
		if (start_line == NULL)
		    goto step_two;
		/* Now start_line is the first line before fileptr
		 * containing a start match.  Is there a start on this
		 * line not followed by an end on this line? */
		start_col = 0;
		while (TRUE) {
		    start_col += startmatch.rm_so;
		    startmatch.rm_eo -= startmatch.rm_so;
 		    if (regexec(tmpcolor->end, start_line->data +
			start_col + startmatch.rm_eo, 0, NULL,
			(start_col + startmatch.rm_eo == 0) ? 0 :
			REG_NOTBOL) == REG_NOMATCH)
			/* No end found after this start. */
			break;
		    start_col++;
		    if (regexec(tmpcolor->start, start_line->data +
			start_col, 1, &startmatch,
			REG_NOTBOL) == REG_NOMATCH)
			/* No later start on this line. */
			goto step_two;
		}
		/* Indeed, there is a start not followed on this line by
		 * an end. */

		/* We have already checked that there is no end before
		 * fileptr and after the start.  Is there an end after
		 * the start at all?  We don't paint unterminated
		 * starts. */
		end_line = fileptr;
		while (end_line != NULL && regexec(tmpcolor->end,
			end_line->data, 1, &endmatch, 0) == REG_NOMATCH)
		    end_line = end_line->next;

		/* No end found, or it is too early. */
		if (end_line == NULL || (end_line == fileptr &&
			endmatch.rm_eo <= startpos))
		    goto step_two;

		/* Now paint the start of fileptr. */
		if (end_line != fileptr)
		    /* If the start of fileptr is on a different line
		     * from the end, paintlen is -1, meaning that
		     * everything on the line gets painted. */
		    paintlen = -1;
		else
		    /* Otherwise, paintlen is the expanded location of
		     * the end of the match minus the expanded location
		     * of the beginning of the page. */
		    paintlen = actual_x(converted,
			strnlenpt(fileptr->data, endmatch.rm_eo) -
			start);

		mvwaddnstr(edit, yval, 0, converted, paintlen);

  step_two:
		/* Second step, we look for starts on this line. */
		start_col = 0;

		while (start_col < endpos) {
		    if (regexec(tmpcolor->start, fileptr->data +
			start_col, 1, &startmatch, (start_col == 0) ?
			0 : REG_NOTBOL) == REG_NOMATCH || start_col +
			startmatch.rm_so >= endpos)
			/* No more starts on this line. */
			break;
		    /* Translate the match to be relative to the
		     * beginning of the line. */
		    startmatch.rm_so += start_col;
		    startmatch.rm_eo += start_col;

		    if (startmatch.rm_so <= startpos)
			x_start = 0;
		    else
			x_start = strnlenpt(fileptr->data,
				startmatch.rm_so) - start;

		    index = actual_x(converted, x_start);

		    if (regexec(tmpcolor->end, fileptr->data +
			startmatch.rm_eo, 1, &endmatch,
			(startmatch.rm_eo == 0) ? 0 : REG_NOTBOL) ==
			0) {
			/* Translate the end match to be relative to the
			 * beginning of the line. */
			endmatch.rm_so += startmatch.rm_eo;
			endmatch.rm_eo += startmatch.rm_eo;
			/* There is an end on this line.  But does it
			 * appear on this page, and is the match more
			 * than zero characters long? */
			if (endmatch.rm_eo > startpos &&
				endmatch.rm_eo > startmatch.rm_so) {
			    paintlen = actual_x(converted + index,
				strnlenpt(fileptr->data,
				endmatch.rm_eo) - start - x_start);

			    assert(0 <= x_start && x_start < COLS);

			    mvwaddnstr(edit, yval, x_start,
				converted + index, paintlen);
			}
		    } else {
			/* There is no end on this line.  But we haven't
			 * yet looked for one on later lines. */
			end_line = fileptr->next;

			while (end_line != NULL &&
				regexec(tmpcolor->end, end_line->data,
				0, NULL, 0) == REG_NOMATCH)
			    end_line = end_line->next;

			if (end_line != NULL) {
			    assert(0 <= x_start && x_start < COLS);

			    mvwaddnstr(edit, yval, x_start,
				converted + index, -1);
			    /* We painted to the end of the line, so
			     * don't bother checking any more starts. */
			    break;
			}
		    }
		    start_col = startmatch.rm_so + 1;
		}
	    }

	    wattroff(edit, A_BOLD);
	    wattroff(edit, COLOR_PAIR(tmpcolor->pairnum));
	}
    }
#endif /* ENABLE_COLOR */

#ifndef NANO_SMALL
    /* If the mark is on, we need to display it. */
    if (openfile->mark_set && (fileptr->lineno <=
	openfile->mark_begin->lineno || fileptr->lineno <=
	openfile->current->lineno) && (fileptr->lineno >=
	openfile->mark_begin->lineno || fileptr->lineno >=
	openfile->current->lineno)) {
	/* fileptr is at least partially selected. */
	const filestruct *top;
	    /* Either current or mark_begin, whichever is first. */
	size_t top_x;
	    /* current_x or mark_begin_x, corresponding to top. */
	const filestruct *bot;
	size_t bot_x;
	int x_start;
	    /* Starting column for mvwaddnstr().  Zero-based. */
	int paintlen;
	    /* Number of chars to paint on this line.  There are COLS
	     * characters on a whole line. */
	size_t index;
	    /* Index in converted where we paint. */

	mark_order(&top, &top_x, &bot, &bot_x, NULL);

	if (top->lineno < fileptr->lineno || top_x < startpos)
	    top_x = startpos;
	if (bot->lineno > fileptr->lineno || bot_x > endpos)
	    bot_x = endpos;

	/* The selected bit of fileptr is on this page. */
	if (top_x < endpos && bot_x > startpos) {
	    assert(startpos <= top_x);

	    /* x_start is the expanded location of the beginning of the
	     * mark minus the beginning of the page. */
	    x_start = strnlenpt(fileptr->data, top_x) - start;

	    if (bot_x >= endpos)
		/* If the end of the mark is off the page, paintlen is
		 * -1, meaning that everything on the line gets
		 * painted. */
		paintlen = -1;
	    else
		/* Otherwise, paintlen is the expanded location of the
		 * end of the mark minus the expanded location of the
		 * beginning of the mark. */
		paintlen = strnlenpt(fileptr->data, bot_x) -
			(x_start + start);

	    /* If x_start is before the beginning of the page, shift
	     * paintlen x_start characters to compensate, and put
	     * x_start at the beginning of the page. */
	    if (x_start < 0) {
		paintlen += x_start;
		x_start = 0;
	    }

	    assert(x_start >= 0 && x_start <= strlen(converted));

	    index = actual_x(converted, x_start);

	    if (paintlen > 0)
		paintlen = actual_x(converted + index, paintlen);

	    wattron(edit, A_REVERSE);
	    mvwaddnstr(edit, yval, x_start, converted + index,
		paintlen);
	    wattroff(edit, A_REVERSE);
	}
    }
#endif /* !NANO_SMALL */
}

/* Just update one line in the edit buffer.  This is basically a wrapper
 * for edit_add().
 *
 * If fileptr != current, then index is considered 0.  The line will be
 * displayed starting with fileptr->data[index].  Likely args are
 * current_x or 0. */
void update_line(const filestruct *fileptr, size_t index)
{
    int line;
	/* The line in the edit window that we want to update. */
    char *converted;
	/* fileptr->data converted to have tabs and control characters
	 * expanded. */
    size_t page_start;

    assert(fileptr != NULL);

    line = fileptr->lineno - openfile->edittop->lineno;

    /* We assume the line numbers are valid.  Is that really true? */
    assert(line < 0 || line == check_linenumbers(fileptr));

    if (line < 0 || line >= editwinrows)
	return;

    /* First, blank out the line. */
    blank_line(edit, line, 0, COLS);

    /* Next, convert variables that index the line to their equivalent
     * positions in the expanded line. */
    index = (fileptr == openfile->current) ? strnlenpt(fileptr->data,
	index) : 0;
    page_start = get_page_start(index);

    /* Expand the line, replacing tabs with spaces, and control
     * characters with their displayed forms. */
    converted = display_string(fileptr->data, page_start, COLS, TRUE);

    /* Paint the line. */
    edit_add(fileptr, converted, line, page_start);
    free(converted);

    if (page_start > 0)
	mvwaddch(edit, line, 0, '$');
    if (strlenpt(fileptr->data) > page_start + COLS)
	mvwaddch(edit, line, COLS - 1, '$');
}

/* Return TRUE if we need an update after moving horizontally, and FALSE
 * otherwise.  We need one if the mark is on or if old_pww and
 * placewewant are on different pages. */
bool need_horizontal_update(size_t old_pww)
{
    return
#ifndef NANO_SMALL
	openfile->mark_set ||
#endif
	get_page_start(old_pww) !=
	get_page_start(openfile->placewewant);
}

/* Return TRUE if we need an update after moving vertically, and FALSE
 * otherwise.  We need one if the mark is on or if old_pww and
 * placewewant are on different pages. */
bool need_vertical_update(size_t old_pww)
{
    return
#ifndef NANO_SMALL
	openfile->mark_set ||
#endif
	get_page_start(old_pww) !=
	get_page_start(openfile->placewewant);
}

/* Scroll the edit window in the given direction and the given number
 * of lines, and draw new lines on the blank lines left after the
 * scrolling.  direction is the direction to scroll, either UP or DOWN,
 * and nlines is the number of lines to scroll.  We change edittop, and
 * assume that current and current_x are up to date.  We also assume
 * that scrollok(edit) is FALSE. */
void edit_scroll(scroll_dir direction, ssize_t nlines)
{
    bool do_redraw = need_vertical_update(0);
    const filestruct *foo;
    ssize_t i;

    /* Don't bother scrolling less than one line. */
    if (nlines < 1)
	return;

    /* Part 1: nlines is the number of lines we're going to scroll the
     * text of the edit window. */

    /* Move the top line of the edit window up or down (depending on the
     * value of direction) nlines lines, or as many lines as we can if
     * there are fewer than nlines lines available. */
    for (i = nlines; i > 0; i--) {
	if (direction == UP) {
	    if (openfile->edittop->prev == NULL)
		break;
	    openfile->edittop = openfile->edittop->prev;
	} else {
	    if (openfile->edittop->next == NULL)
		break;
	    openfile->edittop = openfile->edittop->next;
	}
    }

    /* Limit nlines to the number of lines we could scroll. */
    nlines -= i;

    /* Don't bother scrolling zero lines or more than the number of
     * lines in the edit window minus one; in both cases, get out, and
     * in the latter case, call edit_refresh() beforehand. */
    if (nlines == 0)
	return;

    if (nlines >= editwinrows) {
	edit_refresh();
	return;
    }

    /* Scroll the text of the edit window up or down nlines lines,
     * depending on the value of direction. */
    scrollok(edit, TRUE);
    wscrl(edit, (direction == UP) ? -nlines : nlines);
    scrollok(edit, FALSE);

    /* Part 2: nlines is the number of lines in the scrolled region of
     * the edit window that we need to draw. */

    /* If the top or bottom line of the file is now visible in the edit
     * window, we need to draw the entire edit window. */
    if ((direction == UP && openfile->edittop == openfile->fileage) ||
	(direction == DOWN && openfile->edittop->lineno + editwinrows -
	1 >= openfile->filebot->lineno))
	nlines = editwinrows;

    /* If the scrolled region contains only one line, and the line
     * before it is visible in the edit window, we need to draw it too.
     * If the scrolled region contains more than one line, and the lines
     * before and after the scrolled region are visible in the edit
     * window, we need to draw them too. */
    nlines += (nlines == 1) ? 1 : 2;

    if (nlines > editwinrows)
	nlines = editwinrows;

    /* If we scrolled up, we're on the line before the scrolled
     * region. */
    foo = openfile->edittop;

    /* If we scrolled down, move down to the line before the scrolled
     * region. */
    if (direction == DOWN) {
	for (i = editwinrows - nlines; i > 0 && foo != NULL; i--)
	    foo = foo->next;
    }

    /* Draw new lines on any blank lines before or inside the scrolled
     * region.  If we scrolled down and we're on the top line, or if we
     * scrolled up and we're on the bottom line, the line won't be
     * blank, so we don't need to draw it unless the mark is on or we're
     * not on the first page. */
    for (i = nlines; i > 0 && foo != NULL; i--) {
	if ((i == nlines && direction == DOWN) || (i == 1 &&
		direction == UP)) {
	    if (do_redraw)
		update_line(foo, (foo == openfile->current) ?
			openfile->current_x : 0);
	} else
	    update_line(foo, (foo == openfile->current) ?
		openfile->current_x : 0);
	foo = foo->next;
    }
}

/* Update any lines between old_current and current that need to be
 * updated.  Use this if we've moved without changing any text. */
void edit_redraw(const filestruct *old_current, size_t old_pww)
{
    bool do_redraw = need_vertical_update(0) ||
	need_vertical_update(old_pww);
    const filestruct *foo;

    /* If either old_current or current is offscreen, scroll the edit
     * window until it's onscreen and get out. */
    if (old_current->lineno < openfile->edittop->lineno ||
	old_current->lineno >= openfile->edittop->lineno +
	editwinrows || openfile->current->lineno <
	openfile->edittop->lineno || openfile->current->lineno >=
	openfile->edittop->lineno + editwinrows) {
	filestruct *old_edittop = openfile->edittop;
	ssize_t nlines;

	/* Put edittop in range of current, get the difference in lines
	 * between the original edittop and the current edittop, and
	 * then restore the original edittop. */
	edit_update(
#ifndef NANO_SMALL
		ISSET(SMOOTH_SCROLL) ? NONE :
#endif
		CENTER);

	nlines = openfile->edittop->lineno - old_edittop->lineno;

	openfile->edittop = old_edittop;

	/* Scroll the edit window up or down until edittop is in range
	 * of current. */
	if (nlines < 0)
	    edit_scroll(UP, -nlines);
	else
	    edit_scroll(DOWN, nlines);

	return;
    }

    /* Update old_current and current if we're not on the first page
     * and/or we're not on the same page as before.  If the mark is on,
     * update all the lines between old_current and current too. */
    foo = old_current;

    while (foo != openfile->current) {
	if (do_redraw)
	    update_line(foo, 0);

#ifndef NANO_SMALL
	if (!openfile->mark_set)
#endif
	    break;

#ifndef NANO_SMALL
	foo = (foo->lineno > openfile->current->lineno) ? foo->prev :
		foo->next;
#endif
    }

    if (do_redraw)
	update_line(openfile->current, openfile->current_x);
}

/* Refresh the screen without changing the position of lines.  Use this
 * if we've moved and changed text. */
void edit_refresh(void)
{
    const filestruct *foo;
    int nlines;

    if (openfile->current->lineno < openfile->edittop->lineno ||
	openfile->current->lineno >= openfile->edittop->lineno +
	editwinrows)
	/* Put the top line of the edit window in range of the current
	 * line. */
	edit_update(
#ifndef NANO_SMALL
		ISSET(SMOOTH_SCROLL) ? NONE :
#endif
		CENTER);

    foo = openfile->edittop;

#ifdef DEBUG
    fprintf(stderr, "edit_refresh(): edittop->lineno = %ld\n", (long)openfile->edittop->lineno);
#endif

    for (nlines = 0; nlines < editwinrows && foo != NULL; nlines++) {
	update_line(foo, (foo == openfile->current) ?
		openfile->current_x : 0);
	foo = foo->next;
    }

    for (; nlines < editwinrows; nlines++)
	blank_line(edit, nlines, 0, COLS);

    reset_cursor();
    wnoutrefresh(edit);
}

/* Move edittop to put it in range of current, keeping current in the
 * same place.  location determines how we move it: if it's CENTER, we
 * center current, and if it's NONE, we put current current_y lines
 * below edittop. */
void edit_update(update_type location)
{
    filestruct *foo = openfile->current;
    int goal;

    /* If location is CENTER, we move edittop up (editwinrows / 2)
     * lines.  This puts current at the center of the screen.  If
     * location is NONE, we move edittop up current_y lines if current_y
     * is in range of the screen, 0 lines if current_y is less than 0,
     * or (editwinrows - 1) lines if current_y is greater than
     * (editwinrows - 1).  This puts current at the same place on the
     * screen as before, or at the top or bottom of the screen if
     * edittop is beyond either. */
    if (location == CENTER)
	goal = editwinrows / 2;
    else {
	goal = openfile->current_y;

	/* Limit goal to (editwinrows - 1) lines maximum. */
	if (goal > editwinrows - 1)
	    goal = editwinrows - 1;
    }

    for (; goal > 0 && foo->prev != NULL; goal--)
	foo = foo->prev;

    openfile->edittop = foo;
}

/* Ask a simple yes/no question, specified in msg, on the statusbar.
 * Return 1 for Y, 0 for N, 2 for All (if all is TRUE when passed in)
 * and -1 for abort (^C). */
int do_yesno(bool all, const char *msg)
{
    int ok = -2, width = 16;
    const char *yesstr;		/* String of yes characters accepted. */
    const char *nostr;		/* Same for no. */
    const char *allstr;		/* And all, surprise! */

    assert(msg != NULL);

    /* yesstr, nostr, and allstr are strings of any length.  Each string
     * consists of all single-byte characters accepted as valid
     * characters for that value.  The first value will be the one
     * displayed in the shortcuts.  Translators: if possible, specify
     * both the shortcuts for your language and English.  For example,
     * in French: "OoYy" for "Oui". */
    yesstr = _("Yy");
    nostr = _("Nn");
    allstr = _("Aa");

    if (!ISSET(NO_HELP)) {
	char shortstr[3];		/* Temp string for Y, N, A. */

	if (COLS < 32)
	    width = COLS / 2;

	/* Write the bottom of the screen. */
	blank_bottombars();

	sprintf(shortstr, " %c", yesstr[0]);
	wmove(bottomwin, 1, 0);
	onekey(shortstr, _("Yes"), width);

	if (all) {
	    wmove(bottomwin, 1, width);
	    shortstr[1] = allstr[0];
	    onekey(shortstr, _("All"), width);
	}

	wmove(bottomwin, 2, 0);
	shortstr[1] = nostr[0];
	onekey(shortstr, _("No"), width);

	wmove(bottomwin, 2, 16);
	onekey("^C", _("Cancel"), width);
    }

    wattron(bottomwin, A_REVERSE);

    blank_statusbar();
    mvwaddnstr(bottomwin, 0, 0, msg, actual_x(msg, COLS - 1));

    wattroff(bottomwin, A_REVERSE);

    /* Refresh the edit window and the statusbar before getting
     * input. */
    wnoutrefresh(edit);
    wnoutrefresh(bottomwin);

    do {
	int kbinput;
	bool meta_key, func_key;
#ifndef DISABLE_MOUSE
	int mouse_x, mouse_y;
#endif

	kbinput = get_kbinput(bottomwin, &meta_key, &func_key);

	if (kbinput == NANO_REFRESH_KEY) {
	    total_redraw();
	    continue;
	} else if (kbinput == NANO_CANCEL_KEY)
	    ok = -1;
#ifndef DISABLE_MOUSE
	else if (kbinput == KEY_MOUSE) {
	    get_mouseinput(&mouse_x, &mouse_y, FALSE);

	    if (mouse_x != -1 && mouse_y != -1 && !ISSET(NO_HELP) &&
		wenclose(bottomwin, mouse_y, mouse_x) &&
		mouse_x < (width * 2) && mouse_y >= editwinrows + 3) {
		int x = mouse_x / width;
		    /* Did we click in the first column of shortcuts, or
		     * the second? */
		int y = mouse_y - editwinrows - 3;
		    /* Did we click in the first row of shortcuts? */

		assert(0 <= x && x <= 1 && 0 <= y && y <= 1);

		/* x = 0 means they clicked Yes or No.
		 * y = 0 means Yes or All. */
		ok = -2 * x * y + x - y + 1;

		if (ok == 2 && !all)
		    ok = -2;
	    }
	}
#endif
	/* Look for the kbinput in the yes, no and (optionally) all
	 * strings. */
	else if (strchr(yesstr, kbinput) != NULL)
	    ok = 1;
	else if (strchr(nostr, kbinput) != NULL)
	    ok = 0;
	else if (all && strchr(allstr, kbinput) != NULL)
	    ok = 2;
    } while (ok == -2);

    return ok;
}

void total_redraw(void)
{
#ifdef USE_SLANG
    /* Slang curses emulation brain damage, part 3: Slang doesn't define
     * curscr. */
    SLsmg_touch_screen();
    SLsmg_refresh();
#else
    wrefresh(curscr);
#endif
}

void total_refresh(void)
{
    total_redraw();
    titlebar(NULL);
    edit_refresh();
    bottombars(currshortcut);
}

void display_main_list(void)
{
    bottombars(main_list);
}

/* If constant is TRUE, we display the current cursor position only if
 * disable_cursorpos is FALSE.  Otherwise, we display it
 * unconditionally and set disable_cursorpos to FALSE.  If constant is
 * TRUE and disable_cursorpos is TRUE, we also set disable_cursorpos to
 * FALSE, so that we leave the current statusbar alone this time, and
 * display the current cursor position next time. */
void do_cursorpos(bool constant)
{
    filestruct *f;
    char c;
    size_t i, cur_xpt = xplustabs() + 1;
    size_t cur_lenpt = strlenpt(openfile->current->data) + 1;
    int linepct, colpct, charpct;

    assert(openfile->fileage != NULL && openfile->current != NULL);

    f = openfile->current->next;
    c = openfile->current->data[openfile->current_x];

    openfile->current->next = NULL;
    openfile->current->data[openfile->current_x] = '\0';

    i = get_totsize(openfile->fileage, openfile->current);

    openfile->current->data[openfile->current_x] = c;
    openfile->current->next = f;

    /* Check whether totsize is correct.  If it isn't, there is a bug
     * somewhere. */
    assert(openfile->current != openfile->filebot || i == openfile->totsize);

    if (constant && disable_cursorpos) {
	disable_cursorpos = FALSE;
	return;
    }

    /* Display the current cursor position on the statusbar, and set 
     * disable_cursorpos to FALSE. */
    linepct = 100 * openfile->current->lineno /
	openfile->filebot->lineno;
    colpct = 100 * cur_xpt / cur_lenpt;
    charpct = (openfile->totsize == 0) ? 0 : 100 * i /
	openfile->totsize;

    statusbar(
	_("line %ld/%ld (%d%%), col %lu/%lu (%d%%), char %lu/%lu (%d%%)"),
	(long)openfile->current->lineno,
	(long)openfile->filebot->lineno, linepct,
	(unsigned long)cur_xpt, (unsigned long)cur_lenpt, colpct,
	(unsigned long)i, (unsigned long)openfile->totsize, charpct);

    disable_cursorpos = FALSE;
}

void do_cursorpos_void(void)
{
    do_cursorpos(FALSE);
}

#ifndef DISABLE_HELP
/* Calculate the next line of help_text, starting at ptr. */
size_t help_line_len(const char *ptr)
{
    int help_cols = (COLS > 24) ? COLS - 8 : 24;

    /* Try to break the line at (COLS - 8) columns if we have more than
     * 24 columns, and at 24 columns otherwise. */
    size_t retval = break_line(ptr, help_cols, TRUE);
    size_t retval_save = retval;

    /* Get the length of the entire line up to a null or a newline. */
    while (*(ptr + retval) != '\0' && *(ptr + retval) != '\n')
	retval += move_mbright(ptr + retval, 0);

    /* If the entire line doesn't go more than 8 columns beyond where we
     * tried to break it, we should display it as-is.  Otherwise, we
     * should display it only up to the break. */
    if (strnlenpt(ptr, retval) > help_cols + 8)
	retval = retval_save;

    return retval;
}

/* Our dynamic, shortcut-list-compliant help function. */
void do_help(void)
{
    int line = 0;
	/* The line number in help_text of the first displayed help
	 * line.  This variable is zero-based. */
    bool no_more = FALSE;
	/* no_more means the end of the help text is shown, so don't go
	 * down any more. */
    int kbinput = ERR;
    bool meta_key, func_key;

    bool old_no_help = ISSET(NO_HELP);
#ifndef DISABLE_MOUSE
    const shortcut *oldshortcut = currshortcut;
	/* We will set currshortcut to allow clicking on the help
	 * screen's shortcut list. */
#endif

    curs_set(0);
    blank_edit();
    wattroff(bottomwin, A_REVERSE);
    blank_statusbar();

    /* Set help_text as the string to display. */
    help_init();

    assert(help_text != NULL);

#ifndef DISABLE_MOUSE
    /* Set currshortcut to allow clicking on the help screen's shortcut
     * list, AFTER help_init(). */
    currshortcut = help_list;
#endif

    if (ISSET(NO_HELP)) {
	/* Make sure that the help screen's shortcut list will actually
	 * be displayed. */
	UNSET(NO_HELP);
	window_init();
    }

    bottombars(help_list);

    do {
	int i;
	int old_line = line;
	    /* We redisplay the help only if it moved. */
	const char *ptr = help_text;

	switch (kbinput) {
#ifndef DISABLE_MOUSE
	    case KEY_MOUSE:
		{
		    int mouse_x, mouse_y;
		    get_mouseinput(&mouse_x, &mouse_y, TRUE);
		}
		break;
#endif
	    case NANO_PREVPAGE_KEY:
	    case NANO_PREVPAGE_FKEY:
		if (line > 0) {
		    line -= editwinrows - 2;
		    if (line < 0)
			line = 0;
		}
		break;
	    case NANO_NEXTPAGE_KEY:
	    case NANO_NEXTPAGE_FKEY:
		if (!no_more)
		    line += editwinrows - 2;
		break;
	    case NANO_PREVLINE_KEY:
		if (line > 0)
		    line--;
		break;
	    case NANO_NEXTLINE_KEY:
		if (!no_more)
		    line++;
		break;
	}

	if (kbinput == NANO_REFRESH_KEY)
	    total_redraw();
	else {
	    if (line == old_line && kbinput != ERR)
		goto skip_redisplay;

	    blank_edit();
	}

	/* Calculate where in the text we should be, based on the
	 * page. */
	for (i = 0; i < line; i++) {
	    ptr += help_line_len(ptr);
	    if (*ptr == '\n')
		ptr++;
	}

	for (i = 0; i < editwinrows && *ptr != '\0'; i++) {
	    size_t j = help_line_len(ptr);

	    mvwaddnstr(edit, i, 0, ptr, j);
	    ptr += j;
	    if (*ptr == '\n')
		ptr++;
	}
	no_more = (*ptr == '\0');

  skip_redisplay:
	kbinput = get_kbinput(edit, &meta_key, &func_key);
    } while (kbinput != NANO_EXIT_KEY && kbinput != NANO_EXIT_FKEY);

#ifndef DISABLE_MOUSE
    currshortcut = oldshortcut;
#endif

    if (old_no_help) {
	blank_bottombars();
	wnoutrefresh(bottomwin);
	SET(NO_HELP);
	window_init();
    } else
	bottombars(currshortcut);

    curs_set(1);
    edit_refresh();

    /* The help_init() at the beginning allocated help_text.  Since 
     * help_text has now been written to the screen, we don't need it
     * anymore. */
    free(help_text);
    help_text = NULL;
}
#endif /* !DISABLE_HELP */

/* Highlight the current word being replaced or spell checked.  We
 * expect word to have tabs and control characters expanded. */
void do_replace_highlight(bool highlight, const char *word)
{
    size_t y = xplustabs(), word_len = strlenpt(word);

    y = get_page_start(y) + COLS - y;
	/* Now y is the number of columns that we can display on this
	 * line. */

    assert(y > 0);

    if (word_len > y)
	y--;

    reset_cursor();

    if (highlight)
	wattron(edit, A_REVERSE);

#ifdef HAVE_REGEX_H
    /* This is so we can show zero-length regexes. */
    if (word_len == 0)
	waddch(edit, ' ');
    else
#endif
	waddnstr(edit, word, actual_x(word, y));

    if (word_len > y)
	waddch(edit, '$');

    if (highlight)
	wattroff(edit, A_REVERSE);
}

#ifndef NDEBUG
/* Return what the current line number should be, starting at edittop
 * and ending at fileptr. */
int check_linenumbers(const filestruct *fileptr)
{
    int check_line = 0;
    const filestruct *filetmp;

    for (filetmp = openfile->edittop; filetmp != fileptr;
	filetmp = filetmp->next)
	check_line++;

    return check_line;
}
#endif /* !NDEBUG */

#ifdef DEBUG
/* Dump the filestruct inptr to stderr. */
void dump_filestruct(const filestruct *inptr)
{
    if (inptr == openfile->fileage)
	fprintf(stderr, "Dumping file buffer to stderr...\n");
    else if (inptr == cutbuffer)
	fprintf(stderr, "Dumping cutbuffer to stderr...\n");
    else
	fprintf(stderr, "Dumping a buffer to stderr...\n");

    while (inptr != NULL) {
	fprintf(stderr, "(%ld) %s\n", (long)inptr->lineno, inptr->data);
	inptr = inptr->next;
    }
}

/* Dump the current buffer's filestruct to stderr in reverse. */
void dump_filestruct_reverse(void)
{
    const filestruct *fileptr = openfile->filebot;

    while (fileptr != NULL) {
	fprintf(stderr, "(%ld) %s\n", (long)fileptr->lineno,
		fileptr->data);
	fileptr = fileptr->prev;
    }
}
#endif /* DEBUG */

#ifdef NANO_EXTRA
#define CREDIT_LEN 54
#define XLCREDIT_LEN 8

/* Easter egg: Display credits.  Assume nodelay(edit) and scrollok(edit)
 * are FALSE. */
void do_credits(void)
{
    bool old_more_space = ISSET(MORE_SPACE);
    bool old_no_help = ISSET(NO_HELP);
    int kbinput = ERR, crpos = 0, xlpos = 0;
    const char *credits[CREDIT_LEN] = {
	NULL,				/* "The nano text editor" */
	NULL,				/* "version" */
	VERSION,
	"",
	NULL,				/* "Brought to you by:" */
	"Chris Allegretta",
	"Jordi Mallach",
	"Adam Rogoyski",
	"Rob Siemborski",
	"Rocco Corsi",
	"David Lawrence Ramsey",
	"David Benbennick",
	"Mike Frysinger",
	"Ken Tyler",
	"Sven Guckes",
	NULL,				/* credits[15], handled below. */
	"Pauli Virtanen",
	"Daniele Medri",
	"Clement Laforet",
	"Tedi Heriyanto",
	"Bill Soudan",
	"Christian Weisgerber",
	"Erik Andersen",
	"Big Gaute",
	"Joshua Jensen",
	"Ryan Krebs",
	"Albert Chin",
	"",
	NULL,				/* "Special thanks to:" */
	"Plattsburgh State University",
	"Benet Laboratories",
	"Amy Allegretta",
	"Linda Young",
	"Jeremy Robichaud",
	"Richard Kolb II",
	NULL,				/* "The Free Software Foundation" */
	"Linus Torvalds",
	NULL,				/* "For ncurses:" */
	"Thomas Dickey",
	"Pavel Curtis",
	"Zeyd Ben-Halim",
	"Eric S. Raymond",
	NULL,				/* "and anyone else we forgot..." */
	NULL,				/* "Thank you for using nano!" */
	"",
	"",
	"",
	"",
	"(c) 1999-2005 Chris Allegretta",
	"",
	"",
	"",
	"",
	"http://www.nano-editor.org/"
    };

    const char *xlcredits[XLCREDIT_LEN] = {
	N_("The nano text editor"),
	N_("version"),
	N_("Brought to you by:"),
	N_("Special thanks to:"),
	N_("The Free Software Foundation"),
	N_("For ncurses:"),
	N_("and anyone else we forgot..."),
	N_("Thank you for using nano!")
    };

    /* credits[15]: Make sure this name is displayed properly, since we
     * can't dynamically assign it above, using Unicode 00F6 (Latin
     * Small Letter O with Diaresis) if applicable. */
    credits[15] =
#ifdef ENABLE_UTF8
	 ISSET(USE_UTF8) ? "Florian K\xC3\xB6nig" :
#endif
	"Florian K\xF6nig";

    if (!old_more_space || !old_no_help) {
	SET(MORE_SPACE);
	SET(NO_HELP);
	window_init();
    }

    curs_set(0);
    nodelay(edit, TRUE);

    blank_titlebar();
    blank_topbar();
    blank_edit();
    blank_statusbar();
    blank_bottombars();

    wrefresh(topwin);
    wrefresh(edit);
    wrefresh(bottomwin);
    napms(700);

    for (crpos = 0; crpos < CREDIT_LEN + editwinrows / 2; crpos++) {
	if ((kbinput = wgetch(edit)) != ERR)
	    break;

	if (crpos < CREDIT_LEN) {
	    const char *what;
	    size_t start_x;

	    if (credits[crpos] == NULL) {
		assert(0 <= xlpos && xlpos < XLCREDIT_LEN);

		what = _(xlcredits[xlpos]);
		xlpos++;
	    } else
		what = credits[crpos];

	    start_x = COLS / 2 - strlenpt(what) / 2 - 1;
	    mvwaddstr(edit, editwinrows - 1 - (editwinrows % 2),
		start_x, what);
	}

	wrefresh(edit);

	if ((kbinput = wgetch(edit)) != ERR)
	    break;
	napms(700);

	scrollok(edit, TRUE);
	wscrl(edit, 1);
	scrollok(edit, FALSE);
	wrefresh(edit);

	if ((kbinput = wgetch(edit)) != ERR)
	    break;
	napms(700);

	scrollok(edit, TRUE);
	wscrl(edit, 1);
	scrollok(edit, FALSE);
	wrefresh(edit);
    }

    if (kbinput != ERR)
	ungetch(kbinput);

    if (!old_more_space || !old_no_help) {
	UNSET(MORE_SPACE);
	UNSET(NO_HELP);
	window_init();
    }

    curs_set(1);
    nodelay(edit, FALSE);

    total_refresh();
}
#endif