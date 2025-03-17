# job-control.es -- Job control for the extensible shell.
#
# NOTE: THIS IS AN EXPERIMENTAL SCRIPT!  CAVEAT USER!
#
# This script combines existing mechanisms in es to create a form of job
# control.  It supports ^Z-ing running commands, fg'ing or bg'ing stopped
# commands, and listing stopped jobs (and processes) with the apids function.
#
# There are a few standard job control "sugar" features which are not
# implemented here.  Es does not give jobs cute names, for example, instead only
# using process group IDs (as negative numbers, to indicate that they are pgids,
# like what's used by the waitpid(3) and kill(3) functions); in addition, es
# does not perform the "You have stopped jobs." check on exit.  Both of these
# things can be added without too much difficulty by the motivated user.
#
# Slightly harder to add would be printing job commands when a job is managed.
# This could be added without too much trouble, but making it "pretty" may
# require something like https://github.com/wryun/es-shell/pull/65.  I haven't
# explored this closely.
#
# Ideally none of this should create new buggy/crashing cases for the shell.
# Unfortunately it's relatively easy to shoot yourself in the foot with job
# control, so that's somewhat difficult to guarantee.

# The following hooks %interactive-loop to block the job control signals and
# put the shell into its own process group.  This is the standard start-up
# procedure for a job control shell.  If successful, then it defines the
# %make-job function.
#
# The subtle implication here is that a script invoked in a new shell process
# (like `./script.es`) will not be using job control, but a script sourced
# (like `. script.es`) from an %interactive-loop will.  This matches bash, zsh,
# and tcsh, but differs from ksh.  What do you know!

let (loop = $fn-%interactive-loop)
fn %interactive-loop {
	# Compat: gracefully handle es binaries which can't do job control
	catch @ {
		return <={$loop $*}
	} {
		signals = $signals +sigtstp +sigttin +sigttou
	}
	local (
		fn-%make-job = newpgrp
		noexport = $noexport fn-%make-job
	) {
		newpgrp
		$loop $*
	}
}

# newpgrp has been extended such that when called with no arguments, it puts
# the shell itself into a new process group (and takes the terminal) just as
# before, but if given arguments, it runs that command such that any forked
# processes are placed into a new process group of its own.  Once child
# processes are in a pgroup, the shell manages them as a single unit.  This is
# the core OS-level concept backing a "job" in a job control shell.
#
# Note that this setup will quickly lead to nested `newpgrp' calls when invoking
# an external binary in a pipe, or in the background, or whatever.  In these
# cases the internal `newpgrp' commands are no-ops; they simply run their
# arguments.  That is, the outer newpgrp "wins".
#
# These spoofs hook process group (that is, job) creation into invoking a
# binary, pipe, or backgrounded process.  Other process-creating primitives
# (like $&backquote and other I/O functions) are not hooked like this, as
# processes created in those contexts should be managed with the shell.

let (r = $fn-%run)
fn %run {
	$fn-%make-job $r $*
}

let (p = $fn-%pipe)
fn %pipe {
	$fn-%make-job $p $*
}

let (b = $fn-%background)
fn %background {
	$fn-%make-job $b $*
}


# The following hooks prevent pgroup creation in certain contexts.  es's
# behavior is just less surprising with these spoofs.

let (b = $fn-%backquote)
fn %backquote {
	local (fn-%make-job = ()) $b $*
}

let (r = $fn-%readfrom)
fn %readfrom f in cmd {
	$r $f {local (fn-%make-job = ()) $in} $cmd
}

let (w = $fn-%writeto)
fn %writeto f in cmd {
	$r $f {local (fn-%make-job = ()) $in} $cmd
}



# Since job control makes it generally more useful to know about child processes
# and pgroups, %apids and apids are convenience wrappers around the $&apids
# primitive.  %apids will return processes not wrapped in a pgroup as their pids,
# and any pgroups will be returned as a single -pgid.
#
# If %apids is given any pids or -pgids as arguments, it will search for any
# running pids associated with those arguments and return them.  This allows
# users to wrap non-pgroup-aware commands like
#
#   fn command pids {		# $pids might actually include some -pgids!
#       /usr/bin/command <={%apids $pids}	# now all -pgids are just pids
#   }
#
# In addition, apids can list all child pids as pids and not -pgids, with
#
#   apids <=%apids

fn %apids pids {
	if {~ $#pids 0} {
		$&apids
	} {
		let (r = ())
		for (pid = $pids)
			r = <={$&apids $pid}
	}
}

fn apids {
	echo <={%apids $*}
}

# This extension to %echo-status sets the apid variable and prints something
# appropriate for stopped processes.  $apid is used with job control to make
# `fg' and `bg' more convenient, not requiring a user to pick out the relevant
# -pgid for the stopped job every time.

fn %echo-status pid did status {
	if {~ $pid $apid && !~ $did stopped} {
		apid = ()
	}
	let (msg = <={if {$echo-status-pid} {result $pid^': '} {result ''}})
	if {~ $did stopped} {
		apid = $pid
		echo >[1=2] $msg^'stopped'
	} {~ $did signaled} {
		msg = $msg^<={$&sigmessage $status}
		if {~ $status *+core} {
			msg = $msg^'--core dumped'
		}
		echo >[1=2] $msg
	}
}

# fg and bg continue a stopped process group (job) in the foreground or
# background, respectively.  If called without an argument, they use the
# apid variable if available.  Both functions make use of flags on the
# $&wait primitive, which is what does the heavy lifting.
#
# Part of the job-control-capable es' duties is foregrounding, or "giving the
# terminal to", child pgroups.  It will do so automatically whenever wait()ing
# for a specific pgroup.  If waiting for a non-pgrouped child, or if waiting for
# any child to finish, it will not perform any foregrounding.
#
# $&wait -c sends a SIGCONT to the given pid (or -pgid) before waiting.  It may
# be tempting to simply send a kill -cont $pid before the wait, but that
# creates a race, since the $pid may not "have" the terminal before continuing,
# and may get stopped with a sigttin.  $&wait foregrounds the process before
# sending the signal.  The -c flag requires a specific pid or -pgid to be
# provided.
#
# $&wait -n performs a waitpid() with the WNOHANG option.  If no specified child
# is immediately ready to be reaped, then $&wait -n will simply return an empty
# list (and it will not call %echo-status).

fn fg pid {
	if {~ $pid ()} {
		pid = $apid
	}
	if {~ $pid ()} {
		throw error fg usage: fg pid
	} {
		$&wait -c $pid
	}
}

fn bg pid {
	if {~ $pid ()} {
		pid = $apid
	}
	if {~ $pid ()} {
		throw error bg usage: bg pid
	} {
		apid = $pid
		$&wait -nc $pid
	}
}

# check-wait is a function which performs a $&wait -n along with a more verbose
# %echo-status hook so that any stopped/terminated/signaled process notifies the
# user.  This is another convenience function since generally it is much easier
# to "lose" child processes when doing all the exciting stuff job control
# entails.

fn check-wait {
	catch @ e type msg {
		# this isn't REALLY required long-term but it's nicer than an
		# infinite error loop for shells that can't handle wait -n
		if {~ $e error && ~ $type '$&wait' && ~ $msg 'wait: 0: bad pid'} {
			echo >[1=2] this es can''''t do job control!
			fn-check-wait = {}
		}
	} {
		local (fn %echo-status pid did status {
			echo >[1=2] $pid $did^: $status
		}) $&wait -n
	}
}

# This hooks the check-wait function into %prompt, for behavior somewhat like
# other job-controlling shells.

let (p = $fn-%prompt)
fn %prompt {
	check-wait
	$p $*
}
