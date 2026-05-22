# Readline stuff

fn-%read-line = $&readline

if {~ <=$&primitives resetterminal} {
	set-TERM	= @ { $&resetterminal; result $* }
	set-TERMCAP	= @ { $&resetterminal; result $* }
}

# History file stuff

fn-%write-history = $&writehistory

set-max-history-length = $&setmaxhistorylength
max-history-length = 5000

if {~ <=$&primitives sethistory} {
	set-history = $&sethistory
}
