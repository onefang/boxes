/* boxes.c - Generic editor development sandbox.
 *
 * Copyright 2012 David Seikel <won_fang@yahoo.com.au>
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
#include "lib/handlekeys.h"

GLOBALS(
  char *mode;
  long h, w;
)

#define TT this.boxes

#define FLAG_a  2
#define FLAG_m  4
#define FLAG_h  8
#define FLAG_w  16


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
 *
 * These are the escape sequences we send -
 *    \x1B[m		reset attributes and colours
 *    \x1B[1m		turn on bold
 *    \x1B[%d;%dH	move cursor
 * Plus some experimentation with turning on mouse reporting that's not
 * currently used.
 *
 *
 * TODO - disentangle boxes from views.
 *
 * TODO - should split this up into editing, UI, and boxes parts,
 * so the developer can leave out bits they are not using.
 *
 * TODO - Show status line instead of command line when it's not being edited.
 *
 * TODO - should review it all for UTF8 readiness.  Think I can pull that off
 * by keeping everything on the output side as "screen position", and using
 * the formatter to sort out the input to output mapping.
 *
 * TODO - see if there are any simple shortcuts to avoid recalculating
 * everything all the time.  And to avoid screen redraws.
 */

/* Robs "It's too big" lament.

> So when you give me code, assume I'm dumber than you. Because in this 
> context, I am. I need bite sized pieces, each of which I can
> understand in its entirety before moving on to the next.  

As I mentioned in my last email to the Aboriginal linux list, I wrote
the large blob so I could see how the ideas all work to do the generic
editor stuff and other things.  It kinda needs to be that big to do
anything that is actually useful.  So, onto musings about cutting it up
into bite sized bits...

You mentioned on the Aboriginal Linux list that you wanted a
"readline", and you listed some features.  My reply was that you had
basically listed the features of my "basic editor".  The main
difference between "readline" and a full screen editor, is that the
editor is fullscreen, while the "readline" is one line.  They both have
to deal with a list of lines, going up and down through those lines,
editing the contents of those lines, one line at a time.  For persistent
line history, they both have to save and load those lines to a file.
Other than that "readline" adds other behaviour, like moving the
current line to the end when you hit return and presenting that line to
the caller (here's the command).  So just making "readline" wont cut
out much of the code.  In fact, my "readline" is really just a thin
wrapper around the rest of the code.

Starting from the other end of the size spectrum, I guess "find out
terminal size" is a small enough bite sized chunk.  To actually do
anything useful with that you would probably start to write stuff I've
already written though.  Would be better off just using the stuff I've
already written.  On the other hand, maybe that would be useful for
"ls" listings and the like, then we can start with just that bit?

I guess the smallest useful command I have in my huge blob of code
would be "more".  I could even cut it down more.  Show a page of text,
show the next page of text when you hit the space bar, quit when you
hit "q".  Even then, I would estimate it would only cut out a third of
the code.

On the other hand, there's a bunch of crap in that blob that is for
future plans, and is not doing anything now.

In the end though, breaking it up into suitable bite sized bits might
be almost as hard as writing it in the first place.  I'll see what I
can do, when I find some time.  I did want to break it up into three
bits anyway, and there's a TODO to untangle a couple of bits that are
currently too tightly coupled.

*/

/* Robs contradiction

On Thu, 27 Dec 2012 06:06:53 -0600 Rob Landley <rob@landley.net> wrote:

> On 12/27/2012 12:56:07 AM, David Seikel wrote:  
> > On Thu, 27 Dec 2012 00:37:46 -0600 Rob Landley <rob@landley.net>  
> > wrote:  
> > > Since ls isn't doiing any of that, probing would just be awkward
> > > so toysh should set COLUMNS to a sane value. The problem is,
> > > we're not using toysh yet because it's still just a stub...  
> > 
> > I got some or all of that terminal sizing stuff in my editor thingy
> > already if I remember correctly.  I remember you wanted me to cut it
> > down into tiny bits to start with, so you don't have to digest the  
> > huge lump I sent.  
> 
> Basically what I'd like is a self-contained line reader. More or less
> a getline variant that can handle cursoring left and right to insert  
> stuff, handle backspace past a wordwrap (which on unix requires
> knowing the screen width and your current cursor position), and one
> that lets up/down escape sequences be hooked for less/more screen
> scrolling, vi line movement, shell command history, and so on.  

Which is most of an editor already, so how to break it up into bite
sized morsels?

> There's sort of three levels here, the first is "parse raw input,  
> assembling escape sequences into cursor-left and similar as
> necessary". (That's the one I had to redo in busybox vi because they
> didn't do it right.)
> 
> The second is "edit a line of text, handling cursoring _within_ the  
> line". That's the better/interactive getline().
> 
> The third is handling escapes from that line triggering behavior in
> the larger command (cursor up and cursor down do different things in
> less, in vi, and in shell command history).
> 
> The fiddly bit is that terminal_size() sort of has to integrate with  
> all of these. Possibly I need to have width and height be members of  
> struct toy_context. Or have the better_getline() take a pointer to a  
> state structure it can update, containing that info...

*/


static char *borderChars[][6] =
{
  {"-",    "|",    "+",    "+",    "+",    "+"},     // "stick" characters.
  {"\xE2\x94\x80", "\xE2\x94\x82", "\xE2\x94\x8C", "\xE2\x94\x90", "\xE2\x94\x94", "\xE2\x94\x98"},  // UTF-8
  {"\x71", "\x78", "\x6C", "\x6B", "\x6D", "\x6A"},  // VT100 alternate character set.
  {"\xC4", "\xB3", "\xDA", "\xBF", "\xC0", "\xD9"}   // DOS
};

static char *borderCharsCurrent[][6] =
{
  {"=",    "#",    "+",    "+",    "+",    "+"},     // "stick" characters.
  {"\xE2\x95\x90", "\xE2\x95\x91", "\xE2\x95\x94", "\xE2\x95\x97", "\xE2\x95\x9A", "\xE2\x95\x9D"},  // UTF-8
  {"\x71", "\x78", "\x6C", "\x6B", "\x6D", "\x6A"},  // VT100 alternate character set has none of these.  B-(
  {"\xCD", "\xBA", "\xC9", "\xBB", "\xC8", "\xBC"}   // DOS
};


typedef struct _box box;
typedef struct _view view;

typedef void (*boxFunction) (box *box);
typedef void (*eventHandler) (view *view);

struct function
{
  char *name;		// Name for script purposes.
  char *description;	// Human name for the menus.
  char type;
  union
  {
    eventHandler handler;
    char *scriptCallback;
  };
};

struct keyCommand
{
  char *key, *command;
};

struct item
{
  char *text;		// What's shown to humans.
  struct key *key;	// Shortcut key while the menu is displayed.
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
  char *text, *command;
};

// TODO - a generic "part of text", and what is used to define them.
// For instance - word, line, paragraph, section.
// Each context can have a collection of these.

struct mode
{
  struct keyCommand *keys;	// An array of key to command mappings.
  struct item *items;		// An array of top level menu items.
  struct item *functionKeys;	// An array of single level "menus".  Used to show key commands.
  uint8_t flags;		// commandMode.
};

/*
Have a common menu up the top.
  MC has a menu that changes per mode.
  Nano has no menu.
Have a common display of certain keys down the bottom.
  MC is one row of F1 to F10, but changes for edit / view / file browse.  But those are contexts here.
  Nano is 12 random Ctrl keys, possibly in two lines, that changes depending on the editor mode, like editing the prompt line for various reasons, help.
*/
struct context			// Defines a context for content.  Text viewer, editor, file browser for instance.
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
  boxFunction doneRedraw;	// The box is done with it's redraw, so we can free the damage list or whatever now.
  boxFunction delete;
    // This can be used as the sub struct for various context types.  Like viewer, editor, file browser, top, etc.
    // Could even be an object hierarchy, like generic editor, which Basic vi inherits from.
    //   Or not, since the commands might be different / more of them.
};

// TODO - might be better off just having a general purpose "widget" which includes details of where it gets attached.
// Status lines can have them to.
struct border
{
  struct borderWidget *topLeft, *topMiddle, *topRight;
  struct borderWidget *bottomLeft, *bottomMiddle, *bottomRight;
  struct borderWidget *left, *right;
};

struct line
{
  struct line *next, *prev;
  uint32_t length;	// Careful, this is the length of the allocated memory for real lines, but the number of lines in the header node.
  char *line;		// Should be blank for the header.
};

struct damage
{
  struct damage *next;	// A list for faster draws?
  uint16_t X, Y, W, H;	// The rectangle to be redrawn.
  uint16_t offset;	// Offest from the left for showing lines.
  struct line *lines;	// Pointer to a list of text lines, or NULL.
    // Note - likely a pointer into the middle of the line list in a content.
};

struct content		// For various instances of context types.  
    // Editor / text viewer might have several files open, so one of these per file.
    // MC might have several directories open, one of these per directory.  No idea why you might want to do this.  lol
{
  struct context *context;
  char *name, *file, *path;
  struct line lines;
//  file type
//  double linked list of bookmarks, pointer to line, character position, length (or ending position?), type, blob for types to keep context.
  uint16_t minW, minH, maxW, maxH;
  uint8_t flags;	// readOnly, modified.
    // This can be used as the sub struct for various content types.
};

struct _view
{
  struct content *content;
  box *box;
  struct border *border;	// Can be NULL.
  char *statusLine;		// Text of the status line, or NULL if none.
  int mode;			// For those contexts that can be modal.  Normally used to index the keys, menus, and key displays.
  struct damage *damage;	// Can be NULL.  If not NULL after context->doneRedraw(), box will free it and it's children.
    // TODO - Gotta be careful of overlapping views.
  void *data;			// The context controls this blob, it's specific to each box.
  uint32_t offsetX, offsetY;	// Offset within the content, coz box handles scrolling, usually.
  uint16_t X, Y, W, H;		// Position and size of the content area within the box.  Calculated, but cached coz that might be needed for speed.
  uint16_t cX, cY;		// Cursor position within the content.
  uint16_t iX, oW;		// Cursor position inside the lines input text, in case the formatter makes it different, and output length.
  char *output;			// The current line formatted for output.
  uint8_t flags;		// redrawStatus, redrawBorder;

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
  view *view;			// This boxes view into it's content.  For sharing contents, like a split pane editor for instance, there might be more than one box with this content, but a different view.
    // If it's just a parent box, it wont have this, so just make it a damn pointer, that's the simplest thing.  lol
    // TODO - Are parent boxes getting a view anyway?
  uint16_t X, Y, W, H;		// Position and size of the box itself, not the content.  Calculated, but cached coz that might be needed for speed.
  float split;			// Ratio of sub1's part of the split, the sub2 box gets the rest.
  uint8_t flags;		// Various flags.
};


// Sometimes you just can't avoid circular definitions.
void drawBox(box *box);


#define BOX_HSPLIT  1	// Marks if it's a horizontally or vertically split.
#define BOX_BORDER  2	// Mark if it has a border, often full screen boxes wont.

static int overWriteMode;
static box *rootBox;	// Parent of the rest of the boxes, or the only box.  Always a full screen.
static box *currentBox;
static view *commandLine;
static int commandMode;

#define MEM_SIZE  128	// Chunk size for line memory allocation.

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

    while (&(content->lines) != line)  // We are at the end if we have wrapped to the beginning.
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
  struct content *result  = xzalloc(sizeof(struct content));

  result->lines.next  = &(result->lines);
  result->lines.prev  = &(result->lines);
  result->name    = strdup(name);
  result->context    = context;

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
   * moosh  == NULL  a deletion
   * length == 0    simple insertion
   * length <  mooshlen  delete some, insert moosh
   * length == mooshlen  exact overwrite.
   * length >  mooshlen  delete a lot, insert moosh
   */

  mooshLen -= length;
  resultLen = limit + mooshLen;

  // If we need more space, allocate more.
  if (resultLen > result->length)
  {
    result->length = resultLen + MEM_SIZE;
    result->line = xrealloc(result->line, result->length);
  }

  if (limit <= index)  // At end, just add to end.
  {
    // TODO - Possibly add spaces to pad out to where index is?
    //          Would be needed for "go beyond end of line" and "column blocks".
    //          Both of those are advanced editing.
    index = limit;
    insert = 1;
  }

  pos = &(result->line[index]);

  if (insert)  // Insert / delete before current character, so move it and the rest up / down mooshLen bytes.
  {
    if (0 < mooshLen)  // Gotta move things up.
    {
      c = &(result->line[limit]);
      while (c >= pos)
      {
        *(c + mooshLen) = *c;
        c--;
      }
    }
    else if (0 > mooshLen)  // Gotta move things down.
    {
      c = pos;
      while (*c)
      {
        *c = *(c - mooshLen);  // A double negative.
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

  if ('\0' != left[0])  // Assumes that if one side has a border, then so does the other.
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
  if ('\0' == left[0])  // Assumes that if one side has a border, then so does the other.
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

/* FIXME - We get passed a NULL input, apparently when the file length is close to the screen length. -
> On Thu, 6 Sep 2012 11:56:17 +0800 Roy Tam <roytam@gmail.com> wrote:
>   
> > 2012/9/6 David Seikel <onefang@gmail.com>:  
> > >> Program received signal SIGSEGV, Segmentation fault.
> > >> formatLine (view=0x8069168, input=0x0, output=0x806919c)
> > >>     at toys/other/boxes.c:843
> > >> 843        int len = strlen(input), i = 0, o = 0;
> > >> (gdb) bt
> > >> #0  formatLine (view=0x8069168, input=0x0, output=0x806919c)
> > >>     at toys/other/boxes.c:843
> > >> #1  0x0804f1dd in moveCursorAbsolute (view=0x8069168, cX=0,
> > >> cY=10, sX=0, sY=0) at toys/other/boxes.c:951
> > >> #2  0x0804f367 in moveCursorRelative (view=0x8069168, cX=0,
> > >> cY=10, sX=0, sY=0) at toys/other/boxes.c:1011
> > >> #3  0x0804f479 in upLine (view=0x8069168, event=0x0) at
> > >> toys/other/boxes.c:1442 #4  0x0804fb63 in handleKey
> > >> (view=0x8069168, i=2, keyName=<optimized out>, buffer=0xbffffad8
> > >> "\033[A") at toys/other/boxes.c:1593 #5  0x0805008d in editLine
> > >> (view=0x8069168, X=-1, Y=-1, W=-1, H=-1) at
> > >> toys/other/boxes.c:1785 #6  0x08050288 in boxes_main () at
> > >> toys/other/boxes.c:2482 #7  0x0804b262 in toy_exec
> > >> (argv=0xbffffd58) at main.c:104 #8  0x0804b29d in toybox_main ()
> > >> at main.c:118 #9  0x0804b262 in toy_exec (argv=0xbffffd54) at
> > >> main.c:104 #10 0x0804b29d in toybox_main () at main.c:118
> > >> #11 0x0804affa in main (argc=5, argv=0xbffffd54) at main.c:159  
> > >
> > > No segfault here when I try that, nor with different files.  As I
> > > said, it has bugs.  I have seen other cases before when NULL lines
> > > are passed around near that code.  Guess I've not got them all.
> > > Might help to mention what terminal proggy you are using, it's
> > > size in characters, toybox version, OS, etc.  Something is
> > > different between you and me.  
> > 
> > Terminal Program: PuTTY
> > Windows size: 80 * 25 chars
> > Toybox version: hg head
> > OS: Linux 3.2 (Debian wheezy)
> > File: Toybox LICENSE file  
> 
> I'll install PuTTY, try toybox hg head, and play with them later, see
> if I can reproduce that segfault.  I use the last released tarball of
> toybox, an old Ubuntu, and a bunch of other terminal proggies.  I did
> not know that PuTTY had been ported to Unix.
> 
> But as I mentioned, not interested in a bug hunt right now.  That will
> be more appropriate when it's less of a "sandbox for testing the
> ideas" and more of a "good enough to bother using".  Always good to
> have other terminals for testing though.  

Seems to be sensitive to the number of lines.  On my usual huge
roxterm, using either toybox 0.4 or hg head, doing this -

./toybox boxes -m less LICENSE -h 25

and just hitting down arrow until you get to the bottom of the page
segfaults as well.  -h means "pretend the terminal is that many lines
high", there's a similar -w as well.  80x25 in any other terminal did
the same.  For that particular file (17 lines long), any value of -h
between 19 and 34 will segfault after some moving around.  -h 35 makes
it segfault straight away.  Less than 19 or over 35 is fine.  Longer
files don't have that problem, though I suspect it will happen on
shortish files where the number of lines is about the length of the
screen.

I guess that somewhere I'm doing maths wrong and getting an out of
bounds line number when the file length is close to the screen length.
I'll make note of that, and fix it when I next get time to beat on
boxes.

Thanks for reporting it Roy.
*/

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
      len--;  // Not counting the actual tab character itself.
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
  else  // Only time we are not drawing the current line, and only used for drawing the entire page.
    len = formatLine(NULL, contents, &(temp));

  if (offset > len)
    offset = len;
  drawLine(y, start, end, left, internal, &(temp[offset]), right, current);
}

void updateLine(view *view)
{
  int y, len;

  // Coz things might change out from under us, find the current view.  Again.
  if (commandMode)	view = commandLine;
  else		view = currentBox->view;

  // TODO - When doing scripts and such, might want to turn off the line update until finished.
  // Draw the prompt and the current line.
  y = view->Y + (view->cY - view->offsetY);
  len = strlen(view->prompt);
  drawLine(y, view->X, view->X + view->W, "", " ", view->prompt, "", 0);
  drawContentLine(view, y, view->X + len, view->X + view->W, "", " ", view->line->line, "", 1);
  // Move the cursor.
  printf("\x1B[%d;%dH", y + 1, view->X + len + (view->cX - view->offsetX) + 1);
  fflush(stdout);
}

void doCommand(view *view, char *command)
{
  if (command)
  {
    struct function *functions = view->content->context->commands;
    int i;

// TODO - Some editors have a shortcut command concept.  The smallest unique first part of each command will match, as well as anything longer.
//          A further complication is if we are not implementing some commands that might change what is "shortest unique prefix".

    for (i = 0; functions[i].name; i++)
    {
      if (strcmp(functions[i].name, command) == 0)
      {
        if (functions[i].handler)
        {
          functions[i].handler(view);
          updateLine(view);
        }
        break;
      }
    }
  }
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
  if (0 > cY)    // Trying to move before the beginning of the content.
    cY = 0;
  else if (lY < cY)  // Trying to move beyond end of the content.
    cY = lY;
  if (0 > cX)    // Trying to move before the beginning of the line.
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
  else if (lX < cX)  // Trying to move beyond end of line.
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
      if (view->content->lines.prev == newLine)  // We are at the end if we have wrapped to the beginning.
        break;
    }
    else
    {
      newLine = newLine->prev;
      nY--;
      if (view->content->lines.next == newLine)  // We are at the end if we have wrapped to the beginning.
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

  if (oY > cY)      // Trying to move above the box.
    oY += cY - oY;
  else if ((oY + h) < cY)    // Trying to move below the box
    oY += cY - (oY + h);
  if (oX > cX)      // Trying to move to the left of the box.
    oX += cX - oX;
  else if ((oX + w) <= cX)  // Trying to move to the right of the box.
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
      if (&(box->view->content->lines) == lines)  // We are at the end if we have wrapped to the beginning.
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
      if (&(box->view->content->lines) == lines)  // We are at the end if we have wrapped to the beginning.
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
  if (box->sub1)  // If there's one sub box, there's always two.
  {
    drawBoxes(box->sub1);
    drawBoxes(box->sub2);
  }
  else
    drawBox(box);
}

void calcBoxes(box *box)
{
  if (box->sub1)  // If there's one sub box, there's always two.
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

void deleteBox(view *view)
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
  else if (1.0 <= split)  // User meant to unsplit, and it may already be split.
  {
    // Actually, this means that the OTHER sub box gets deleted.
    if (box->parent)
    {
      if (box == box->parent->sub1)
        deleteBox(box->parent->sub2->view);
      else
        deleteBox(box->parent->sub1->view);
    }
    return;
  }
  else if (0.0 < split)  // This is the normal case, so do nothing.
  {
  }
  else      // User meant to delete this, zero split.
  {
      deleteBox(box->view);
      return;
  }
  if (box->flags & BOX_HSPLIT)
    size = box->H;
  else
    size = box->W;
  if (6 > size)    // Is there room for 2 borders for each sub box and one character of content each?
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
  if (NULL == box->sub1)  // If not split already, do so.
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
void switchBoxes(view *view)
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

void halveBoxHorizontally(view *view)
{
  view->box->flags |= BOX_HSPLIT;
  splitBox(view->box, 0.5);
}

void halveBoxVertically(view *view)
{
  view->box->flags &= ~BOX_HSPLIT;
  splitBox(view->box, 0.5);
}

void switchMode(view *view)
{
  currentBox->view->mode++;
  // Assumes that modes will always have a key mapping, which I think is a safe bet.
  if (NULL == currentBox->view->content->context->modes[currentBox->view->mode].keys)
    currentBox->view->mode = 0;
  commandMode = currentBox->view->content->context->modes[currentBox->view->mode].flags & 1;
}

void leftChar(view *view)
{
  moveCursorRelative(view, -1, 0, 0, 0);
}

void rightChar(view *view)
{
  moveCursorRelative(view, 1, 0, 0, 0);
}

void upLine(view *view)
{
  moveCursorRelative(view, 0, -1, 0, 0);
}

void downLine(view *view)
{
  moveCursorRelative(view, 0, 1, 0, 0);
}

void upPage(view *view)
{
  moveCursorRelative(view, 0, 0 - (view->H - 1), 0, 0 - (view->H - 1));
}

void downPage(view *view)
{
  moveCursorRelative(view, 0, view->H - 1, 0, view->H - 1);
}

void endOfLine(view *view)
{
  moveCursorAbsolute(view, strlen(view->prompt) + view->oW, view->cY, 0, 0);
}

void startOfLine(view *view)
{
  // TODO - add the advanced editing "smart home".
  moveCursorAbsolute(view, strlen(view->prompt), view->cY, 0, 0);
}

void splitLine(view *view)
{
  // TODO - should move this into mooshLines().
  addLine(view->content, view->line, &(view->line->line[view->iX]), 0);
  view->line->line[view->iX] = '\0';
  moveCursorAbsolute(view, 0, view->cY + 1, 0, 0);
  if (view->box)
    drawBox(view->box);
}

void deleteChar(view *view)
{
  // TODO - should move this into mooshLines().
  // If we are at the end of the line, then join this and the next line.
  if (view->oW == view->cX)
  {
    // Only if there IS a next line.
    if (&(view->content->lines) != view->line->next)
    {
      mooshStrings(view->line, view->line->next->line, view->iX, 1, !overWriteMode);
      view->line->next->line = NULL;
      freeLine(view->content, view->line->next);
      // TODO - should check if we are on the last page, then deal with scrolling.
      if (view->box)
        drawBox(view->box);
    }
  }
  else
    mooshStrings(view->line, NULL, view->iX, 1, !overWriteMode);
}

void backSpaceChar(view *view)
{
  if (moveCursorRelative(view, -1, 0, 0, 0))
    deleteChar(view);
}

void saveContent(view *view)
{
  saveFile(view->content);
}

void executeLine(view *view)
{
  struct line *result = view->line;

  // Don't bother doing much if there's nothing on this line.
  if (result->line[0])
  {
    doCommand(currentBox->view, result->line);
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
      endOfLine(view);
      splitLine(view);
    }
  }

  saveFile(view->content);
}

void quit(view *view)
{
  handle_keys_quit();
}

void nop(view *view)
{
  // 'tis a nop, don't actually do anything.
}


typedef void (*CSIhandler) (long extra, int *code, int count);

struct CSI
{
  char *code;
  CSIhandler func;
};

static void termSize(long extra, int *params, int count)
{
  struct _view *view = (struct _view *) extra;		// Though we pretty much stomp on this straight away.
  int r = params[0], c = params[1];

  // The defaults are 1, which get ignored by the heuristic below.
  // Check it's not an F3 key variation, coz some of them use the same CSI function code.
  // This is a heuristic, we are checking against an unusable terminal size.
  // TODO - Double check what the maximum F3 variations can be.
  if ((2 == count) && (8 < r) && (8 < c))
  {
    // FIXME - The change is not being propogated to everything properly.
    sizeViewToBox(rootBox, rootBox->X, rootBox->Y, c, r - 1);
    calcBoxes(rootBox);
    drawBoxes(rootBox);

    // Move the cursor to where it is, to check it's not now outside the terminal window.
    moveCursorAbsolute(rootBox->view, rootBox->view->cX, rootBox->view->cY, 0, 0);

    // We have no idea which is the current view now.
    if (commandMode)	view = commandLine;
    else		view = currentBox->view;
    updateLine(view);
  }
}

struct CSI CSIcommands[] =
{
  {"R", termSize}	// Parameters are cursor line and column.  Note this may be sent at other times, not just during terminal resize.
};

static void handleCSI(long extra, char *command, int *params, int count)
{
  int j;

  for (j = 0; j < (sizeof(CSIcommands) / sizeof(*CSIcommands)); j++)
  {
    if (strcmp(CSIcommands[j].code, command) == 0)
    {
      CSIcommands[j].func(extra, params, count);
      break;
    }
  }
}

// Callback for incoming key sequences from the user.
static int handleKeySequence(long extra, char *sequence, int isTranslated)
{
  struct _view *view = (struct _view *) extra;		// Though we pretty much stomp on this straight away.
  struct keyCommand *commands = currentBox->view->content->context->modes[currentBox->view->mode].keys;
  int j, l = strlen(sequence);

  // Coz things might change out from under us, find the current view.
  if (commandMode)	view = commandLine;
  else		view = currentBox->view;

  // Search for a key sequence bound to a command.
  for (j = 0; commands[j].key; j++)
  {
    if (strncmp(commands[j].key, sequence, l) == 0)
    {
      // If it's a partial match, keep accumulating them.
      if (strlen(commands[j].key) != l)
        return 0;
      else
      {
        doCommand(view, commands[j].command);
        return 1;
      }
    }
  }

  // See if it's ordinary keys.
  // NOTE - with vi style ordinary keys can be commands,
  // but they would be found by the command check above first.
  if (!isTranslated)
  {
    // TODO - Should check for tabs to, and insert them.
    //        Though better off having a function for that?
    mooshStrings(view->line, sequence, view->iX, 0, !overWriteMode);
    view->oW = formatLine(view, view->line->line, &(view->output));
    moveCursorRelative(view, strlen(sequence), 0, 0, 0);
    updateLine(view);
  }

  // Tell handle_keys to drop it, coz we dealt with it, or it's not one of ours.
  return 1;
}


// The default command to function mappings, with help text.  Any editor that does not have it's own commands can use these for keystroke binding and such.
// Though most of the editors have their own variation.  Maybe just use the joe one as default, it uses short names at least.
struct function simpleEditCommands[] =
{
  {"backSpaceChar",	"Back space last character.",		0, {backSpaceChar}},
  {"deleteBox",		"Delete a box.",			0, {deleteBox}},
  {"deleteChar",	"Delete current character.",		0, {deleteChar}},
  {"downLine",		"Move cursor down one line.",		0, {downLine}},
  {"downPage",		"Move cursor down one page.",		0, {downPage}},
  {"endOfLine",		"Go to end of line.",			0, {endOfLine}},
  {"executeLine",	"Execute a line as a script.",		0, {executeLine}},
  {"leftChar",		"Move cursor left one character.",	0, {leftChar}},
  {"quit",		"Quit the application.",		0, {quit}},
  {"rightChar",		"Move cursor right one character.",	0, {rightChar}},
  {"save",		"Save.",				0, {saveContent}},
  {"splitH",		"Split box in half horizontally.",	0, {halveBoxHorizontally}},
  {"splitLine",		"Split line at cursor.",		0, {splitLine}},
  {"splitV",		"Split box in half vertically.",	0, {halveBoxVertically}},
  {"startOfLine",	"Go to start of line.",			0, {startOfLine}},
  {"switchBoxes",	"Switch to another box.",		0, {switchBoxes}},
  {"switchMode",	"Switch between command and box.",	0, {switchMode}},
  {"upLine",		"Move cursor up one line.",		0, {upLine}},
  {"upPage",		"Move cursor up one page.",		0, {upPage}},
  {NULL, NULL, 0, {NULL}}
};

// Construct a simple command line.

// The key to command mappings.
// TODO - Should not move off the ends of the line to the next / previous line.
struct keyCommand simpleCommandKeys[] =
{
  {"BS",	"backSpaceChar"},
  {"Del",	"deleteChar"},
  {"Down",	"downLine"},
  {"End",	"endOfLine"},
  {"F10",	"quit"},
  {"Home",	"startOfLine"},
  {"Left",	"leftChar"},
  {"Enter",	"executeLine"},
  {"Return",	"executeLine"},
  {"Right",	"rightChar"},
  {"Esc",	"switchMode"},
  {"Up",	"upLine"},
  {NULL, NULL}
};


// Construct a simple emacs editor.

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
  {"delete-char",		"Delete current character.",		0, {deleteChar}},
  {"next-line",			"Move cursor down one line.",		0, {downLine}},
  {"scroll-up",			"Move cursor down one page.",		0, {downPage}},
  {"end-of-line",		"Go to end of line.",			0, {endOfLine}},
  {"accept-line",		"Execute a line as a script.",		0, {executeLine}},		// From readline, which uses emacs commands, coz mg at least does not seem to have this.
  {"backward-char",		"Move cursor left one character.",	0, {leftChar}},
  {"save-buffers-kill-emacs",	"Quit the application.",		0, {quit}},			// TODO - Does more than just quit.
  {"forward-char",		"Move cursor right one character.",	0, {rightChar}},
  {"save-buffer",		"Save.",				0, {saveContent}},
  {"split-window-horizontally",	"Split box in half horizontally.",	0, {halveBoxHorizontally}},	// TODO - Making this one up for now, mg does not have it.
  {"newline",			"Split line at cursor.",		0, {splitLine}},
  {"split-window-vertically",	"Split box in half vertically.",	0, {halveBoxVertically}},
  {"beginning-of-line",		"Go to start of line.",			0, {startOfLine}},
  {"other-window",		"Switch to another box.",		0, {switchBoxes}},		// There is also "previous-window" for going in the other direction, which we don't support yet.
  {"execute-extended-command",	"Switch between command and box.",	0, {switchMode}},		// Actually a one time invocation of the command line.
  {"previous-line",		"Move cursor up one line.",		0, {upLine}},
  {"scroll-down",		"Move cursor up one page.",		0, {upPage}},
  {NULL, NULL, 0, {NULL}}
};

// The key to command mappings.
struct keyCommand simpleEmacsKeys[] =
{
  {"BS",	"delete-backward-char"},
  {"Del",	"delete-backward-char"},
  {"^D",	"delete-char"},
  {"Down",	"next-line"},
  {"^N",	"next-line"},
  {"End",	"end-of-line"},
  {"^E",	"end-of-line"},
  {"^X^C",	"save-buffers-kill-emacs"},
  {"^X^S",	"save-buffer"},
  {"Home",	"beginning-of-line"},
  {"^A",	"beginning-of-line"},
  {"Left",	"backward-char"},
  {"^B",	"backward-char"},
  {"PgDn",	"scroll-up"},
  {"^V",	"scroll-up"},
  {"PgUp",	"scroll-down"},
  {"Escv",	"scroll-down"},			// M-v
  {"Enter",	"newline"},
  {"Return",	"newline"},
  {"Right",	"forward-char"},
  {"^F",	"forward-char"},
  {"Escx",	"execute-extended-command"},	// M-x
  {"^X2",	"split-window-vertically"},
  {"^X3",	"split-window-horizontally"},	// TODO - Just making this up for now.
  {"^XP",	"other-window"},
  {"^X0",	"delete-window"},
  {"Up",	"previous-line"},
  {"^P",	"previous-line"},
  {NULL, NULL}
};

struct keyCommand simpleEmacsCommandKeys[] =
{
  {"Del",	"delete-backwards-char"},
  {"^D",	"delete-char"},
  {"Down",	"next-line"},
  {"^N",	"next-line"},
  {"End",	"end-of-line"},
  {"^E",	"end-of-line"},
  {"Home",	"beginning-of-line"},
  {"^A",	"beginning-of-line"},
  {"Left",	"backward-char"},
  {"^B",	"backward-char"},
  {"Up",	"previous-line"},
  {"^P",	"previous-line"},
  {"Enter",	"accept-line"},
  {"Return",	"accept-line"},
  {"Escx",	"execute-extended-command"},
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
//   Can't find a single list of command mappings for joe, gotta search all over.  sigh
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
  {"eol",	"Go to end of line.",			0, {endOfLine}},
  {"ltarw",	"Move cursor left one character.",	0, {leftChar}},
  {"killjoe",	"Quit the application.",		0, {quit}},
  {"rtarw",	"Move cursor right one character.",	0, {rightChar}},
  {"save",	"Save.",				0, {saveContent}},
  {"splitw",	"Split box in half horizontally.",	0, {halveBoxHorizontally}},
  {"open",	"Split line at cursor.",		0, {splitLine}},
  {"bol",	"Go to start of line.",			0, {startOfLine}},
  {"home",	"Go to start of line.",			0, {startOfLine}},
  {"nextw",	"Switch to another box.",		0, {switchBoxes}},	// This is "next window", there's also "previous window" which we don't support yet.
  {"execmd",	"Switch between command and box.",	0, {switchMode}},	// Actually I think this just switches to the command mode, not back and forth.  Or it might execute the actual command.
  {"uparw",	"Move cursor up one line.",		0, {upLine}},
  {"pgup",	"Move cursor up one page.",		0, {upPage}},

  // Not an actual joe command.
  {"executeLine",	"Execute a line as a script.",	0, {executeLine}},	// Perhaps this should be execmd?
  {NULL, NULL, 0, {NULL}}
};

struct keyCommand simpleJoeKeys[] =
{
  {"BS",	"backs"},
  {"^D",	"delch"},
  {"Down",	"dnarw"},
  {"^N",	"dnarw"},
  {"^E",	"eol"},
//  {"F10",	"killjoe"},	// "deleteBox" should do this if it's the last window.
  {"^Kd",	"save"},
  {"^K^D"	"save"},
  {"^A",	"bol"},
  {"Left",	"ltarw"},
  {"^B",	"ltarw"},
  {"^V",	"pgdn"},	// Actually half a page.
  {"^U",	"pgup"},	// Actually half a page.
  {"Enter",	"open"},
  {"Return",	"open"},
  {"Right",	"rtarw"},
  {"^F",	"rtarw"},
  {"Escx",	"execmd"},
  {"Esc^X",	"execmd"},
  {"^Ko",	"splitw"},
  {"^K^O",	"splitw"},
  {"^Kn",	"nextw"},
  {"^K^N",	"nextw"},
  {"^Kx",	"abort"},	// Should ask if it should save if it's been modified.  A good generic thing to do anyway.
  {"^K^X",	"abort"},
  {"Up",	"uparw"},
  {"^P",	"uparw"},
  {NULL, NULL}
};

struct keyCommand simpleJoeCommandKeys[] =
{
  {"BS",	"backs"},
  {"^D",	"delch"},
  {"Down",	"dnarw"},
  {"^N",	"dnarw"},
  {"^E",	"eol"},
  {"^A",	"bol"},
  {"Left",	"ltarw"},
  {"^B",	"ltarw"},
  {"Right",	"rtarw"},
  {"^F",	"rtarw"},
  {"Escx",	"execmd"},
  {"Esc^X",	"execmd"},
  {"Up",	"uparw"},
  {"^P",	"uparw"},
  {"Enter",	"executeLine"},
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
// TODO - actually implement read only mode where up and down one line do actual scrolling instead of cursor movement.

struct keyCommand simpleLessKeys[] =
{
  {"Down",	"downLine"},
  {"j",		"downLine"},
  {"Enter",	"downLine"},
  {"Return",	"downLine"},
  {"End",	"endOfLine"},
  {"q",		"quit"},
  {":q",	"quit"},	// TODO - A vi ism, should do ex command stuff instead.
  {"ZZ",	"quit"},
  {"PgDn",	"downPage"},
  {"f",		"downPage"},
  {" ",		"downPage"},
  {"^F",	"downPage"},
  {"Left",	"leftChar"},
  {"Right",	"rightChar"},
  {"PgUp",	"upPage"},
  {"b",		"upPage"},
  {"^B",	"upPage"},
  {"Up",	"upLine"},
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
  {"Enter",	"downLine"},
  {"Return",	"downLine"},
  {"q",		"quit"},
  {":q",	"quit"},	// See comments for "less".
  {"ZZ",	"quit"},
  {"f",		"downPage"},
  {" ",		"downPage"},
  {"^F",	"downPage"},
  {"b",		"upPage"},
  {"^B",	"upPage"},
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
  {"BS",	"backSpaceChar"},
  {"Del",	"deleteChar"},
  {"Down",	"downLine"},
  {"End",	"endOfLine"},
  {"F10",	"quit"},
  {"Esc0",	"quit"},
  {"F2",	"save"},
  {"Esc2",	"save"},
  {"Home",	"startOfLine"},
  {"Left",	"leftChar"},
  {"PgDn",	"downPage"},
  {"PgUp",	"upPage"},
  {"Enter",	"splitLine"},
  {"Return",	"splitLine"},
  {"Right",	"rightChar"},
{"Shift F2",	"switchMode"},	// MC doesn't have a command mode.
  {"Esc:",	"switchMode"},	// Sorta vi like, and coz tmux is screwing with the shift function keys somehow.
  {"Esc|",	"splitV"},	// MC doesn't have a split window concept, so make these up to match tmux more or less.
  {"Esc-",	"splitH"},
  {"Esco",	"switchBoxes"},
  {"Escx",	"deleteBox"},
  {"Up",	"upLine"},
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
  {"backSpaceChar",	"Back space last character.",		0, {backSpaceChar}},
  {"delete",		"Delete current character.",		0, {deleteChar}},
  {"down",		"Move cursor down one line.",		0, {downLine}},
  {"downPage",		"Move cursor down one page.",		0, {downPage}},
  {"end",		"Go to end of line.",			0, {endOfLine}},
  {"left",		"Move cursor left one character.",	0, {leftChar}},
  {"exit",		"Quit the application.",		0, {quit}},
  {"right",		"Move cursor right one character.",	0, {rightChar}},
  {"writeout",		"Save.",				0, {saveContent}},
  {"enter",		"Split line at cursor.",		0, {splitLine}},
  {"home",		"Go to start of line.",			0, {startOfLine}},
  {"up",		"Move cursor up one line.",		0, {upLine}},
  {"upPage",		"Move cursor up one page.",		0, {upPage}},
  {NULL, NULL, 0, {NULL}}
};


// Hmm, back space, page up, and page down don't seem to have bindable commands according to the web page, but they are bound to keys anyway.
struct keyCommand simpleNanoKeys[] =
{
// TODO - Delete key is ^H dammit.  Find the alternate Esc sequence for Del.
//  {"^H",	"backSpaceChar"},	// ?
  {"BS",	"backSpaceChar"},
  {"^D",	"delete"},
  {"Del",	"delete"},
  {"^N",	"down"},
  {"Down",	"down"},
  {"^E",	"end"},
  {"End",	"end"},
  {"^X",	"exit"},
  {"F2",	"quit"},
  {"^O",	"writeout"},
  {"F3",	"writeout"},
  {"^A",	"home"},
  {"Home",	"home"},
  {"^B",	"left"},
  {"Left",	"left"},
  {"^V",	"downPage"},	// ?
  {"PgDn",	"downPage"},
  {"^Y",	"upPage"},	// ?
  {"PgUp",	"upPage"},
  {"Enter",	"enter"},	// TODO - Not sure if this is correct.
  {"Return",	"enter"},	// TODO - Not sure if this is correct.
  {"^F",	"right"},
  {"Right",	"right"},
  {"^P",	"up"},
  {"Up",	"up"},
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
// Only vi is not so simple.  lol
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

void viMode(view *view)
{
  currentBox->view->mode = 0;
  commandMode = 0;
  viTempExMode = 0;
}

void viInsertMode(view *view)
{
  currentBox->view->mode = 1;
  commandMode = 0;
}

void viExMode(view *view)
{
  currentBox->view->mode = 2;
  commandMode = 1;
  // TODO - Should change this based on the event, : or Q.
  viTempExMode = 1;
  commandLine->prompt = xrealloc(commandLine->prompt, 2);
  strcpy(commandLine->prompt, ":");
}

void viBackSpaceChar(view *view)
{
  if ((2 == currentBox->view->mode) && (0 == view->cX) && viTempExMode)
    viMode(view);
  else
    backSpaceChar(view);
}

void viStartOfNextLine(view *view)
{
  startOfLine(view);
  downLine(view);
}

struct function simpleViCommands[] =
{
  // These are actual ex commands.
  {"insert",		"Switch to insert mode.",		0, {viInsertMode}},
  {"quit",		"Quit the application.",		0, {quit}},
  {"visual",		"Switch to visual mode.",		0, {viMode}},
  {"write",		"Save.",				0, {saveContent}},

  // These are not ex commands.
  {"backSpaceChar",	"Back space last character.",		0, {viBackSpaceChar}},
  {"deleteBox",		"Delete a box.",			0, {deleteBox}},
  {"deleteChar",	"Delete current character.",		0, {deleteChar}},
  {"downLine",		"Move cursor down one line.",		0, {downLine}},
  {"downPage",		"Move cursor down one page.",		0, {downPage}},
  {"endOfLine",		"Go to end of line.",			0, {endOfLine}},
  {"executeLine",	"Execute a line as a script.",		0, {executeLine}},
  {"exMode",		"Switch to ex mode.",			0, {viExMode}},
  {"leftChar",		"Move cursor left one character.",	0, {leftChar}},
  {"rightChar",		"Move cursor right one character.",	0, {rightChar}},
  {"splitH",		"Split box in half horizontally.",	0, {halveBoxHorizontally}},
  {"splitLine",		"Split line at cursor.",		0, {splitLine}},
  {"splitV",		"Split box in half vertically.",	0, {halveBoxVertically}},
  {"startOfLine",	"Go to start of line.",			0, {startOfLine}},
  {"startOfNLine",	"Go to start of next line.",		0, {viStartOfNextLine}},
  {"switchBoxes",	"Switch to another box.",		0, {switchBoxes}},
  {"upLine",		"Move cursor up one line.",		0, {upLine}},
  {"upPage",		"Move cursor up one page.",		0, {upPage}},
  {NULL, NULL, 0, {NULL}}
};

struct keyCommand simpleViNormalKeys[] =
{
  {"BS",	"leftChar"},
  {"X",		"backSpaceChar"},
  {"Del",	"deleteChar"},
  {"x",		"deleteChar"},
  {"Down",	"downLine"},
  {"j",		"downLine"},
  {"End",	"endOfLine"},
  {"Home",	"startOfLine"},
  {"Left",	"leftChar"},
  {"h",		"leftChar"},
  {"PgDn",	"downPage"},
  {"^F",	"downPage"},
  {"PgUp",	"upPage"},
  {"^B",	"upPage"},
  {"Enter",	"startOfNextLine"},
  {"Return",	"startOfNextLine"},
  {"Right",	"rightChar"},
  {"l",		"rightChar"},
  {"i",		"insert"},
  {":",		"exMode"},	// This is the temporary ex mode that you can backspace out of.  Or any command backs you out.
  {"Q",		"exMode"},	// This is the ex mode you need to do the "visual" command to get out of.
  {"^Wv",	"splitV"},
  {"^W^V",	"splitV"},
  {"^Ws",	"splitH"},
  {"^WS",	"splitH"},
  {"^W^S",	"splitH"},
  {"^Ww",	"switchBoxes"},
  {"^W^W",	"switchBoxes"},
  {"^Wq",	"deleteBox"},
  {"^W^Q",	"deleteBox"},
  {"Up",	"upLine"},
  {"k",		"upLine"},
  {NULL, NULL}
};

struct keyCommand simpleViInsertKeys[] =
{
  {"BS",	"backSpaceChar"},
  {"Del",	"deleteChar"},
  {"Return",	"splitLine"},
  {"Esc",	"visual"},
  {"^C",	"visual"},
  {NULL, NULL}
};

struct keyCommand simpleExKeys[] =
{
  {"BS",	"backSpaceChar"},
  {"Del",	"deleteChar"},
  {"Down",	"downLine"},
  {"End",	"endOfLine"},
  {"Home",	"startOfLine"},
  {"Left",	"leftChar"},
  {"Enter",	"executeLine"},
  {"Return",	"executeLine"},
  {"Right",	"rightChar"},
  {"Esc",	"visual"},
  {"Up",	"upLine"},
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
  struct context *context = &simpleMcedit;  // The default is mcedit, coz that's what I use.
  struct termios termio, oldtermio;
  char *prompt = "Enter a command : ";
  unsigned W = 80, H = 24;

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

  // TODO - Should do an isatty() here, though not sure about the usefullness of driving this from a script or redirected input, since it's supposed to be a UI for terminals.
  //          It would STILL need the terminal size for output though.  Perhaps just bitch and abort if it's not a tty?
  //          On the other hand, sed don't need no stinkin' UI.  And things like more or less should be usable on the end of a pipe.

  // Grab the old terminal settings and save it.
  tcgetattr(0, &oldtermio);
  tcflush(0, TCIFLUSH);
  termio = oldtermio;

  // Mould the terminal to our will.
  /*
    IUCLC	(not in POSIX) Map uppercase characters to lowercase on input.
    IXON	Enable XON/XOFF flow control on output.
    IXOFF	Enable XON/XOFF flow control on input.
    IXANY	(not in POSIX.1; XSI) Enable any character to restart output.

    ECHO	Echo input characters.
    ECHOE	If ICANON is also set, the ERASE character erases the preceding input character, and WERASE erases the preceding word.
    ECHOK	If ICANON is also set, the KILL character erases the current line. 
    ECHONL	If ICANON is also set, echo the NL character even if ECHO is not set. 
    TOSTOP	Send the SIGTTOU signal to the process group of a background process which tries to write to its controlling terminal.
    ICANON	Enable canonical mode. This enables the special characters EOF, EOL, EOL2, ERASE, KILL, LNEXT, REPRINT, STATUS, and WERASE, and buffers by lines.

    VTIME	Timeout in deciseconds for non-canonical read.
    VMIN	Minimum number of characters for non-canonical read.

    raw mode  turning off ICANON, IEXTEN, and ISIG kills most special key processing.
       termio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
       termio.c_oflag &= ~OPOST;
       termio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
       termio.c_cflag &= ~(CSIZE | PARENB);
       termio.c_cflag |= CS8;

    IGNBRK	ignore BREAK
    BRKINT	complicated, bet in this context, sends BREAK as '\x00'
    PARMRK	characters with parity or frame erors are sent as '\x00'
    ISTRIP	strip 8th byte
    INLCR	translate LF to CR in input
    IGLCR	ignore CR
    OPOST	enable implementation defined output processing
    ISIG	generate signals on INTR (SIGINT on ^C), QUIT (SIGQUIT on ^\\), SUSP (SIGTSTP on ^Z), DSUSP (SIGTSTP on ^Y)
    IEXTEN	enable implementation defined input processing, turns on some key -> signal -tuff
    CSIZE	mask for character sizes, so in this case, we mask them all out
    PARENB	enable parity
    CS8		8 bit characters

    VEOF	"sends" EOF on ^D, ICANON turns that on.
    VSTART	restart output on ^Q, IXON turns that on.
    VSTATUS	display status info and sends SIGINFO (STATUS) on ^T.  Not in POSIX, not supported in Linux.  ICANON turns that on.
    VSTOP	stop output on ^S, IXON turns that on.
  */
  termio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IUCLC | IXON | IXOFF | IXANY);
  termio.c_oflag &= ~OPOST;
  termio.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | TOSTOP | ICANON | ISIG | IEXTEN);
  termio.c_cflag &= ~(CSIZE | PARENB);
  termio.c_cflag |= CS8;
  termio.c_cc[VTIME]=0;  // deciseconds.
  termio.c_cc[VMIN]=1;
  tcsetattr(0, TCSANOW, &termio);

  terminal_size(&W, &H);
  if (toys.optflags & FLAG_w)
    W = TT.w;
  if (toys.optflags & FLAG_h)
    H = TT.h;

  // Create the main box.  Right now the system needs one for wrapping around while switching.  The H - 1 bit is to leave room for our example command line.
  rootBox = addBox("root", context, toys.optargs[0], 0, 0, W, H - 1);
  currentBox = rootBox;

  // Create the command line view, sharing the same context as the root.  It will differentiate based on the view mode of the current box.
  // Also load the command line history as it's file.
  // TODO - different contexts will have different history files, though what to do about ones with no history, and ones with different histories for different modes?
  commandLine = addView("command", rootBox->view->content->context, ".boxes.history", 0, H, W, 1);
  // Add a prompt to it.
  commandLine->prompt = xrealloc(commandLine->prompt, strlen(prompt) + 1);
  strcpy(commandLine->prompt, prompt);
  // Move to the end of the history.
  moveCursorAbsolute(commandLine, 0, commandLine->content->lines.length, 0, 0);

  // All the mouse tracking methods suck one way or another.  sigh
  // http://rtfm.etla.org/xterm/ctlseq.html documents xterm stuff, near the bottom is the mouse stuff.
  // http://leonerds-code.blogspot.co.uk/2012/04/wide-mouse-support-in-libvterm.html is helpful.
  // Enable mouse (VT200 normal tracking mode, UTF8 encoding).  The limit is 2015.  Seems to only be in later xterms.
//  fputs("\x1B[?1005h", stdout);
  // Enable mouse (VT340 locator reporting mode).  In theory has no limit.  Wont actually work though.
  // On the other hand, only allows for four buttons, so only half a mouse wheel.
  // Responds with "\1B[e;p;r;c;p&w" where e is event type, p is a bitmap of buttons pressed, r and c are the mouse coords in decimal, and p is the "page number".
//  fputs("\x1B[1;2'z\x1B[1;3'{", stdout);
  // Enable mouse (VT200 normal tracking mode).  Has a limit of 256 - 32 rows and columns.  An xterm exclusive I think, but works in roxterm at least.  No wheel reports.
  // Responds with "\x1B[Mbxy" where x and y are the mouse coords, and b is bit encoded buttons and modifiers - 0=MB1 pressed, 1=MB2 pressed, 2=MB3 pressed, 3=release, 4=Shift, 8=Meta, 16=Control
  fputs("\x1B[?1000h", stdout);
  fflush(stdout);

  calcBoxes(currentBox);
  drawBoxes(currentBox);
  // Do the first cursor update.
  updateLine(currentBox->view);

  // Run the main loop.
  handle_keys((long) currentBox->view, handleKeySequence, handleCSI);

  // TODO - Should remember to turn off mouse reporting when we leave.

  // Restore the old terminal settings.
  tcsetattr(0, TCSANOW, &oldtermio);

  puts("");
  fflush(stdout);
}
