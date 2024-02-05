#!/usr/local/bin/es

# test.es -- Entry point for es tests.

# Invoke like:
# ; /path/to/es -s < test.es (--junit) (tests/test1.es tests/test2.es ...)
#
# --junit makes test.es report test results in junit xml compatible with circleci.

# Test files require:
#  - fn-test = @ name block { ... }
#     - which defines fn-assert = @ command name { ... }
#  - the variable $es which specifies where the es binary under test is

let (
	name = ()
	cases = ()
	passed-cases = ()
	failed-cases = ()
	failure-msgs = ()
	test-execution-failure = ()
) {
	fn new-test title {
		name = $title
		cases = ()
		passed-cases = ()
		failed-cases = ()
		failure-msgs = ()
		test-execution-failure = ()
	}

	fn fail-case test-name cmd msg {
		cases = $cases $^cmd
		failed-cases = $failed-cases $^cmd
		failure-msgs = $failure-msgs $^msg
	}

	fn pass-case test-name cmd {
		cases = $cases $^cmd
		passed-cases = $passed-cases $^cmd
	}

	let (fn-xml-escape = @ {
		let (result = ()) {
			for (string = $*) {
				string = <={%flatten '&amp;' <={%fsplit '&' $string}}
				string = <={%flatten '&quot;' <={%fsplit " $string}}
				string = <={%flatten '&apos;' <={%fsplit '''' $string}}
				string = <={%flatten '&lt;' <={%fsplit '<' $string}}
				result = $result <={%flatten '&gt;' <={%fsplit '>' $string}}
			}
			result $result
		}
	})
	fn report {
		if $junit {
			echo <={%flatten '' \
				'    <testsuite errors="0" failures="' $#failed-cases \
					'" name="' <={xml-escape $name} \
					'" tests="' $#cases \
					'">'}

			for (case = $cases) {
				echo -n <={%flatten '' '        <testcase name="' <={xml-escape $case} '"'}
				if {~ $case $failed-cases} {
					echo '>'
					for (fcase = $failed-cases; msg = $failure-msgs)
					if {~ $case $fcase} {
						echo <={%flatten '' '            <failure message="' <={xml-escape $msg} \
							'" type="WARNING">'}
						echo <={xml-escape $msg}
						echo '            </failure>'
						echo '        </testcase>'
					}
				} {
					echo '/>'
				}
			}

			echo '    </testsuite>'
		} {
			if {~ $failed-cases ()} {
				echo -n $^name^': '
			} {
				echo $name
				for (case = $failed-cases; msg = $failure-msgs)
					echo - $case failed $msg
			}
			if {!~ $test-execution-failure ()} {
				echo test execution failure: $test-execution-failure
			} {~ $#failed-cases 0} {
				echo passed!
			} {
				echo - $#passed-cases cases passed, $#failed-cases failed.
			}
		}
	}
}

fn test title testbody {
	# The main `assert` function.
	local (
		fn assert cmd message {
			let (result = ()) {
				catch @ e {
					fail-case $title $cmd $e
					return
				} {
					result = <={$cmd}
				}
				if {!result $result} {
					if {!~ $message ()} {
						fail-case $title $^message
					} {
						fail-case $title $cmd
					}
				} {
					pass-case $title $cmd
				}
			}
		}
	) {
		new-test $title
		catch @ e {
			test-execution-failure = $e
		} {
			$testbody
		}
		report
	}
}

es = $0
junit = false

if {~ $1 --junit} {
	junit = true
	* = $*(2 ...)
}

if $junit {
	echo '<?xml version="1.0"?>'
	echo '<testsuites>'
}

for (testfile = $*)
	. $testfile

if $junit {
	echo '</testsuites>'
}
