# cdpath.es -- Add rc-like $cdpath behavior to es.
#
# `cd'ing to an absolute directory (one which starts with a /, ./, or ../) is
# unchanged.  For any other directory, cd performs a path-search for the
# directory in the list given by the $cdpath variable.
#
# If multiple arguments are given, then path searching is not performed, as we
# don't know a priori which of the arguments are meant to be a searchable
# directory.  Perhaps a future improvement -- decomposing the cdpath behavior
# to allow more-extended definitions of cd to integrate $cdpath a bit more
# easily.

let (cd = $fn-cd)
fn cd dir {
	if {!~ $#dir 1 || ~ $dir (/* ./* ../*)} {
		return <={$cd $dir}
	}
	let (abs = <={%cdpathsearch $dir}) {
		if {!~ $abs ($dir ./$dir)} {
			echo >[1=2] $abs
		}
		$cd $abs
	}
}

# %cdpathsearch performs the cdpath-searching behavior for cd.  It is nearly
# identical to the default %pathsearch, except it searches for directories
# rather than executable files.

fn %cdpathsearch name { access -n $name -1e -d $cdpath }

# Completions for %cdpathsearch and the cd that uses it.  These are directly
# defined rather than messing with $completion-path -- that should be changed,
# in part because that would allow users to source cdpath.es and completion.es
# in whatever order they want.

fn %complete-%cdpathsearch _ word {
	let (dirs = ()) {
		for (p = $cdpath)
		for (w = $p/$word^*)
		if {access -d -- $w} {
			dirs = $dirs <={~~ $w $p/*}
		}
		result $dirs
	}
}

# Call `%complete-fn cd` for the side effect of loading it into the environment
# so we can stow it in `compl`.  Obviously pretty bogus.  Also, should we define
# %completion-to-function in %complete-%cdpathsearch and make use of that in
# %complete-cd?

local (fn-%completion-to-file = ())
	{} <={%complete-fn cd}

let (compl = $fn-%complete-cd)
fn %complete-cd p word {
	let (result = <={%complete-%cdpathsearch $p $word}) {
		# Do this in an assignment rather than in the `let` to make sure
		# %completion-to-file is set by $compl
		result = $result <={$compl $p $word}

		let (ctf = $fn-%completion-to-file)
		fn %completion-to-file f {
			let (r = <={if {!~ $#ctf 0} {$ctf $f} {result}})
			if {!~ $#r 0 && access -d $r} {
				result $r
			} {
				catch @ {result $f} {
					%cdpathsearch $f
				}
			}
		}
		result $result
	}
}

# cdpath and CDPATH contain the list of directories to search for cd.  They
# follow the convention that the uppercase CDPATH contains a single colon-
# separated word which is understandable to other CDPATH-using shells, while
# the lowercase cdpath contains an es list.  For interoperability with other
# utilities, CDPATH is exported while cdpath is not.

# Somewhat awkwardly, we have to "re-noexport" cdpath in each shell which
# inherits CDPATH, because noexport is itself not exported, so es "forgets"
# about noexport state.  This is probably the correct behavior in general, but
# it's not very convenient in this case.

set-cdpath = @ {local (set-CDPATH = ) CDPATH = <={%flatten : $*}; result $*}
set-CDPATH = @ {
	local (set-cdpath = ) cdpath = <={%fsplit : $*}
	if {!~ $noexport cdpath} {noexport = $noexport cdpath}
	result $*
}

noexport = $noexport cdpath

# The default value of cdpath is '.': the current directory.  If cdpath is the
# empty list, then path searching behavior will not work, and it will only be
# possible to cd to paths starting with one of (/ ./ ../).

cdpath = .
