# Readline stuff

fn-%read-line = $&readline

if {~ <=$&primitives resetterminal} {
	set-TERM	= @ { $&resetterminal; result $* }
	set-TERMCAP	= @ { $&resetterminal; result $* }
}

# History file stuff

fn-%write-history = $&writehistory

# esdump gets mad at you if you set the settor function first... problem?
max-history-length = 5000
set-max-history-length = $&setmaxhistorylength

if {~ <=$&primitives sethistory} {
	set-history = $&sethistory
}
