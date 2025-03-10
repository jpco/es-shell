# job-control.es -- Job control for the extensible shell.

# TODO: test test test with scripts! scripts should have "job-control = false"!

# TODO? add "there are stopped jobs." check to %interactive-loop

# TODO: inject these into %interactive-loop
signals = $signals +sigtstp +sigttin +sigttou
newpgrp

# These spoofs hook process group (that is, job) creation into invoking a
# binary, pipe, or backgrounded process.  Other process-creating primitives
# (like $&backquote and other I/O functions) are not hooked.

let (r = $fn-%run)
fn %run {
	if %is-interactive {
		newpgrp $r $*
	} {
		$r $*
	}
}

let (p = $fn-%pipe)
fn %pipe {
	if %is-interactive {
		newpgrp $p $*
	} {
		$p $*
	}
}

let (b = $fn-%background)
fn %background {
	if %is-interactive {
		newpgrp $b $*
	} {
		$b $*
	}
}

# %apids and apids are convenience wrappers for the $&apids primitive, which is
# generally more useful with job control.  Where there are child pids in a child
# process group, the group itself is reported with -pgid.  %apids and apids can
# be passed one or more pids (and/or -pgids), in which case the child pids
# corresponding with those pids and/or -pgids are returned.

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

# This extension to %echo-status sets the stopped-job variable and prints
# something appropriate for stopped processes.

fn %echo-status pid did status {
	if {~ $pid $stopped-job && !~ $did stopped} {
		stopped-job = ()
	}
	let (msg = <={if {$echo-status-pid} {result $pid^': '} {result ''}})
	if {~ $did stopped} {
		stopped-job = $pid
		echo >[1=2] $msg^'stopped'
	} {~ $did signaled} {
		msg = $msg^<={$&sigmessage $status}
		if {~ $status *+core} {
			msg = $msg^'--core dumped'
		}
		echo >[1=2] $msg
	}
}

noexport = $noexport stopped-job

# fg and bg continue a stopped process group (job) in the foreground or
# background, respectively.  If called without an argument, it uses the
# stopped-job variable if available.  Both functions make use of flags on the
# $&wait primitive, which does the heavy lifting.

fn fg pid {
	if {~ $pid ()} {
		pid = $stopped-job
	}
	if {~ $pid ()} {
		throw error fg usage: fg pid
	} {
		$&wait -c $pid
	}
}

fn bg pid {
	if {~ $pid ()} {
		pid = $stopped-job
	}
	if {~ $pid ()} {
		throw error bg usage: bg pid
	} {
		$&wait -nc $pid
	}
}

# check-wait is a function which performs a NOHANG wait along with a more
# verbose %echo-status hook so that any stopped/terminated/signaled process
# notifies the user.

fn check-wait {
	local (fn %echo-status pid did status {
		echo >[1=2] $pid $did^: $status
	}) $&wait -n
}

# This hooks the check-wait function into %prompt, for behavior somewhat like
# other shells.

let (p = $fn-%prompt)
fn %prompt {
	check-wait
	$p $*
}
