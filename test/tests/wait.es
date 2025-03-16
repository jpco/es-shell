# tests/wait.es -- verify behaviors around backgrounding and wait are correct

test 'exit status' {
	let (pid = <={$&background {result 3}}) {
		let (status = <={wait $pid >[2] /dev/null})
			assert {~ $status 3}
	}

	let (pid = <={$&background {./testrun s}}) {
		kill -TERM $pid
		let (status = <={wait $pid >[2] /dev/null})
			assert {~ $status sigterm}
	}

# 	let (pid = <={$&background {./testrun s}}) {
# 		kill -QUIT $pid
# 		# TODO: clean up core file?
# 		let (status = <={wait $pid >[2] /dev/null})
# 			assert {~ $status sigquit+core}
# 	}

	let (status = <={result 1 | result 2 | result 3})
		assert {~ $#status 3 && ~ $^status '1 2 3'}
}

test 'flag handling' {
	catch @ e type msg {
		assert {~ $e error} 'wait -c with no pid throws an error'
	} {
		wait -c
		assert false 'wait -c with no pid throws an error'
	}

	catch @ e type msg {
		assert {~ $e error} 'wait -c with no pid throws an error'
	} {
		wait -cn
		assert false 'wait -cn with no pid throws an error'
	}

	catch @ e type msg {
		assert false 'wait -n with no pid does not throw'
	} {
		let (r = <={wait -n})
		assert {~ $r ()} 'wait -n with no pid returns an empty list'
	}
}

test 'wait is precise' {
	let (pid = <={$&background {result 99}}) {
		assert {~ <=%apids $pid}
		assert {~ <=%apids $pid} 'apids is stable'
		fork {}
		assert {~ <=%apids $pid} 'waiting is precise'
		assert {~ <={wait $pid} 99} 'exit status is available'
	}
}

test 'setpgid' {
	let (pid = <={$&background {./testrun s}}) {
		assert {ps -o pid | grep $pid > /dev/null} 'background process appears in ps'
		kill $pid
		wait $pid >[2] /dev/null
		assert {!{ps -o pid | grep $pid}}
	}
}

# TODO: job control related tests
#  - make sure `{} and <{} and >{} run in local pgrp
#  - make sure newpgrp {a; b} doesn't break
