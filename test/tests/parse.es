# tests/parse.es -- test that $&parse works with the chaos of various reader commands

test 'parser' {
	let (ex = ()) {
		catch @ e {
			ex = $e
		} {
			$&parse {throw test-exception}
		}
		assert {~ $ex test-exception}
	}

	let ((e type msg) = ()) {
		catch @ exc {
			(e type msg) = $exc
		} {
			$&parse {result ')'}
		}
		assert {~ $e error && ~ $msg *'syntax error'*} \
			'parser handles syntax error'
	}

	let ((e type msg) = ()) {
		catch @ exc {
			(e type msg) = $exc
		} {
			$&parse {result '('}
		}
		assert {~ $e error && {~ $msg *'memory exhausted'* || ~ $msg *'stack overflow'*}} 'parser handles infinite recursion'
	}

	let ((e type msg) = ()) {
		catch @ exc {
			(e type msg) = $exc
		} {
			let (line = 'aaaa ( bbbbb')
			$&parse {let (l = $line) {line = (); result $l}}
		}
		assert {~ $e error && ~ $msg *'syntax error'*}
		catch @ exc {
			(e type msg) = $exc
		} {
			$&parse
		}
		assert {~ $e eof && ~ $type ()}
	}

	# test parsing from string while parsing from input
	let (e = ()) {
		catch @ exc {
			e = $exc
		} {
			$&parse {eval result true}
		}
		assert {~ $e ()}
	}

	# do normal cases last to see if previous ones broke anything
	assert {~ <={$&parse {result 'echo >[1=2]'}} '{%dup 1 2 {echo}}'}

	# force GCs during parsing
	let (lines = 'fn zoom {' 'this is one' 'let (z = a a a) {' 'this is three' '}' '}')
	assert {~ <={$&parse {
		let (l = ()) {
			(l lines) = $lines
			$&collect
			result $l
		}
	}} '{fn-^zoom=@ *{%seq {this is one} {let(z=a a a){this is three}}}}'}
}
