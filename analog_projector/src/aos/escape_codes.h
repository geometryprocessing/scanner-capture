#ifndef ESCAPE_CODES_H
#define ESCAPE_CODES_H

#define ESC     '\x1b'
#define RESET   "\x1bc"

#define CTRL_C     '\x03'
#define BACKSPACE  '\x08' // '\b'
#define TAB        '\x09' // '\t'
#define LF         '\x0a' // '\n'
#define CR         '\x0d' // '\r'
#define DEL        '\x7f'

#define BLACK_    "\x1b[30m"
#define RED_      "\x1b[31m"
#define GREEN_    "\x1b[32m"
#define YELLOW_   "\x1b[33m"
#define BLUE_     "\x1b[34m"
#define MAGENTA_  "\x1b[35m"
#define CYAN_     "\x1b[36m"
#define WHITE_    "\x1b[37m"
#define _RESET    "\x1b[0m"
#define _RESETn    "\x1b[0m\n"

#define CLEAR_BELOW   "\x1b[J"
#define CLEAR_ABOVE   "\x1b[1J"
#define CLEAR_SCREEN  "\x1b[2J"
#define CLEAR_ALL     "\x1b[3J"

#define CLEAR_RIGHT   "\x1b[K"
#define CLEAR_LEFT    "\x1b[1K"
#define CLEAR_LINE    "\x1b[2K"

#define GO_HOME   "\x1b[H"
#define SAVE_CUR  "\x1b[s"
#define LOAD_CUR  "\x1b[u"
#define GET_CUR   "\x1b[6n"

#define CUR_UP     "\x1b[A"
#define CUR_DOWN   "\x1b[B"
#define CUR_LEFT   "\x1b[D"
#define CUR_RIGHT  "\x1b[C"

#define CUR_UP_(X)      "\x1b[" #X "A"
#define CUR_DOWN_(X)    "\x1b[" #X "B"
#define CUR_LEFT_(X)    "\x1b[" #X "D"
#define CUR_RIGHT_(X)   "\x1b[" #X "C"

#define CUR_COL_(X)     "\x1b[" #X "G" // not ANSI.SYS
#define CUR_POS_(R, C)  "\x1b[" #R ";" #C "H"

#define PREV_LINE  "\x1b[F" // not ANSI.SYS
#define NEXT_LINE  "\x1b[E" // not ANSI.SYS

#define PREV_LINE_(X)  "\x1b[" #X "F" // not ANSI.SYS
#define NEXT_LINE_(X)  "\x1b[" #X "E" // not ANSI.SYS

#define SCROLL_UP    "\x1b[S" // not ANSI.SYS
#define SCROLL_DOWN  "\x1b[T" // not ANSI.SYS

#define SCROLL_UP_(X)    "\x1b[" #X "S" // not ANSI.SYS
#define SCROLL_DOWN_(X)  "\x1b[" #X "T" // not ANSI.SYS

// modifiers flag
#define ALT_BIT 0x80

typedef enum {
    eNull,      // '\0'
    eHome,
    eEnd,
    eCtrl_C,    // '\x03'
    eLeft,
    eRight,
    eUp,
    eDown,
    eBackspace, // '\b'
    eTab,       // '\t'
    eEnter,     // '\n'
    eInsert,
    eDelete,
    eReturn,    // '\r'
    eF1,
    eF2,
    eF3,
    eF4,
    eF5,
    eF6,
    eF7,
    eF8,
    eF9,
    eF10,
    eF11,
    eF12,
    eNum_5,
    eEsc,       // '\x1b'
    ePageUp,
    ePageDown,
    eReserved0,
    eReserved1,
    eUnknown = 127

} tESC;

#endif /* ESCAPE_CODES_H */
