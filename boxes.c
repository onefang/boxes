/* vi: set sw=4 ts=4:
 *
 * boxes.c - Generic editor development sandbox.
 *
 * Copyright 2012 David Seikel <won_fang@yahoo.com>
 *
 * Not in SUSv4.  An entirely new invention, thus no web site either.
 * See -
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ex.html
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/more.html
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/sed.html
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/vi.html
 * http://linux.die.net/man/1/less

USE_BOXES(NEWTOY(boxes, "w#h#m(mode):a(stickchars)1", TOYFLAG_USR|TOYFLAG_BIN))

config BOXES
	bool "boxes"
	default n
	help
	  usage: boxes [-m|--mode mode] [-a|--stickchars] [-w width] [-h height]

	  Generic text editor and pager.

	  Mode selects which editor or text viewr it emulates, the choices are -
	    emacs is a microemacs type editor.
	    joe is a joe / wordstar type editor.
	    less is a less type pager.
	    mcedit (the default) is cooledit / mcedit type editor.
	    more is a more type pager.
	    nano is a nano / pico type editor.
	    vi is a vi type editor.

	  Stick chars means to use ASCII for the boxes instead of "graphics" characters.
*/

#include "toys.h"

GLOBALS(
	char *mode;
	long h, w;
	// TODO - actually, these should be globals in the library, and leave this buffer alone.
	int stillRunning;
	int overWriteMode;
)

#define TT this.boxes

#define FLAG_a	2
#define FLAG_m	4
#define FLAG_h	8
#define FLAG_w	16

#define MEM_SIZE	128

/* This is trying to be a generic text editing, text viewing, and terminal
 * handling system.  The current code is a work in progress, and the design
 * may change.  Certainly at this moment it's only partly written.  It is
 * "usable" though, for a very small value of "usable".  In the following
 * I'll use "editors" to refer to the toys using this, though not all will
 * be editors.
 *
 * The things it is targeting are - vi and more (part of the standards, so
 * part of the toybox TODO), less (also on the toybox TODO), joe and
 * wordstar (coz Rob said they would be good to include), nano (again Rob
 * thinks it would be good and I agree), microemacs (to avoid religous
 * wars), and mcedit (coz that's what I actually use).  The ex editor comes
 * along for the ride coz vi is basically a screen editor wrapper around
 * the ex line editor.  Sed might be supported coz I'll need to do basic
 * editing functions that are common to the editors, and sed needs the same
 * editing functions.
 *
 * I will also use this for a midnight commander clone as discussed on the
 * mailing list.  This would have things in common with emacs dired, so we
 * might get that as well.  Parts of this code could also be used for a
 * file chooser, as used by some of the editors we are targeting.  Finally,
 * the terminal handling stuff might be useful for other toys, so should be
 * generic in it's own right.  Oh, screen is listed in the toybox TODO as
 * "maybe", so I'll poke at that to.
 *
 * The basic building blocks are box, content, context, edit line, and
 * view.  A box is an on screen rectanglur area.  Content is a file, and
 * the text that is in that file.  A context represents a particular editor
 * type, it has key mappings and other similar stuff.  The edit line is a
 * single line where editing happens, it's similar to readline.  A view is
 * a view into a content, there can be many, it represents that portion of
 * the content that is on screen right now.
 *
 * I plan on splitting these things up a bit so they can be used
 * separately.  Then I can make actually toybox libraries out of them.  For
 * now it's all one big file for ease of development.
 *
 * The screen is split into boxes, by default there are only two, the main
 * text area and the command line at the bottom.  Each box contains a view,
 * and each view points to a content (file) for it's text.  A content can
 * have many views.  Each content has a context (editor).  There is only
 * ever one edit line, it's the line that is being edited at the moment. 
 * The edit line moves within and between boxes (including the command
 * line) as needed.
 *
 * The justification for boxes is that most of the editors we are trying to
 * emulate include some splitting up of the screen area for various
 * reasons, and some of them include a split window system as well.  So
 * this boxes concept covers command line, main editing area, split windows,
 * menus, on screen display of command keys, file selection, and anything
 * else that might be needed.
 *
 * To keep things simple boxes are organised as a binary tree of boxes. 
 * There is a root box, it's a global.  Each box can have two sub boxes. 
 * Only the leaf nodes of the box tree are visible on the screen.  Each box
 * with sub boxes is split either horizontally or vertically.  Navigating
 * through the boxes goes depth first.
 *
 * A content keeps track of a file and it's text.  Each content also has a
 * context, which is a collection of the things that define a particular
 * editor.  (I might move the context pointer from content to view, makes
 * more sense I think.)
 *
 * A context is the heart of the generic part of the system.  Any given
 * toybox command that uses this system would basically define a context
 * and pass that to the rest of the system.  See the end of this file for a
 * few examples.  A context holds a list of command to C function mappings,
 * key to command mappings, and a list of modes.
 *
 * Most of the editors targetted include a command line where the user
 * types in editor commands, and each of those editors has different
 * commands.  They would mostly use the same editing C functions though, or
 * short wrappers around them.  The vi context at the end of this file is a
 * good example, it has a bunch of short C wrappers around some editing
 * functions, or uses standard C editing functions directly.  So a context
 * has an editor command to C function mapping.
 *
 * The editors respond to keystrokes, and those keystrokes invoke editor
 * commands, often in a modal way.  So there are keystroke to editor
 * command mappings.  To cater for editing modes, each context has a list
 * of modes, and each mode can have key to command mappings, as well as
 * menu to command mappings, and a list of displayed key/command pairs. 
 * Menu and key/command pair display is not written yet.  Most editors have
 * a system for remapping key to command mappings, that's not supported
 * yet.  Emacs has a heirarchy of key to command mappings, that's not
 * supported yet.  Some twiddling of the current design would be needed for
 * those.
 *
 * The key mappings used can be multiple keystrokes in a sequence, the
 * system caters for that.  Some can be multi byte like function keys, and
 * even different strings of bytes depending on the terminal type.  To
 * simplify this, there is a table that maps various terminals ideas of
 * special keys to key names, and the mapping of keys to commands uses
 * those key names.
 *
 * A view represents the on screen visible portion of a content within a
 * box.  To cater for split window style editors, a content can have many
 * views.  It deals with keeping track of what's shown on screen, mapping
 * the on screen representation of the text to the stored text during input
 * and output.  Each box holds one view.
 *
 * The edit line is basically a movable readline.  There are editing C
 * functions for moving it up and down lines within a view, and for
 * shifting the edit line to some other box.  So an editor would map the
 * cursor keys for "up a line" and "down a line" to these C functions for
 * example.  Actual readline style functionality is just the bottom command
 * box being a single line view, with the history file loaded into it's
 * content, and the Enter key mapped to the editor contexts "do this
 * command" function.  Using most of the system with not much special casing.
 *
 *
 * I assume that there wont be a terribly large number of boxes.
 * Things like minimum box sizes, current maximum screen sizes, and the fact
 * that they all have to be on the screen mean that this assumption should
 * be safe.  It's likely that most of the time there will be only a few at
 * most.  The point is there is a built in limit, there's only so many non
 * overlapping boxes with textual contents that you can squeeze onto one
 * terminal screen at once.
 *
 * I assume that there will only be one command line, no matter how many boxes,
 * and that the command line is for the current box.
 *
 * I use a wide screen monitor and small font.
 * My usual terminal is 104 x 380 characters.
 * There will be people with bigger monitors and smaller fonts.
 * So use sixteen bits for storing screen positions and the like.
 * Eight bits wont cut it.
 *
 * TODO - disentangle boxes from views.
 *
 * TODO - Show status line instead of command line when it's not being edited.
 *
 * TODO - should split this up into editing, UI, and boxes parts,
 * so the developer can leave out bits they are not using.
 *
 * TODO - should review it all for UTF8 readiness.  Think I can pull that off
 * by keeping everything on the output side as "screen position", and using
 * the formatter to sort out the input to output mapping.
 *
 * TODO - see if there are any simple shortcuts to avoid recalculating
 * everything all the time.  And to avoid screen redraws.
 */


struct key
{
	char *code;
	char *name;
};

// This table includes some variations I have found on some terminals, and the MC "Esc digit" versions.
// NOTE - The MC Esc variations might not be such a good idea, other programs want the Esc key for other things.
//          Notably seems that "Esc somekey" is used in place of "Alt somekey" AKA "Meta somekey" coz apparently some OSes swallow those.
//            Conversely, some terminals send "Esc somekey" when you do "Alt somekey".
//          Those MC Esc variants might be used on Macs for other things?
// TODO - Don't think I got all the linux console variations.
// TODO - Add more shift variations, plus Ctrl & Alt variations.
// TODO - Add other miscelany that does not use an escape sequence.  Including mouse events.
// TODO - Perhaps sort this for quicker searching, OR to say which terminal is which, though there is some overlap.
//          On the other hand, simple wins out over speed, and sorting by terminal type wins the simple test.
//          Plus, human typing speeds wont need binary searching speeds on this small table.
struct key keys[] =
{
	{"\x1B[3~",		"Del"},
	{"\x1B[2~",		"Ins"},
	{"\x1B[D",		"Left"},
	{"\x1BOD",		"Left"},
	{"\x1B[C",		"Right"},
	{"\x1BOC",		"Right"},
	{"\x1B[A",		"Up"},
	{"\x1BOA",		"Up"},
	{"\x1B[B",		"Down"},
	{"\x1BOB",		"Down"},
	{"\x1B\x4f\x48",	"Home"},
	{"\x1B[1~",		"Home"},
	{"\x1B[7~",		"Home"},
	{"\x1B[H",		"Home"},
	{"\x1BOH",		"Home"},
	{"\x1B\x4f\x46",	"End"},
	{"\x1B[4~",		"End"},
	{"\x1B[8~",		"End"},
	{"\x1B[F",		"End"},
	{"\x1BOF",		"End"},
	{"\x1BOw",		"End"},
	{"\x1B[5~",		"PgUp"},
	{"\x1B[6~",		"PgDn"},
	{"\x1B\x4F\x50",	"F1"},
	{"\x1B[11~",		"F1"},
	{"\x1B\x31",		"F1"},
	{"\x1BOP",		"F1"},
	{"\x1B\x4F\x51",	"F2"},
	{"\x1B[12~",		"F2"},
	{"\x1B\x32",		"F2"},
	{"\x1BOO",		"F2"},
	{"\x1B\x4F\x52",	"F3"},
	{"\x1B[13~",		"F3"},
	{"\x1B\x33~",		"F3"},
	{"\x1BOR",		"F3"},
	{"\x1B\x4F\x53",	"F4"},
	{"\x1B[14~",		"F4"},
	{"\x1B\x34",		"F4"},
	{"\x1BOS",		"F4"},
	{"\x1B[15~",		"F5"},
	{"\x1B\x35",		"F5"},
	{"\x1B[17~",		"F6"},
	{"\x1B\x36",		"F6"},
	{"\x1B[18~",		"F7"},
	{"\x1B\x37",		"F7"},
	{"\x1B[19~",		"F8"},
	{"\x1B\x38",		"F8"},
	{"\x1B[20~",		"F9"},
	{"\x1B\x39",		"F9"},
	{"\x1B[21~",		"F10"},
	{"\x1B\x30",		"F10"},
	{"\x1B[23~",		"F11"},
	{"\x1B[24~",		"F12"},
	{"\x1B\x4f\x31;2P",	"Shift F1"},
	{"\x1B[1;2P",		"Shift F1"},
	{"\x1B\x4f\x31;2Q",	"Shift F2"},
	{"\x1B[1;2Q",		"Shift F2"},
	{"\x1B\x4f\x31;2R",	"Shift F3"},
	{"\x1B[1;2R",		"Shift F3"},
	{"\x1B\x4f\x31;2S",	"Shift F4"},
	{"\x1B[1;2S",		"Shift F4"},
	{"\x1B[15;2~",		"Shift F5"},
	{"\x1B[17;2~",		"Shift F6"},
	{"\x1B[18;2~",		"Shift F7"},
	{"\x1B[19;2~",		"Shift F8"},
	{"\x1B[20;2~",		"Shift F9"},
	{"\x1B[21;2~",		"Shift F10"},
	{"\x1B[23;2~",		"Shift F11"},
	{"\x1B[24;2~",		"Shift F12"},

	// These are here for documentation, but some are mapped to particular key names.
	// The commented out control keys are handled in editLine(), or just used via "^X".
//	{"\x00",		"^@"},		// NUL
//	{"\x01",		"^A"},		// SOH
//	{"\x02",		"^B"},		// STX
//	{"\x03",		"^C"},		// ETX
//	{"\x04",		"^D"},		// EOT
//	{"\x05",		"^E"},		// ENQ
//	{"\x06",		"^F"},		// ACK
//	{"\x07",		"^G"},		// BEL
	{"\x08",		"Del"},		// BS  Delete key, usually.
	{"\x09",		"Tab"},		// HT  Tab key.
	{"\x0A",		"Return"},	// LF  Return key.  Roxterm at least is translating both Ctrl-J and Ctrl-M into this.
//	{"\x0B",		"^K"},		// VT
//	{"\x0C",		"^L"},		// FF
//	{"\x0D",		"^M"},		// CR
//	{"\x0E",		"^N"},		// SO
//	{"\x0F",		"^O"},		// SI
//	{"\x10",		"^P"},		// DLE
//	{"\x11",		"^Q"},		// DC1
//	{"\x12",		"^R"},		// DC2
//	{"\x13",		"^S"},		// DC3
//	{"\x14",		"^T"},		// DC4
//	{"\x15",		"^U"},		// NAK
//	{"\x16",		"^V"},		// SYN
//	{"\x17",		"^W"},		// ETB
//	{"\x18",		"^X"},		// CAN
//	{"\x19",		"^Y"},		// EM
//	{"\x1A",		"^Z"},		// SUB
//	{"\x1B",		"Esc"},		// ESC Esc key.  Commented out coz it's the ANSI start byte in the above multibyte keys.  Handled in the code with a timeout.
//	{"\x1C",		"^\\"},		// FS
//	{"\x1D",		"^]"},		// GS
//	{"\x1E",		"^^"},		// RS
//	{"\x1F",		"^_"},		// US
	{"\x7f",		"BS"},		// Backspace key, usually.  Ctrl-? perhaps?
	{NULL, NULL}
};

char *borderChars[][6] =
{
    {"-",    "|",    "+",    "+",    "+",    "+"},	// "stick" characters.
    {"\xE2\x94\x80", "\xE2\x94\x82", "\xE2\x94\x8C", "\xE2\x94\x90", "\xE2\x94\x94", "\xE2\x94\x98"},	// UTF-8
    {"\x71", "\x78", "\x6C", "\x6B", "\x6D", "\x6A"},	// VT100 alternate character set.
    {"\xC4", "\xB3", "\xDA", "\xBF", "\xC0", "\xD9"}	// DOS
};

char *borderCharsCurrent[][6] =
{
    {"=",    "#",    "+",    "+",    "+",    "+"},	// "stick" characters.
    {"\xE2\x95\x90", "\xE2\x95\x91", "\xE2\x95\x94", "\xE2\x95\x97", "\xE2\x95\x9A", "\xE2\x95\x9D"},	// UTF-8
    {"\x71", "\x78", "\x6C", "\x6B", "\x6D", "\x6A"},	// VT100 alternate character set has none of these.  B-(
    {"\xCD", "\xBA", "\xC9", "\xBB", "\xC8", "\xBC"}	// DOS
};


typedef struct _box box;
typedef struct _view view;
typedef struct _event event;

typedef void (*boxFunction) (box *box);
typedef void (*eventHandler) (view *view, event *event);

struct function
{
	char *name;			// Name for script purposes.
	char *description;		// Human name for the menus.
	char type;
	union
	{
		eventHandler handler;
		char *scriptCallback;
	};
};

struct keyCommand
{
	char *key;			// Key name.
	char *command;
};

struct item
{
	char *text;			// What's shown to humans.
	struct key *key;		// Shortcut key while the menu is displayed.
					// If there happens to be a key bound to the same command, the menu system should find that and show it to.
	char type;
	union
	{
		char *command;
		struct item *items;	// An array of next level menu items.
	};
};

struct borderWidget
{
	char *text;
	char *command;
};

// TODO - No idea if we will actually need this.
struct _event
{
	struct function *function;
	uint16_t X, Y;				// Current cursor position, or position of mouse click.
	char type;
	union
	{
		struct keyCommand *key;		// keystroke / mouse click
		struct item *item;		// menu
		struct borderWidget widget;	// border widget click
		int    time;			// timer
		struct				// scroll contents
		{
			int X, Y;
		}      scroll;
		// TODO - might need events for - leave box, enter box.  Could use a new event type "command with arguments"?
	};
};

// TODO - a generic "part of text", and what is used to define them.
// For instance - word, line, paragraph, section.
// Each context can have a collection of these.

struct mode
{
	struct keyCommand *keys;	// An array of key to command mappings.
	struct item *items;		// An array of top level menu items.
	struct item *functionKeys;	// An array of single level "menus".  Used to show key commands.
	uint8_t flags;			// commandMode.
};

/*
Have a common menu up the top.
    MC has a menu that changes per mode.
    Nano has no menu.
Have a common display of certain keys down the bottom.
    MC is one row of F1 to F10, but changes for edit / view / file browse.  But those are contexts here.
    Nano is 12 random Ctrl keys, possibly in two lines, that changes depending on the editor mode, like editing the prompt line for various reasons, help.
*/
struct context				// Defines a context for content.  Text viewer, editor, file browser for instance.
{
	struct function *commands;	// The master list, the ones pointed to by the menus etc should be in this list.
	struct mode *modes;		// A possible empty array of modes, indexed by the view.
					// OR might be better to have these as a linked list, so that things like Emacs can have it's mode keymap hierarcy.
	eventHandler handler;		// TODO - Might be better to put this in the modes.  I think vi will need that.
					// Should set the damage list if it needs a redraw, and flags if border or status line needs updating.
					// Keyboard / mouse events if the box did not handle them itself.
					// DrawAll event for when drawing the box for the first time, on reveals for coming out of full screen, or user hit the redraw key.
					// Scroll event if the content wants to handle that itself.
					// Timer event for things like top that might want to have this called regularly.
	boxFunction doneRedraw;		// The box is done with it's redraw, so we can free the damage list or whatever now.
	boxFunction delete;
    // This can be used as the sub struct for various context types.  Like viewer, editor, file browser, top, etc.
    // Could even be an object hierarchy, like generic editor, which Basic vi inherits from.
    //   Or not, since the commands might be different / more of them.
};

// TODO - might be better off just having a general purpose "widget" which includes details of where it gets attached.
// Status lines can have them to.
struct border
{
	struct borderWidget *topLeft;
	struct borderWidget *topMiddle;
	struct borderWidget *topRight;
	struct borderWidget *bottomLeft;
	struct borderWidget *bottomMiddle;
	struct borderWidget *bottomRight;
	struct borderWidget *left;
	struct borderWidget *right;
};

struct line
{
	struct line *next, *prev;
	uint32_t length;		// Careful, this is the length of the allocated memory for real lines, but the number of lines in the header node.
	char *line;			// Should be blank for the header.
};

struct damage
{
	struct damage *next;		// A list for faster draws?
	uint16_t X, Y, W, H;		// The rectangle to be redrawn.
	uint16_t offset;		// Offest from the left for showing lines.
	struct line *lines;		// Pointer to a list of text lines, or NULL.
					// Note - likely a pointer into the middle of the line list in a content.
};

struct content				// For various instances of context types.  
					// Editor / text viewer might have several files open, so one of these per file.
					// MC might have several directories open, one of these per directory.  No idea why you might want to do this.  lol
{
    struct context *context;
    char *name, *file, *path;
    struct line lines;
//  file type
//  double linked list of bookmarks, pointer to line, character position, length (or ending position?), type, blob for types to keep context.
    uint16_t minW, minH, maxW, maxH;
    uint8_t flags;			// readOnly, modified.
    // This can be used as the sub struct for various content types.
};

struct _view
{
	struct content *content;
	box *box;
	struct border *border;		// Can be NULL.
	char *statusLine;		// Text of the status line, or NULL if none.
	int mode;			// For those contexts that can be modal.  Normally used to index the keys, menus, and key displays.
	struct damage *damage;		// Can be NULL.  If not NULL after context->doneRedraw(), box will free it and it's children.
					// TODO - Gotta be careful of overlapping views.
	void *data;			// The context controls this blob, it's specific to each box.
	uint32_t offsetX, offsetY;	// Offset within the content, coz box handles scrolling, usually.
	uint16_t X, Y, W, H;		// Position and size of the content area within the box.  Calculated, but cached coz that might be needed for speed.
	uint16_t cX, cY;		// Cursor position within the content.
	uint16_t iX, oW;		// Cursor position inside the lines input text, in case the formatter makes it different, and output length.
	char *output;			// The current line formatted for output.
	uint8_t flags;			// redrawStatus, redrawBorder;

	// Assumption is that each box has it's own editLine (likely the line being edited), or that there's a box that is just the editLine (emacs minibuffer, and command lines for other proggies).
	struct line *line;		// Pointer to the current line, might be the only line.
	char *prompt;			// Optional prompt for the editLine.

// Display mode / format hook.
// view specific bookmarks, including highlighted block and it's type.
// Linked list of selected lines for a filtered view, or processing only those lines.
// Linked list of pointers to struct keyCommand, for emacs keymaps hierarchy, and anything similar in other editors.  Plus some way of dealing with emacs minor mode keymaps.
};

struct _box
{
	box *sub1, *sub2, *parent;
	view *view;		// This boxes view into it's content.  For sharing contents, like a split pane editor for instance, there might be more than one box with this content, but a different view.
				// If it's just a parent box, it wont have this, so just make it a damn pointer, that's the simplest thing.  lol
				// TODO - Are parent boxes getting a view anyway?
	uint16_t X, Y, W, H;	// Position and size of the box itself, not the content.  Calculated, but cached coz that might be needed for speed.
	float split;		// Ratio of sub1's part of the split, the sub2 box gets the rest.
	uint8_t flags;		// Various flags.
};


// Sometimes you just can't avoid circular definitions.
void drawBox(box *box);


#define BOX_HSPLIT	1	// Marks if it's a horizontally or vertically split.
#define BOX_BORDER	2	// Mark if it has a border, often full screen boxes wont.

static box *rootBox;	// Parent of the rest of the boxes, or the only box.  Always a full screen.
static box *currentBox;
static view *commandLine;
static int commandMode;

void doCommand(struct function *functions, char *command, view *view, event *event)
{
	if (command)
	{
		int i;

		for (i = 0; functions[i].name; i++)
		{
			if (strcmp(functions[i].name, command) == 0)
			{
				if (functions[i].handler);
					functions[i].handler(view, event);
				break;
			}
		}
	}
}

// Inserts the line after the given line, or at the end of content if no line.
struct line *addLine(struct content *content, struct line *line, char *text, uint32_t length)
{
	struct line *result = NULL;
	uint32_t len;

	if (!length)
		length = strlen(text);
	// Round length up.
	len = (((length + 1) / MEM_SIZE) + 1) * MEM_SIZE;
	result = xzalloc(sizeof(struct line));
	result->line = xzalloc(len);
	result->length = len;
	strncpy(result->line, text, length);

	if (content)
	{
		if (!line)
			line = content->lines.prev;

		result->next = line->next;
		result->prev = line;

		line->next->prev = result;
		line->next = result;

		content->lines.length++;
	}
	else
	{
		result->next = result;
		result->prev = result;
	}

	return result;
}

void freeLine(struct content *content, struct line *line)
{
	line->next->prev = line->prev;
	line->prev->next = line->next;
	if (content)
		content->lines.length--;
	free(line->line);
	free(line);
}

void loadFile(struct content *content)
{
	int fd = open(content->path, O_RDONLY);

	if (-1 != fd)
	{
		char *temp = NULL;
		long len;

		do
		{
			// TODO - get_rawline() is slow, and wont help much with DOS and Mac line endings.
			temp = get_rawline(fd, &len, '\n');
			if (temp)
			{
				if (temp[len - 1] == '\n')
				    temp[--len] = '\0';
				addLine(content, NULL, temp, len);
			}
		} while (temp);
		close(fd);
	}
}

// TODO - load and save should be able to deal with pipes, and with loading only parts of files, to load more parts later.

void saveFile(struct content *content)
{
// TODO - Should do "Save as" as well.  Which is just a matter of changing content->path before calling this.
	int fd;

	fd = open(content->path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);

	if (-1 != fd)
	{
		struct line *line = content->lines.next;

		while (&(content->lines) != line)	// We are at the end if we have wrapped to the beginning.
		{
			write(fd, line->line, strlen(line->line));
			write(fd, "\n", 1);
			line = line->next;
		}
		close(fd);
	}
	else
	{
		fprintf(stderr, "Can't open file %s\n", content->path);
		exit(1);
	}
}

struct content *addContent(char *name, struct context *context, char *filePath)
{
	struct content *result	= xzalloc(sizeof(struct content));

	result->lines.next	= &(result->lines);
	result->lines.prev	= &(result->lines);
	result->name		= strdup(name);
	result->context		= context;

	if (filePath)
	{
		result->path = strdup(filePath);
		loadFile(result);
	}

	return result;
}

// General purpose line moosher.  Used for appends, inserts, overwrites, and deletes.
// TODO - should have the same semantics as mooshStrings, only it deals with whole lines in double linked lists.
// We need content so we can adjust it's number of lines if needed.
void mooshLines(struct content *content, struct line *result, struct line *moosh, uint16_t index, uint16_t length, int insert)
{
}

// General purpose string moosher.  Used for appends, inserts, overwrites, and deletes.
void mooshStrings(struct line *result, char *moosh, uint16_t index, uint16_t length, int insert)
{
	char *c, *pos;
	int limit = strlen(result->line);
	int mooshLen = 0, resultLen;

	if (moosh)
		mooshLen = strlen(moosh);

	/*
	 * moosh  == NULL	a deletion
	 * length == 0		simple insertion
	 * length <  mooshlen	delete some, insert moosh
	 * length == mooshlen	exact overwrite.
	 * length >  mooshlen	delete a lot, insert moosh
	 */

	mooshLen -= length;
	resultLen = limit + mooshLen;

	// If we need more space, allocate more.
	if (resultLen > result->length)
	{
		result->length = resultLen + MEM_SIZE;
		result->line = xrealloc(result->line, result->length);
	}

	if (limit <= index)	// At end, just add to end.
	{
		// TODO - Possibly add spaces to pad out to where index is?
		//          Would be needed for "go beyond end of line" and "column blocks".
		//          Both of those are advanced editing.
		index = limit;
		insert = 1;
	}

	pos = &(result->line[index]);

	if (insert)	// Insert / delete before current character, so move it and the rest up / down mooshLen bytes.
	{
		if (0 < mooshLen)	// Gotta move things up.
		{
			c = &(result->line[limit]);
			while (c >= pos)
			{
				*(c + mooshLen) = *c;
				c--;
			}
		}
		else if (0 > mooshLen)	// Gotta move things down.
		{
			c = pos;
			while (*c)
			{
				*c = *(c - mooshLen);	// A double negative.
				c++;
			}
		}
	}

	if (moosh)
	{
		c = moosh;
		do
		{
			*pos++ = *c++;
		}
		while (*c);
	}
}

// TODO - Should draw the current border in green, the text as default (or highlight / bright).
//          Then allow one other box to be red bordered (MC / dired destination box).
//          All other boxes with dark gray border, and dim text.
void drawLine(int y, int start, int end, char *left, char *internal, char *contents, char *right, int current)
{
	int size = strlen(internal);
	int len = (end - start) * size, x = 0;
	char line[len + 1];

	if ('\0' != left[0])	// Assumes that if one side has a border, then so does the other.
		len -= 2 * size;

	if (contents)
	{
		// strncpy wont add a null at the end if the source is longer, but will pad with nulls if source is shorter.
		// So it's best to put a safety null in first.
		line[len] = '\0';
		strncpy(line, contents, len);
		// Make sure the following while loop pads out line with the internal character.
		x = strlen(line);
	}
	while (x < len)
	{
		strcpy(&line[x], internal);
		x += size;
	}
	line[x++] = '\0';
	if ('\0' == left[0])	// Assumes that if one side has a border, then so does the other.
	{
		if (current)
			printf("\x1B[1m\x1B[%d;%dH%s\x1B[m", y + 1, start + 1, line);
		else
			printf("\x1B[m\x1B[%d;%dH%s", y + 1, start + 1, line);
	}
	else
	{
		if (current)
			printf("\x1B[1m\x1B[%d;%dH%s%s%s\x1B[m", y + 1, start + 1, left, line, right);
		else
			printf("\x1B[m\x1B[%d;%dH%s%s%s", y + 1, start + 1, left, line, right);
	}
}

void formatCheckCursor(view *view, long *cX, long *cY, char *input)
{
	int i = 0, o = 0, direction = (*cX) - view->cX;

	// Adjust the cursor if needed, depending on the contents of the line, and the direction of travel.
	while (input[i])
	{
		// When o is equal to the cX position, update the iX position to be where i is.
		if ('\t' == input[i])
		{
			int j = 8 - (i % 8);

			// Check if the cursor is in the middle of the tab.
			if (((*cX) > o) && ((*cX) < (o + j)))
			{
				if (0 <= direction)
				{
					*cX = (o + j);
					view->iX = i + 1;
				}
				else
				{
					*cX = o;
					view->iX = i;
				}
			}
			o += j;
		}
		else
		{
			if ((*cX) == o)
				view->iX = i;
			o++;
		}
		i++;
	}
	// One more check in case the cursor is at the end of the line.
	if ((*cX) == o)
		view->iX = i;
}

// TODO - Should convert control characters to reverse video, and deal with UTF8.
// TODO - We get passed a NULL input, apparently when the file length is close to the screen length.  See the mailing list for details.  Fix that.
int formatLine(view *view, char *input, char **output)
{
	int len = strlen(input), i = 0, o = 0;

	*output = xrealloc(*output, len + 1);

	while (input[i])
	{
		if ('\t' == input[i])
		{
			int j = 8 - (i % 8);

			*output = xrealloc(*output, len + j + 1);
			for (; j; j--)
			{
				(*output)[o++] = ' ';
				len++;
			}
			len--;	// Not counting the actual tab character itself.
		}
		else
			(*output)[o++] = input[i];
		i++;
	}
	(*output)[o++] = '\0';

	return len;
}

void drawContentLine(view *view, int y, int start, int end, char *left, char *internal, char *contents, char *right, int current)
{
	char *temp = NULL;
	int offset = view->offsetX, len;

	if (contents == view->line->line)
	{
		view->oW = formatLine(view, contents, &(view->output));
		temp = view->output;
		len = view->oW;
	}
	else	// Only time we are not drawing the current line, and only used for drawing the entire page.
		len = formatLine(NULL, contents, &(temp));

	if (offset > len)
		offset = len;
	drawLine(y, start, end, left, internal, &(temp[offset]), right, current);
}

int moveCursorAbsolute(view *view, long cX, long cY, long sX, long sY)
{
	struct line *newLine = view->line;
	long oX = view->offsetX, oY = view->offsetY;
	long lX = view->oW, lY = view->content->lines.length - 1;
	long nY = view->cY;
	uint16_t w = view->W - 1, h = view->H - 1;
	int moved = 0, updatedY = 0, endOfLine = 0;

	// Check if it's still within the contents.
	if (0 > cY)		// Trying to move before the beginning of the content.
		cY = 0;
	else if (lY < cY)	// Trying to move beyond end of the content.
		cY = lY;
	if (0 > cX)		// Trying to move before the beginning of the line.
	{
		// See if we can move to the end of the previous line.
		if (view->line->prev != &(view->content->lines))
		{
			cY--;
			endOfLine = 1;
		}
		else
			cX = 0;
	}
	else if (lX < cX)	// Trying to move beyond end of line.
	{
		// See if we can move to the begining of the next line.
		if (view->line->next != &(view->content->lines))
		{
			cY++;
			cX = 0;
		}
		else
			cX = lX;
	}

	// Find the new line.
	while (nY != cY)
	{
		updatedY = 1;
		if (nY < cY)
		{
			newLine = newLine->next;
			nY++;
			if (view->content->lines.prev == newLine)	// We are at the end if we have wrapped to the beginning.
				break;
		}
		else
		{
			newLine = newLine->prev;
			nY--;
			if (view->content->lines.next == newLine)	// We are at the end if we have wrapped to the beginning.
				break;
		}
	}
	cY = nY;

	// Check if we have moved past the end of the new line.
	if (updatedY)
	{
		// Format it.
		view->oW = formatLine(view, newLine->line, &(view->output));
		if (view->oW < cX)
			endOfLine = 1;
		if (endOfLine)
			cX = view->oW;
	}

	// Let the formatter decide if it wants to adjust things.
	// It's up to the formatter to deal with things if it changes cY.
	// On the other hand, changing cX is it's job.
	formatCheckCursor(view, &cX, &cY, newLine->line);

	// Check the scrolling.
	lY -= view->H - 1;
	oX += sX;
	oY += sY;

	if (oY > cY)			// Trying to move above the box.
		oY += cY - oY;
	else if ((oY + h) < cY)		// Trying to move below the box
		oY += cY - (oY + h);
	if (oX > cX)			// Trying to move to the left of the box.
		oX += cX - oX;
	else if ((oX + w) <= cX)	// Trying to move to the right of the box.
		oX += cX - (oX + w);

	if (oY < 0)
		oY = 0;
	if (oY >= lY)
		oY = lY;
	if (oX < 0)
		oX = 0;
	// TODO - Should limit oX to less than the longest line, minus box width.
	//          Gonna be a pain figuring out what the longest line is.
	//          On the other hand, don't think that will be an actual issue unless "move past end of line" is enabled, and that's an advanced editor thing.
	//          Though still might want to do that for the longest line on the new page to be.

	if ((view->cX != cX) || (view->cY != cY))
		moved = 1;

	// Actually update stuff, though some have been done already.
	view->cX = cX;
	view->cY = cY;
	view->line = newLine;

	// Handle scrolling.
	if ((view->offsetX != oX) || (view->offsetY != oY))
	{
		view->offsetX = oX;
		view->offsetY = oY;

		if (view->box)
			drawBox(view->box);
	}

	return moved;
}

int moveCursorRelative(view *view, long cX, long cY, long sX, long sY)
{
	return moveCursorAbsolute(view, view->cX + cX, view->cY + cY, sX, sY);
}

void sizeViewToBox(box *box, int X, int Y, int W, int H)
{
	uint16_t one = 1, two = 2;

	if (!(box->flags & BOX_BORDER))
	{
	    one = 0;
	    two = 0;
	}
	box->view->X = X;
	box->view->Y = Y;
	box->view->W = W;
	box->view->H = H;
	if (0 > X)  box->view->X = box->X + one;
	if (0 > Y)  box->view->Y = box->Y + one;
	if (0 > W)  box->view->W = box->W - two;
	if (0 > H)  box->view->H = box->H - two;
}

view *addView(char *name, struct context *context, char *filePath, uint16_t X, uint16_t Y, uint16_t W, uint16_t H)
{
	view *result = xzalloc(sizeof(struct _view));

	result->X = X;
	result->Y = Y;
	result->W = W;
	result->H = H;

	result->content = addContent(name, context, filePath);
	result->prompt = xzalloc(1);
	// If there was content, format it's first line as usual, otherwise create an empty first line.
	if (result->content->lines.next != &(result->content->lines))
	{
		result->line = result->content->lines.next;
		result->oW = formatLine(result, result->line->line, &(result->output));
	}
	else
	{
		result->line = addLine(result->content, NULL, "\0", 0);
		result->output = xzalloc(1);
	}

	return result;
}

box *addBox(char *name, struct context *context, char *filePath, uint16_t X, uint16_t Y, uint16_t W, uint16_t H)
{
	box *result = xzalloc(sizeof(struct _box));

	result->X = X;
	result->Y = Y;
	result->W = W;
	result->H = H;
	result->view = addView(name, context, filePath, X, Y, W, H);
	result->view->box = result;
	sizeViewToBox(result, X, Y, W, H);

	return result;
}

void freeBox(box *box)
{
	if (box)
	{
		freeBox(box->sub1);
		freeBox(box->sub2);
		if (box->view)
		{
			// In theory the line should not be part of the content if there is no content, so we should free it.
			if (!box->view->content)
				freeLine(NULL, box->view->line);
			free(box->view->prompt);
			free(box->view->output);
			free(box->view);
		}
		free(box);
	}
}

void drawBox(box *box)
{
	// This could be heavily optimized, but let's keep things simple for now.
	// Optimized for sending less characters I mean, on slow serial links for instance.

	char **bchars = (toys.optflags & FLAG_a) ? borderChars[0] : borderChars[1];
	char *left = "\0", *right = "\0";
	struct line *lines = NULL;
	int y = box->Y, current = (box == currentBox);
	uint16_t h = box->Y + box->H;

	if (current)
		bchars = (toys.optflags & FLAG_a) ? borderCharsCurrent[0] : borderCharsCurrent[1];

	// Slow and laborious way to figure out where in the linked list of lines we start from.
	// Wont scale well, but is simple.
	if (box->view && box->view->content)
	{
		int i = box->view->offsetY;

		lines = &(box->view->content->lines);
		while (i--)
		{
			lines = lines->next;
			if (&(box->view->content->lines) == lines)	// We are at the end if we have wrapped to the beginning.
				break;
		}
	}

	if (box->flags & BOX_BORDER)
	{
		h--;
		left = right = bchars[1];
		drawLine(y++, box->X, box->X + box->W, bchars[2], bchars[0], NULL, bchars[3], current);
	}

	while (y < h)
	{
		char *line = "";

		if (lines)
		{
			lines = lines->next;
			if (&(box->view->content->lines) == lines)	// We are at the end if we have wrapped to the beginning.
				lines = NULL;
			else
				line = lines->line;
			// Figure out which line is our current line while we are here.
			if (box->view->Y + (box->view->cY - box->view->offsetY) == y)
				box->view->line = lines;
		}
		drawContentLine(box->view, y++, box->X, box->X + box->W, left, " ", line, right, current);
	}
	if (box->flags & BOX_BORDER)
		drawLine(y++, box->X, box->X + box->W, bchars[4], bchars[0], NULL, bchars[5], current);
	fflush(stdout);
}

void drawBoxes(box *box)
{
	if (box->sub1)	// If there's one sub box, there's always two.
	{
		drawBoxes(box->sub1);
		drawBoxes(box->sub2);
	}
	else
		drawBox(box);
}
void calcBoxes(box *box)
{
	if (box->sub1)	// If there's one sub box, there's always two.
	{
		box->sub1->X = box->X;
		box->sub1->Y = box->Y;
		box->sub1->W = box->W;
		box->sub1->H = box->H;
		box->sub2->X = box->X;
		box->sub2->Y = box->Y;
		box->sub2->W = box->W;
		box->sub2->H = box->H;
		if (box->flags & BOX_HSPLIT)
		{
			box->sub1->H *= box->split;
			box->sub2->H -= box->sub1->H;
			box->sub2->Y += box->sub1->H;
		}
		else
		{
			box->sub1->W *= box->split;
			box->sub2->W -= box->sub1->W;
			box->sub2->X += box->sub1->W;
		}
		sizeViewToBox(box->sub1, -1, -1, -1, -1);
		calcBoxes(box->sub1);
		sizeViewToBox(box->sub2, -1, -1, -1, -1);
		calcBoxes(box->sub2);
	}
	// Move the cursor to where it is, to check it's not now outside the box.
	moveCursorAbsolute(box->view, box->view->cX, box->view->cY, 0, 0);

	// We could call drawBoxes() here, but this is recursive, and so is drawBoxes().
	// The combination might be deadly.  Drawing the content of a box might be an expensive operation.
	// Later we might use a dirty box flag to deal with this, if it's not too much of a complication.
}

void deleteBox(view *view, event *event)
{
	box *box = view->box;

	if (box->parent)
	{
		struct _box *oldBox = box, *otherBox = box->parent->sub1;

		if (otherBox == oldBox)
			otherBox = box->parent->sub2;
		if (currentBox->parent == box->parent)
			currentBox = box->parent;
		box = box->parent;
		box->X = box->sub1->X;
		box->Y = box->sub1->Y;
		if (box->flags & BOX_HSPLIT)
			box->H = box->sub1->H + box->sub2->H;
		else
			box->W = box->sub1->W + box->sub2->W;
		box->flags &= ~BOX_HSPLIT;
		// Move the other sub boxes contents up to this box.
		box->sub1 = otherBox->sub1;
		box->sub2 = otherBox->sub2;
		if (box->sub1)
		{
		    box->sub1->parent = box;
		    box->sub2->parent = box;
		    box->flags = otherBox->flags;
		    if (currentBox == box)
			    currentBox = box->sub1;
		}
		else
		{
			if (!box->parent)
				box->flags &= ~BOX_BORDER;
			box->split = 1.0;
		}
		otherBox->sub1 = NULL;
		otherBox->sub2 = NULL;
		// Safe to free the boxes now that we have all their contents.
		freeBox(otherBox);
		freeBox(oldBox);
	}
	// Otherwise it must be a single full screen box.  Never delete that one, unless we are quitting.

	// Start the recursive recalculation of all the sub boxes.
	calcBoxes(box);
	drawBoxes(box);
}

void cloneBox(struct _box *box, struct _box *sub)
{
	sub->parent = box;
	// Only a full screen box has no border.
	sub->flags |= BOX_BORDER;
	sub->view = xmalloc(sizeof(struct _view));
	// TODO - After this is more stable, should check if the memcpy() is simpler than - xzalloc() then copy a few things manually.
	//          Might even be able to arrange the structure so we can memcpy just part of it, leaving the rest blank.
	memcpy(sub->view, box->view, sizeof(struct _view));
	sub->view->damage = NULL;
	sub->view->data = NULL;
	sub->view->output = NULL;
	sub->view->box = sub;
	if (box->view->prompt)
		sub->view->prompt = strdup(box->view->prompt);
}

void splitBox(box *box, float split)
{
	uint16_t size;
	int otherBox = 0;

	// First some sanity checks.
	if (0.0 > split)
	{
		// TODO - put this in the status line, or just silently fail.  Also, better message.  lol
		fprintf(stderr, "User is crazy.\n");
		return;
	}
	else if (1.0 <= split)	// User meant to unsplit, and it may already be split.
	{
		// Actually, this means that the OTHER sub box gets deleted.
		if (box->parent)
		{
			if (box == box->parent->sub1)
				deleteBox(box->parent->sub2->view, NULL);
			else
				deleteBox(box->parent->sub1->view, NULL);
		}
		return;
	}
	else if (0.0 < split)	// This is the normal case, so do nothing.
	{
	}
	else			// User meant to delete this, zero split.
	{
	    deleteBox(box->view, NULL);
	    return;
	}
	if (box->flags & BOX_HSPLIT)
		size = box->H;
	else
		size = box->W;
	if (6 > size)		// Is there room for 2 borders for each sub box and one character of content each?
				// TODO - also should check the contents minimum size.
				// No need to check the no border case, that's only for full screen.
				// People using terminals smaller than 6 characters get what they deserve.
	{
		// TODO - put this in the status line, or just silently fail.
		fprintf(stderr, "Box is too small to split.\n");
		return;
	}

	// Note that a split box is actually three boxes.  The parent, which is not drawn, and the two subs, which are.
	// Based on the assumption that there wont be lots of boxes, this keeps things simple.
	// It's possible that the box has already been split, and this is called just to update the split.
	box->split = split;
	if (NULL == box->sub1)	// If not split already, do so.
	{
		box->sub1 = xzalloc(sizeof(struct _box));
		box->sub2 = xzalloc(sizeof(struct _box));
		cloneBox(box, box->sub1);
		cloneBox(box, box->sub2);
		if (box->flags & BOX_HSPLIT)
		{
			// Split the boxes in the middle of the content.
			box->sub2->view->offsetY += (box->H * box->split) - 2;
			// Figure out which sub box the cursor will be in, then update the cursor in the other box.
			if (box->sub1->view->cY < box->sub2->view->offsetY)
				box->sub2->view->cY = box->sub2->view->offsetY;
			else
			{
				box->sub1->view->cY = box->sub2->view->offsetY - 1;
				otherBox = 1;
			}
		}
		else
		{
			// Split the boxes in the middle of the content.
			box->sub2->view->offsetX += (box->W * box->split) - 2;
			// Figure out which sub box the cursor will be in, then update the cursor in the other box.
			if (box->sub1->view->cX < box->sub2->view->offsetX)
				box->sub2->view->cX = box->sub2->view->offsetX;
			else
			{
				box->sub1->view->cX = box->sub2->view->offsetX - 1;
				otherBox = 1;
			}
		}
	}

	if ((currentBox == box) && (box->sub1))
	{
		if (otherBox)
			currentBox = box->sub2;
		else
			currentBox = box->sub1;
	}

	// Start the recursive recalculation of all the sub boxes.
	calcBoxes(box);
	drawBoxes(box);
}

// TODO - Might be better to just have a double linked list of boxes, and traverse that instead.
//          Except that leaves a problem when deleting boxes, could end up with a blank space.
void switchBoxes(view *view, event *event)
{
	box *box = view->box;

	// The assumption here is that box == currentBox.
	struct _box *oldBox = currentBox;
	struct _box *thisBox = box;
	int backingUp = 0;

	// Depth first traversal.
	while ((oldBox == currentBox) && (thisBox->parent))
	{
		if (thisBox == thisBox->parent->sub1)
		{
			if (backingUp && (thisBox->parent))
				currentBox = thisBox->parent->sub2;
			else if (thisBox->sub1)
				currentBox = thisBox->sub1;
			else
				currentBox = thisBox->parent->sub2;
		}
		else if (thisBox == thisBox->parent->sub2)
		{
			thisBox = thisBox->parent;
			backingUp = 1;
		}
	}

	// If we have not found the next box to move to, move back to the beginning.
	if (oldBox == currentBox)
		currentBox = rootBox;

	// If we ended up on a parent box, go to it's first sub.
	while (currentBox->sub1)
		currentBox = currentBox->sub1;

	// Just redraw them all.
	drawBoxes(rootBox);
}

// TODO - It might be better to do away with this bunch of single line functions
//        and map script commands directly to lower functions.
//        How to deal with the various argument needs?
//        Might be where we can re use the toybox argument stuff.

void halveBoxHorizontally(view *view, event *event)
{
	view->box->flags |= BOX_HSPLIT;
	splitBox(view->box, 0.5);
}

void halveBoxVertically(view *view, event *event)
{
	view->box->flags &= ~BOX_HSPLIT;
	splitBox(view->box, 0.5);
}

void switchMode(view *view, event *event)
{
	currentBox->view->mode++;
	// Assumes that modes will always have a key mapping, which I think is a safe bet.
	if (NULL == currentBox->view->content->context->modes[currentBox->view->mode].keys)
		currentBox->view->mode = 0;
	commandMode = currentBox->view->content->context->modes[currentBox->view->mode].flags & 1;
}

void leftChar(view *view, event *event)
{
	moveCursorRelative(view, -1, 0, 0, 0);
}

void rightChar(view *view, event *event)
{
	moveCursorRelative(view, 1, 0, 0, 0);
}

void upLine(view *view, event *event)
{
	moveCursorRelative(view, 0, -1, 0, 0);
}

void downLine(view *view, event *event)
{
	moveCursorRelative(view, 0, 1, 0, 0);
}

void upPage(view *view, event *event)
{
	moveCursorRelative(view, 0, 0 - (view->H - 1), 0, 0 - (view->H - 1));
}

void downPage(view *view, event *event)
{
	moveCursorRelative(view, 0, view->H - 1, 0, view->H - 1);
}

void endOfLine(view *view, event *event)
{
	moveCursorAbsolute(view, strlen(view->prompt) + view->oW, view->cY, 0, 0);
}

void startOfLine(view *view, event *event)
{
	// TODO - add the advanced editing "smart home".
	moveCursorAbsolute(view, strlen(view->prompt), view->cY, 0, 0);
}

void splitLine(view *view, event *event)
{
	// TODO - should move this into mooshLines().
	addLine(view->content, view->line, &(view->line->line[view->iX]), 0);
	view->line->line[view->iX] = '\0';
	moveCursorAbsolute(view, 0, view->cY + 1, 0, 0);
	if (view->box)
		drawBox(view->box);
}

void deleteChar(view *view, event *event)
{
	// TODO - should move this into mooshLines().
	// If we are at the end of the line, then join this and the next line.
	if (view->oW == view->cX)
	{
		// Only if there IS a next line.
		if (&(view->content->lines) != view->line->next)
		{
			mooshStrings(view->line, view->line->next->line, view->iX, 1, !TT.overWriteMode);
			view->line->next->line = NULL;
			freeLine(view->content, view->line->next);
			// TODO - should check if we are on the last page, then deal with scrolling.
			if (view->box)
				drawBox(view->box);
		}
	}
	else
		mooshStrings(view->line, NULL, view->iX, 1, !TT.overWriteMode);
}

void backSpaceChar(view *view, event *event)
{
	if (moveCursorRelative(view, -1, 0, 0, 0))
		deleteChar(view, event);
}

void saveContent(view *view, event *event)
{
	saveFile(view->content);
}

void executeLine(view *view, event *event)
{
	struct line *result = view->line;

	// Don't bother doing much if there's nothing on this line.
	if (result->line[0])
	{
		doCommand(currentBox->view->content->context->commands, result->line, currentBox->view, event);
		// If we are not at the end of the history contents.
		if (&(view->content->lines) != result->next)
		{
			struct line *line = view->content->lines.prev;

			// Remove the line first.
			result->next->prev = result->prev;
			result->prev->next = result->next;
			// Check if the last line is already blank, then remove it.
			if ('\0' == line->line[0])
			{
				freeLine(view->content, line);
				line = view->content->lines.prev;
			}
			// Then add it to the end.
			result->next = line->next;
			result->prev = line;
			line->next->prev = result;
			line->next = result;
			view->cY = view->content->lines.length - 1;
		}
		moveCursorAbsolute(view, 0, view->content->lines.length, 0, 0);
		// Make sure there is one blank line at the end.
		if ('\0' != view->line->line[0])
		{
			endOfLine(view, event);
			splitLine(view, event);
		}
	}

	saveFile(view->content);
}

void quit(view *view, event *event)
{
	TT.stillRunning = 0;
}

void nop(box *box, event *event)
{
	// 'tis a nop, don't actually do anything.
}

#define BUFFER_LEN 16


int handleKey(view *view, int i, char *keyName, char *buffer)
{
	// This is using the currentBox instead of view, coz the command line keys are part of the box context now, not separate.
	struct keyCommand *keys = currentBox->view->content->context->modes[currentBox->view->mode].keys;
	int k, len = strlen(keyName), found = 0, doZero = 1;

	for (k = 0; keys[k].key; k++)
	{
		if (strncmp(keys[k].key, keyName, len) == 0)
		{
			if ('\0' != keys[k].key[len])
			{		// Found only a partial key.
				if (('^' == keyName[0]) && (1 != len))  // Want to let actual ^ characters through unmolested.
				{	// And it's a control key combo, so keep accumulating them.
					// Note this wont just keep accumulating, coz the moment it no longer matches any key combos, it fails to be found and falls through.
					found = 1;
					i++;
					doZero = 0;
					break;
				}
				else	// It's really an ordinary key, but we can break early at least.
					break;
			}
			else		// We have matched the entire key name, so do it.
			{
				found = 1;
				doCommand(view->content->context->commands, keys[k].command, view, NULL);
			}
			break;
		}
	}
	if (!found)			// No bound key, or partial control key combo, add input to the current view.
	{
		// TODO - Should check for tabs to, and insert them.
		//        Though better off having a function for that?
		if ((i == 0) && (isprint(buffer[0])))
		{
			mooshStrings(view->line, buffer, view->iX, 0, !TT.overWriteMode);
			view->oW = formatLine(view, view->line->line, &(view->output));
			moveCursorRelative(view, strlen(buffer), 0, 0, 0);
		}
		else
		{
			// TODO - Should bitch on the status line instead.
			fprintf(stderr, "Key is %s\n", keyName);
			fflush(stderr);
		}
	}
	if (doZero)
	{
		i = 0;
		buffer[0] = '\0';
	}

	return i;
}


// Basically this is the main loop.

// X and Y are screen coords.
// W and H are the size of the editLine.  EditLine will start one line high, and grow to a maximum of H if needed, then start to scroll.
// H more than one means the editLine can grow upwards, unless it's at the top of the box / screen, then it has to grow downwards.
// X, Y, W, and H can be -1, which means to grab suitable numbers from the views box.
void editLine(view *view, int16_t X, int16_t Y, int16_t W, int16_t H)
{
	struct termios termio, oldtermio;
	struct pollfd pollfds[1];
	char buffer[BUFFER_LEN];
	int pollcount = 1;
	int i = 0;
// TODO - multiline editLine is an advanced feature.  Editing boxes just moves the editLine up and down.
//	uint16_t h = 1;
// TODO - should check if it's at the top of the box, then grow it down instead of up if so.

	buffer[0] = '\0';

	if (view->box)
		sizeViewToBox(view->box, X, Y, W, H);
	// Assumes the view was already setup if it's not part of a box.

	// All the mouse tracking methods suck one way or another.  sigh
	// Enable mouse (VT200 normal tracking mode, UTF8 encoding).  The limit is 2015.  Seems to only be in later xterms.
//	printf("\x1B[?1005h");
	// Enable mouse (DEC locator reporting mode).  In theory has no limit.  Wont actually work though.
	// On the other hand, only allows for four buttons, so only half a mouse wheel.
//	printf("\x1B[1;2'z\x1B[1;3'{");
	// Enable mouse (VT200 normal tracking mode).  Has a limit of 256 - 32 rows and columns.  An xterm exclusive I think, but works in roxterm at least.
	printf("\x1B[?1000h");
	fflush(stdout);
	// TODO - Should remember to turn off mouse reporting when we leave.

	// Grab the old terminal settings and save it.
	tcgetattr(0, &oldtermio);
	tcflush(0, TCIFLUSH);
	termio = oldtermio;

	// Mould the terminal to our will.
	termio.c_iflag &= ~(IUCLC|IXON|IXOFF|IXANY);
	termio.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL|TOSTOP|ICANON);
	termio.c_cc[VTIME]=0;	// deciseconds.
	termio.c_cc[VMIN]=1;
	tcsetattr(0, TCSANOW, &termio);

	calcBoxes(currentBox);
	drawBoxes(currentBox);

	// TODO - OS buffered keys might be a problem, but we can't do the usual timestamp filter for now.
	while (TT.stillRunning)
	{
		// TODO - We can reuse one or two of these to have less of them.
		int j = 0, p, ret, y, len;

		if (commandMode)
			view = commandLine;
		else
			view = currentBox->view;
		y = view->Y + (view->cY - view->offsetY);
		len = strlen(view->prompt);
		drawLine(y, view->X, view->X + view->W, "\0", " ", view->prompt, '\0', 0);
		drawContentLine(view, y, view->X + len, view->X + view->W, "\0", " ", view->line->line, '\0', 1);
		printf("\x1B[%d;%dH", y + 1, view->X + len + (view->cX - view->offsetX) + 1);
		fflush(stdout);

		// Apparently it's more portable to reset this each time.
		memset(pollfds, 0, pollcount * sizeof(struct pollfd));
		pollfds[0].events = POLLIN;
		pollfds[0].fd = 0;

		p = poll(pollfds, pollcount, 100);		// Timeout of one tenth of a second (100).
		if (0 >  p) perror_exit("poll");
		if (0 == p)					// A timeout, trigger a time event.
		{
			if ((1 == i) && ('\x1B' == buffer[0]))
			{
				// After a short delay to check, this is a real Escape key, not part of an escape sequence, so deal with it.
				strcpy(buffer, "^[");
				i = 1;
				i = handleKey(view, i, buffer, buffer);
				continue;
			}
			else
			{
				// TODO - Send a timer event somewhere.
				// This wont be a precise timed event, but don't think we need one.
				continue;
			}
		}
		for (p--; 0 <= p; p--)
		{
			if (pollfds[p].revents & POLLIN)
			{
				ret = read(pollfds[p].fd, &buffer[i], 1);
				buffer[i + 1] = '\0';
				if (ret < 0)			// An error happened.
				{
					// For now, just ignore errors.
					fprintf(stderr, "input error on %d\n", p);
					fflush(stderr);
				}
				else if (ret == 0)		// End of file.
				{
					fprintf(stderr, "EOF\n");
					fflush(stderr);
					break;
				}
				else if (BUFFER_LEN == i + 1)	// Ran out of buffer.
				{
					fprintf(stderr, "Full buffer -%s\n", buffer);
					for (j = 0; buffer[j + 1]; j++)
						fprintf(stderr, "(%x) %c, ", (int) buffer[j], buffer[j]);
					fflush(stderr);
					i = 0;
				}
				else
				{
					char *keyName = NULL;

					if (('\x1B' == buffer[i]) && (0 != i))	// An unrecognised escape sequence, start again.
										// TODO - it might be a reply from a query we sent, like asking for the terminal size or cursor position.  Which apparently is the same thing.
					{
						// TODO - Should bitch on the status line instead.
						fprintf(stderr, "Unknown escape sequence ");
						for (j = 0; buffer[j + 1]; j++)
							fprintf(stderr, "(%x) %c, ", (int) buffer[j], buffer[j]);
						fprintf(stderr, "\n");
						fflush(stderr);
						buffer[0] = '\x1B';
						i = 1;
						continue;
					}

					for (j = 0; keys[j].code; j++)		// Search for multibyte keys and some control keys.
					{
						if (strcmp(keys[j].code, buffer) == 0)
						{
							keyName = keys[j].name;
							break;
						}
					}
					// See if it's an ordinary key,
					if ((NULL == keyName) && (0 == i) && isprint(buffer[0]))
						keyName = buffer;
					// Check for control keys, but not those that have already been identified, or ESC.
					if ((NULL == keyName) && iscntrl(buffer[i]) && ('\x1B' != buffer[i]))
					{
						// Convert to "^X" format.
						buffer[i + 1] = buffer[i] + '@';
						buffer[i++] = '^';
						buffer[i + 1] = '\0';
						keyName=buffer;
					}
					// See if it's already accumulating a control key combo.
					if ('^' == buffer[0])
						keyName = buffer;
					// For now we will assume that control keys could be the start of multi key combinations.
					// TODO - If the view->context HAS on event handler, use it, otherwise look up the specific event handler in the context modes ourselves?
					if (keyName)				// Search for a bound key.
						i = handleKey(view, i, keyName, buffer);
					else
						i++;
				}
			}
		}
	}

	// Restore the old terminal settings.
	tcsetattr(0, TCSANOW, &oldtermio);
}


// The default command to function mappings, with help text.  Any editor that does not have it's own commands can use these for keystroke binding and such.
// Though most of the editors have their own variation.  Maybe just use the joe one as default, it uses short names at least.
struct function simpleEditCommands[] =
{
	{"backSpaceChar","Back space last character.",		0, {backSpaceChar}},
	{"deleteBox",	"Delete a box.",			0, {deleteBox}},
	{"deleteChar",	"Delete current character.",		0, {deleteChar}},
	{"downLine",	"Move cursor down one line.",		0, {downLine}},
	{"downPage",	"Move cursor down one page.",		0, {downPage}},
	{"endOfLine",	"Go to end of line.",			0, {endOfLine}},
	{"executeLine",	"Execute a line as a script.",		0, {executeLine}},
	{"leftChar",	"Move cursor left one character.",	0, {leftChar}},
	{"quit",	"Quit the application.",		0, {quit}},
	{"rightChar",	"Move cursor right one character.",	0, {rightChar}},
	{"save",	"Save.",				0, {saveContent}},
	{"splitH",	"Split box in half horizontally.",	0, {halveBoxHorizontally}},
	{"splitLine",	"Split line at cursor.",		0, {splitLine}},
	{"splitV",	"Split box in half vertically.",	0, {halveBoxVertically}},
	{"startOfLine",	"Go to start of line.",			0, {startOfLine}},
	{"switchBoxes",	"Switch to another box.",		0, {switchBoxes}},
	{"switchMode",	"Switch between command and box.",	0, {switchMode}},
	{"upLine",	"Move cursor up one line.",		0, {upLine}},
	{"upPage",	"Move cursor up one page.",		0, {upPage}},
	{NULL, NULL, 0, {NULL}}
};

// Construct a simple command line.

// The key to command mappings.
// TODO - Should not move off the ends of the line to the next / previous line.
struct keyCommand simpleCommandKeys[] =
{
	{"BS",		"backSpaceChar"},
	{"Del",		"deleteChar"},
	{"Down",	"downLine"},
	{"End",		"endOfLine"},
	{"F10",		"quit"},
	{"Home",	"startOfLine"},
	{"Left",	"leftChar"},
	{"Return",	"executeLine"},
	{"Right",	"rightChar"},
	{"Shift F2",	"switchMode"},
	{"Up",		"upLine"},
	{NULL, NULL}
};


// TODO - simple emacs editor.
// Mostly control keys, some meta keys.
// Ctrl-h and Ctrl-x have more keys in the commands.  Some of those extra keys are commands by themselves.  Up to "Ctrl-x 4 Ctrl-g" which apparently is identical to just Ctrl-g.  shrugs
//   Ctrl-h is backspace / del.  Pffft.
// Meta key is either Alt-keystroke, Esc keystroke, or an actual Meta-keystroke (do they still exist?).
//   TODO - Alt and Meta not supported yet, so using Esc.
// Windows commands.

// readline uses these same commands, and defaults to emacs keystrokes.
struct function simpleEmacsCommands[] =
{
	{"delete-backward-char",	"Back space last character.",		0, {backSpaceChar}},
	{"delete-window",		"Delete a box.",			0, {deleteBox}},
	{"delete-char",			"Delete current character.",		0, {deleteChar}},
	{"next-line",			"Move cursor down one line.",		0, {downLine}},
	{"scroll-up",			"Move cursor down one page.",		0, {downPage}},
	{"end-of-line",			"Go to end of line.",			0, {endOfLine}},
	{"accept-line",			"Execute a line as a script.",		0, {executeLine}},		// From readline, which uses emacs commands, coz mg at least does not seem to have this.
	{"backward-char",		"Move cursor left one character.",	0, {leftChar}},
	{"save-buffers-kill-emacs",	"Quit the application.",		0, {quit}},			// Does more than just quit.
	{"forward-char",		"Move cursor right one character.",	0, {rightChar}},
	{"save-buffer",			"Save.",				0, {saveContent}},
	{"split-window-horizontally",	"Split box in half horizontally.",	0, {halveBoxHorizontally}},	// TODO - Making this one up for now, mg does not have it.
	{"newline",			"Split line at cursor.",		0, {splitLine}},
	{"split-window-vertically",	"Split box in half vertically.",	0, {halveBoxVertically}},
	{"beginning-of-line",		"Go to start of line.",			0, {startOfLine}},
	{"other-window",		"Switch to another box.",		0, {switchBoxes}},		// There is also "previous-window" for going in the other direction, which we don't support yet.
	{"execute-extended-command",	"Switch between command and box.",	0, {switchMode}},		// Actually a one time invocation of the command line.
	{"previous-line",		"Move cursor up one line.",		0, {upLine}},
	{"scroll-down",			"Move cursor up one page.",		0, {upPage}},
	{NULL, NULL, 0, {NULL}}
};

// The key to command mappings.
struct keyCommand simpleEmacsKeys[] =
{
	{"Del",		"delete-backward-char"},
	{"^D",		"delete-char"},
	{"Down",	"next-line"},
	{"^N",		"next-line"},
	{"End",		"end-of-line"},
	{"^E",		"end-of-line"},
	{"^X^C",	"save-buffers-kill-emacs"},	// Damn, Ctrl C getting eaten by default signal handling.
	{"^Xq",		"save-buffers-kill-emacs"},	// TODO - Faking this so we can actually exit.  Remove it later.
	{"^X^S",	"save-buffer"},
	{"Home",	"beginning-of-line"},
	{"^A",		"beginning-of-line"},
	{"Left",	"backward-char"},
	{"^B",		"backward-char"},
	{"PgDn",	"scroll-up"},
	{"^V",		"scroll-up"},
	{"PgUp",	"scroll-down"},
	{"^[v",		"scroll-down"},			// M-v
	{"Return",	"newline"},
	{"Right",	"forward-char"},
	{"^F",		"forward-char"},
	{"^[x",		"execute-extended-command"},	// M-x
	{"^X2",		"split-window-vertically"},
	{"^X3",		"split-window-horizontally"},	// TODO - Again, just making this up for now.
	{"^XP",		"other-window"},
	{"^XP",		"other-window"},
	{"^X0",		"delete-window"},
	{"Up",		"previous-line"},
	{"^P",		"previous-line"},
	{NULL, NULL}
};

struct keyCommand simpleEmacsCommandKeys[] =
{
	{"Del",		"delete-backwards-char"},
	{"^D",		"delete-char"},
	{"^D",		"delete-char"},
	{"Down",	"next-line"},
	{"^N",		"next-line"},
	{"End",		"end-of-line"},
	{"^E",		"end-of-line"},
	{"Home",	"beginning-of-line"},
	{"^A",		"beginning-of-line"},
	{"Left",	"backward-char"},
	{"^B",		"backward-char"},
	{"Up",		"previous-line"},
	{"^P",		"previous-line"},
	{"Return",	"accept-line"},
	{"^[x",		"execute-extended-command"},
	{NULL, NULL}
};

// An array of various modes.
struct mode simpleEmacsMode[] =
{
    {simpleEmacsKeys, NULL, NULL, 0},
    {simpleEmacsCommandKeys, NULL, NULL, 1},
    {NULL, NULL, NULL}
};

// Put it all together into a simple editor context.
struct context simpleEmacs =
{
    simpleEmacsCommands,
    simpleEmacsMode,
    NULL,
    NULL,
    NULL
};


// Construct a simple joe / wordstar editor, using joe is the reference, seems to be the popular Unix variant.
// Esc x starts up the command line.
// Has multi control key combos.  Mostly Ctrl-K, Ctrl-[ (Esc), (Ctrl-B, Ctrl-Q in wordstar and delphi), but might be others.
//   Can't find a single list of comand mappings for joe, gotta search all over.  sigh
//   Even the command line keystroke I stumbled on (Esc x) is not documented.
//   Note that you don't have to let go of the Ctrl key for the second keystroke, but you can.

// From http://joe-editor.sourceforge.net/list.html
// TODO - Some of these might be wrong.  Just going by the inadequate joe docs for now.
struct function simpleJoeCommands[] =
{
	{"backs",	"Back space last character.",		0, {backSpaceChar}},
	{"abort",	"Delete a box.",			0, {deleteBox}},
	{"delch",	"Delete current character.",		0, {deleteChar}},
	{"dnarw",	"Move cursor down one line.",		0, {downLine}},
	{"pgdn",	"Move cursor down one page.",		0, {downPage}},
	{"eol",		"Go to end of line.",			0, {endOfLine}},
	{"ltarw",	"Move cursor left one character.",	0, {leftChar}},
	{"killjoe",	"Quit the application.",		0, {quit}},
	{"rtarw",	"Move cursor right one character.",	0, {rightChar}},
	{"save",	"Save.",				0, {saveContent}},
	{"splitw",	"Split box in half horizontally.",	0, {halveBoxHorizontally}},
	{"open",	"Split line at cursor.",		0, {splitLine}},
	{"bol",		"Go to start of line.",			0, {startOfLine}},
	{"home",	"Go to start of line.",			0, {startOfLine}},
	{"nextw",	"Switch to another box.",		0, {switchBoxes}},	// This is "next window", there's also "previous window" which we don't support yet.
	{"execmd",	"Switch between command and box.",	0, {switchMode}},	// Actually I think this just switches to the command mode, not back and forth.  Or it might execute the actual command.
	{"uparw",	"Move cursor up one line.",		0, {upLine}},
	{"pgup",	"Move cursor up one page.",		0, {upPage}},

	// Not an actual joe command.
	{"executeLine",	"Execute a line as a script.",		0, {executeLine}},	// Perhaps this should be execmd?
	{NULL, NULL, 0, {NULL}}
};

struct keyCommand simpleJoeKeys[] =
{
	{"BS",		"backs"},
	{"^D",		"delch"},
	{"Down",	"dnarw"},
	{"^N",		"dnarw"},
	{"^E",		"eol"},
//	{"F10",		"killjoe"},	// "deleteBox" should do this if it's the last window.
	{"^Kd",		"save"},
	{"^K^D"		"save"},
	{"^A",		"bol"},
	{"Left",	"ltarw"},
	{"^B",		"ltarw"},
	{"^V",		"pgdn"},	// Actually half a page.
	{"^U",		"pgup"},	// Actually half a page.
	{"Return",	"open"},
	{"Right",	"rtarw"},
	{"^F",		"rtarw"},
	{"^[x",		"execmd"},
	{"^[^X",	"execmd"},
	{"^Ko",		"splitw"},
	{"^K^O",	"splitw"},
	{"^Kn",		"nextw"},
	{"^K^N",	"nextw"},
	{"^Kx",		"abort"},	// Should ask if it should save if it's been modified.  A good generic thing to do anyway.
	{"^K^X",	"abort"},
	{"Up",		"uparw"},
	{"^P",		"uparw"},
	{NULL, NULL}
};

struct keyCommand simpleJoeCommandKeys[] =
{
	{"BS",		"backs"},
	{"^D",		"delch"},
	{"Down",	"dnarw"},
	{"^N",		"dnarw"},
	{"^E",		"eol"},
	{"^A",		"bol"},
	{"Left",	"ltarw"},
	{"^B",		"ltarw"},
	{"Right",	"rtarw"},
	{"^F",		"rtarw"},
	{"^[x",		"execmd"},
	{"^[^X",	"execmd"},
	{"Up",		"uparw"},
	{"^P",		"uparw"},
	{"Return",	"executeLine"},
	{NULL, NULL}
};

struct mode simpleJoeMode[] =
{
    {simpleJoeKeys, NULL, NULL, 0},
    {simpleJoeCommandKeys, NULL, NULL, 1},
    {NULL, NULL, NULL, 0}
};

struct context simpleJoe =
{
    simpleJoeCommands,
    simpleJoeMode,
    NULL,
    NULL,
    NULL
};


// Simple more and / or less.
// '/' and '?' for search command mode.  I think they both have some ex commands with the usual : command mode starter.
// No cursor movement, just scrolling.
// TODO - Put content into read only mode.
// TODO - actually implement read only mode where up and down one line do actualy scrolling.

struct keyCommand simpleLessKeys[] =
{
	{"Down",	"downLine"},
	{"j",		"downLine"},
	{"Return",	"downLine"},
	{"End",		"endOfLine"},
	{"q",		"quit"},
	{":q",		"quit"},
	{"ZZ",		"quit"},
	{"PgDn",	"downPage"},
	{"f",		"downPage"},
	{" ",		"downPage"},
	{"^F",		"downPage"},
	{"Left",	"leftChar"},
	{"Right",	"rightChar"},
	{"PgUp",	"upPage"},
	{"b",		"upPage"},
	{"^B",		"upPage"},
	{"Up",		"upLine"},
	{"k",		"upLine"},
	{NULL, NULL}
};

struct mode simpleLessMode[] =
{
    {simpleLessKeys, NULL, NULL, 0},
    {simpleCommandKeys, NULL, NULL, 1},
    {NULL, NULL, NULL}
};

struct context simpleLess =
{
    simpleEditCommands,
    simpleLessMode,
    NULL,
    NULL,
    NULL
};

struct keyCommand simpleMoreKeys[] =
{
	{"j",		"downLine"},
	{"Return",	"downLine"},
	{"q",		"quit"},
	{":q",		"quit"},
	{"ZZ",		"quit"},
	{"f",		"downPage"},
	{" ",		"downPage"},
	{"^F",		"downPage"},
	{"b",		"upPage"},
	{"^B",		"upPage"},
	{"k",		"upLine"},
	{NULL, NULL}
};

struct mode simpleMoreMode[] =
{
    {simpleMoreKeys, NULL, NULL, 0},
    {simpleCommandKeys, NULL, NULL, 1},
    {NULL, NULL, NULL}
};

struct context simpleMore =
{
    simpleEditCommands,
    simpleMoreMode,
    NULL,
    NULL,
    NULL
};


// Construct a simple mcedit / cool edit editor.

struct keyCommand simpleMceditKeys[] =
{
	{"BS",		"backSpaceChar"},
	{"Del",		"deleteChar"},
	{"Down",	"downLine"},
	{"End",		"endOfLine"},
	{"F10",		"quit"},
	{"F2",		"save"},
	{"Home",	"startOfLine"},
	{"Left",	"leftChar"},
	{"PgDn",	"downPage"},
	{"PgUp",	"upPage"},
	{"Return",	"splitLine"},
	{"Right",	"rightChar"},
	{"Shift F2",	"switchMode"},
	{"Shift F3",	"splitV"},
	{"Shift F4",	"splitH"},
	{"Shift F6",	"switchBoxes"},
	{"Shift F9",	"deleteBox"},
	{"Up",		"upLine"},
	{NULL, NULL}
};

struct mode simpleMceditMode[] =
{
    {simpleMceditKeys, NULL, NULL, 0},
    {simpleCommandKeys, NULL, NULL, 1},
    {NULL, NULL, NULL}
};

struct context simpleMcedit =
{
    simpleEditCommands,
    simpleMceditMode,
    NULL,
    NULL,
    NULL
};


// Simple nano editor.
// Has key to function bindings, but no command line mode.  Has "enter parameter on this line" mode for some commands.
// Control and meta keys, only singles, unlike emacs and joe.
// Can have multiple buffers, but no windows.  Think I can skip that for simple editor.

struct function simpleNanoCommands[] =
{
	{"backSpaceChar","Back space last character.",		0, {backSpaceChar}},
	{"delete",	"Delete current character.",		0, {deleteChar}},
	{"down",	"Move cursor down one line.",		0, {downLine}},
	{"downPage",	"Move cursor down one page.",		0, {downPage}},
	{"end",		"Go to end of line.",			0, {endOfLine}},
	{"left",	"Move cursor left one character.",	0, {leftChar}},
	{"exit",	"Quit the application.",		0, {quit}},
	{"right",	"Move cursor right one character.",	0, {rightChar}},
	{"writeout",	"Save.",				0, {saveContent}},
	{"enter",	"Split line at cursor.",		0, {splitLine}},
	{"home",	"Go to start of line.",			0, {startOfLine}},
	{"up",		"Move cursor up one line.",		0, {upLine}},
	{"upPage",	"Move cursor up one page.",		0, {upPage}},
	{NULL, NULL, 0, {NULL}}
};


// Hmm, back space, page up, and page down don't seem to have bindable commands according to the web page, but they are bound to keys anyway.
struct keyCommand simpleNanoKeys[] =
{
// TODO - Delete key is ^H dammit.  Find the alternate Esc sequence for Del.
//	{"^H",		"backSpaceChar"},	// ?
	{"BS",		"backSpaceChar"},
	{"^D",		"delete"},
	{"Del",		"delete"},
	{"^N",		"down"},
	{"Down",	"down"},
	{"^E",		"end"},
	{"End",		"end"},
	{"^X",		"exit"},
	{"F2",		"quit"},
	{"^O",		"writeout"},
	{"F3",		"writeout"},
	{"^A",		"home"},
	{"Home",	"home"},
	{"^B",		"left"},
	{"Left",	"left"},
	{"^V",		"downPage"},		// ?
	{"PgDn",	"downPage"},
	{"^Y",		"upPage"},		// ?
	{"PgUp",	"upPage"},
	{"Return",	"enter"},		// TODO - Not sure if this is correct.
	{"^F",		"right"},
	{"Right",	"right"},
	{"^P",		"up"},
	{"Up",		"up"},
	{NULL, NULL}
};

struct mode simpleNanoMode[] =
{
    {simpleNanoKeys, NULL, NULL, 0},
    {NULL, NULL, NULL}
};

struct context simpleNano =
{
    simpleNanoCommands,
    simpleNanoMode,
    NULL,
    NULL,
    NULL
};


// Construct a simple vi editor.
// The "command line" modes are /, ?, :, and !,
//   / is regex search.
//   ? is regex search backwards.
//   : is ex command mode.
//   ! is replace text with output from shell command mode.
// Arrow keys do the right thing in "normal" mode, but not in insert mode.
// "i" goes into insert mode, "Esc" (or Ctrl-[ or Ctrl-C) gets you out.  So much for "you can do it all touch typing on the home row".  Pffft
//   Ah, the Esc key WAS a lot closer to the home row (where Tab usually is now) on the original keyboard vi was designed for, ADM3A.
//   Which is also the keyboard with the arrow keys marked on h, j, k, and l keys.
//   Did I mention that vi is just a horrid historic relic that should have died long ago?
//   Emacs looks to have the same problem, originally designed for an ancient keyboard that is nothing like what people actually use these days.
// "h", "j", "k", "l" move cursor, which is just random keys for dvorak users.
// ":" goes into ex command mode.
// ":q" deletes current window in vim.
// ":qa!" goes into ex mode and does some sort of quit command.
//   The 'q' is short for quit, the ! is an optional argument to quit.  No idea yet what the a is for, all windows?
// Del or "x" to delete a character.  Del in insert mode.
// "X" to backspace.  BS or Ctrl-H to backspace in insert mode.
//   NOTE - Backspace in normal mode just moves left.
// Tab or Ctrl-I to insert a tab in insert mode.
// Return in normal mode goes to the start of the next line, or splits the line in insert mode.
// ":help" opens a window with help text.
// Vim window commands.

// Vi needs extra variables and functions.
static int viTempExMode;

void viMode(view *view, event *event)
{
	currentBox->view->mode = 0;
	commandMode = 0;
	viTempExMode = 0;
}

void viInsertMode(view *view, event *event)
{
	currentBox->view->mode = 1;
	commandMode = 0;
}

void viExMode(view *view, event *event)
{
	currentBox->view->mode = 2;
	commandMode = 1;
	// TODO - Should change this based on the event, : or Q.
	viTempExMode = 1;
	commandLine->prompt = xrealloc(commandLine->prompt, 2);
	strcpy(commandLine->prompt, ":");
}

void viBackSpaceChar(view *view, event *event)
{
	if ((2 == currentBox->view->mode) && (0 == view->cX) && viTempExMode)
		viMode(view, event);
	else
		backSpaceChar(view, event);
}

void viStartOfNextLine(view *view, event *event)
{
	startOfLine(view, event);
	downLine(view, event);
}

// TODO - ex uses "shortest unique string" to match commands, should implement that, and do it for the other contexts to.
struct function simpleViCommands[] =
{
	// These are actual ex commands.
	{"insert",	"Switch to insert mode.",		0, {viInsertMode}},
	{"quit",	"Quit the application.",		0, {quit}},
	{"visual",	"Switch to visual mode.",		0, {viMode}},
	{"write",	"Save.",				0, {saveContent}},

	// These are not ex commands.
	{"backSpaceChar","Back space last character.",		0, {viBackSpaceChar}},
	{"deleteBox",	"Delete a box.",			0, {deleteBox}},
	{"deleteChar",	"Delete current character.",		0, {deleteChar}},
	{"downLine",	"Move cursor down one line.",		0, {downLine}},
	{"downPage",	"Move cursor down one page.",		0, {downPage}},
	{"endOfLine",	"Go to end of line.",			0, {endOfLine}},
	{"executeLine",	"Execute a line as a script.",		0, {executeLine}},
	{"exMode",	"Switch to ex mode.",			0, {viExMode}},
	{"leftChar",	"Move cursor left one character.",	0, {leftChar}},
	{"rightChar",	"Move cursor right one character.",	0, {rightChar}},
	{"splitH",	"Split box in half horizontally.",	0, {halveBoxHorizontally}},
	{"splitLine",	"Split line at cursor.",		0, {splitLine}},
	{"splitV",	"Split box in half vertically.",	0, {halveBoxVertically}},
	{"startOfLine",	"Go to start of line.",			0, {startOfLine}},
	{"startOfNLine","Go to start of next line.",		0, {viStartOfNextLine}},
	{"switchBoxes",	"Switch to another box.",		0, {switchBoxes}},
	{"upLine",	"Move cursor up one line.",		0, {upLine}},
	{"upPage",	"Move cursor up one page.",		0, {upPage}},
	{NULL, NULL, 0, {NULL}}
};

struct keyCommand simpleViNormalKeys[] =
{
	{"BS",		"leftChar"},
	{"X",		"backSpaceChar"},
	{"Del",		"deleteChar"},
	{"x",		"deleteChar"},
	{"Down",	"downLine"},
	{"j",		"downLine"},
	{"End",		"endOfLine"},
	{"Home",	"startOfLine"},
	{"Left",	"leftChar"},
	{"h",		"leftChar"},
	{"PgDn",	"downPage"},
	{"^F",		"downPage"},
	{"PgUp",	"upPage"},
	{"^B",		"upPage"},
	{"Return",	"startOfNextLine"},
	{"Right",	"rightChar"},
	{"l",		"rightChar"},
	{"i",		"insert"},
	{":",		"exMode"},	// This is the temporary ex mode that you can backspace out of.  Or any command backs you out.
	{"Q",		"exMode"},	// This is the ex mode you need to do the "visual" command to get out of.
	{"^Wv",		"splitV"},
	{"^W^V",	"splitV"},
	{"^Ws",		"splitH"},
	{"^WS",		"splitH"},
	{"^W^S",	"splitH"},
	{"^Ww",		"switchBoxes"},
	{"^W^W",	"switchBoxes"},
	{"^Wq",		"deleteBox"},
	{"^W^Q",	"deleteBox"},
	{"Up",		"upLine"},
	{"k",		"upLine"},
	{NULL, NULL}
};

struct keyCommand simpleViInsertKeys[] =
{
	{"BS",		"backSpaceChar"},
	{"Del",		"deleteChar"},
	{"Return",	"splitLine"},
	{"^[",		"visual"},
	{"^C",		"visual"},	// TODO - Ctrl-C is filtered by the default signal handling, which we might want to disable.
	{NULL, NULL}
};

struct keyCommand simpleExKeys[] =
{
	{"BS",		"backSpaceChar"},
	{"Del",		"deleteChar"},
	{"Down",	"downLine"},
	{"End",		"endOfLine"},
	{"Home",	"startOfLine"},
	{"Left",	"leftChar"},
	{"Return",	"executeLine"},
	{"Right",	"rightChar"},
	{"^[",		"visual"},
	{"Up",		"upLine"},
	{NULL, NULL}
};

struct mode simpleViMode[] =
{
    {simpleViNormalKeys, NULL, NULL, 0},
    {simpleViInsertKeys, NULL, NULL, 0},
    {simpleExKeys, NULL, NULL, 1},
    {NULL, NULL, NULL}
};

struct context simpleVi =
{
    simpleViCommands,
    simpleViMode,
    NULL,
    NULL,
    NULL
};


// TODO - simple sed editor?  May be out of scope for "simple", so leave it until later?
// Probably entirely useless for "simple".


// TODO - have any unrecognised escape key sequence start up a new box (split one) to show the "show keys" content.
// That just adds each "Key is X" to the end of the content, and allows scrolling, as well as switching between other boxes.


void boxes_main(void)
{
	struct context *context = &simpleMcedit;	// The default is mcedit, coz that's what I use.
	char *prompt = "Enter a command : ";
	unsigned W = 80, H = 24;

	// TODO - Should do an isatty() here, though not sure about the usefullness of driving this from a script or redirected input, since it's supposed to be a UI for terminals.
	//          It would STILL need the terminal size for output though.  Perhaps just bitch and abort if it's not a tty?
	//          On the other hand, sed don't need no stinkin' UI.  And things like more or less should be usable on the end of a pipe.

	// TODO - set up a handler for SIGWINCH to find out when the terminal has been resized.
	terminal_size(&W, &H);
	if (toys.optflags & FLAG_w)
		W = TT.w;
	if (toys.optflags & FLAG_h)
		H = TT.h;

	TT.stillRunning = 1;

	// For testing purposes, figure out which context we use.  When this gets real, the toybox multiplexer will sort this out for us instead.
	if (toys.optflags & FLAG_m)
	{
		if (strcmp(TT.mode, "emacs") == 0)
			context = &simpleEmacs;
		else if (strcmp(TT.mode, "joe") == 0)
			context = &simpleJoe;
		else if (strcmp(TT.mode, "less") == 0)
			context = &simpleLess;
		else if (strcmp(TT.mode, "mcedit") == 0)
			context = &simpleMcedit;
		else if (strcmp(TT.mode, "more") == 0)
			context = &simpleMore;
		else if (strcmp(TT.mode, "nano") == 0)
			context = &simpleNano;
		else if (strcmp(TT.mode, "vi") == 0)
			context = &simpleVi;
	}

	// Create the main box.  Right now the system needs one for wrapping around while switching.  The H - 1 bit is to leave room for our example command line.
	rootBox = addBox("root", context, toys.optargs[0], 0, 0, W, H - 1);

	// Create the command line view, sharing the same context as the root.  It will differentiate based on the view mode of the current box.
	// Also load the command line history as it's file.
	// TODO - different contexts will have different history files, though what to do about ones with no history, and ones with different histories for different modes?
	commandLine = addView("command", rootBox->view->content->context, ".boxes.history", 0, H, W, 1);
	// Add a prompt to it.
	commandLine->prompt = xrealloc(commandLine->prompt, strlen(prompt) + 1);
	strcpy(commandLine->prompt, prompt);
	// Move to the end of the history.
	moveCursorAbsolute(commandLine, 0, commandLine->content->lines.length, 0, 0);

	// Run the main loop.
	currentBox = rootBox;
	editLine(currentBox->view, -1, -1, -1, -1);

	puts("\n");
	fflush(stdout);
}

