# readline.es -- definitions for readline integration

#	The $&readline primitive is the entry point to the readline library.
#	Here we define the %read-line function, which is called by %parse, to
#	the primitive rather than simply prompting and reading in pure es
#	script.

fn-%read-line	= $&readline


#	TERM and TERMCAP are pretty cool.

set-TERM	= @ { $&resetterminal; result $* }
set-TERMCAP	= @ { $&resetterminal; result $* }


#	Readline provides history primitives for stuff.  It writes to the
#	$history file if it's set and writable, and to the in-memory history log
#	if max-history-length allows.

fn-%write-history = $&writehistory
set-history	= $&sethistory


#	The primitive $&setmaxhistorylength is another readline-only primitive
#	which limits the length of the in-memory history list, to reduce memory
#	size implications of a large history file.  Setting max-history-length
#	to 0 clears the history list and disables adding anything more to it.
#	Unsetting max-history-length allows the history list to grow unbounded.

set-max-history-length	= $&setmaxhistorylength
max-history-length	= 5000


#
#	Compilation stuff goes here, I guess.
#

#	Should this be a call to make?
$CC -o readline.o readline.c $headers	# TODO: working dir problems?

# TODO: how do we populate READLINE_LIBS and READLINE_CFLAGS initially?
LIBS	= $READLINE_LIBS $LIBS
CFLAGS	= $READLINE_CFLAGS $CFLAGS
OFILES	= $OFILES readline.o		# TODO: working dir problems?
