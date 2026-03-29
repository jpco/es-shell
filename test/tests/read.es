#!/usr/local/bin/es

test 'null reading' {
	let (tmp = `{mktemp test-nul.XXXXXX})
	unwind-protect {
		echo first line	 > $tmp
		./testrun 0	>> $tmp

		let (first = (); second = ()) {
			{
				first = <=$&read
				second = <=$&read
			} < $tmp
			assert {~ $first 'first line'} '$&read reads valid line'
			assert {~ $second(1) 're'} '$&read reads line with zero (1)'
			assert {~ $second(2) 'sult 6'} '$&read reads line with zero (2)'
		}

		let (first = (); second = ()) {
			{
				first = <=%read
				second = <=%read
			} < $tmp
			assert {~ $first 'first line'} '%read reads valid line'
			assert {~ $second 'result 6'} '%read reads line with zero'
		}

		let ((first second) = `` \n {
			let (fl = ())
			cat $tmp | {echo <=$&read; echo <=$&read}
		}) {
			assert {~ $first 'first line'} 'pipe $&read reads valid line'
			assert {~ $second 're sult 6'} 'pipe $&read reads line with zero'
		}

		let ((first second) = `` \n {
			let (fl = ())
			cat $tmp | {echo <=%read; echo <=%read}
		}) {
			assert {~ $first 'first line'} 'pipe %read reads valid line'
			assert {~ $second 'result 6'} 'pipe %read reads line with zero'
		}

		# TODO: $&readline should not be tested in this file
		if {~ <=$&primitives readline} {
			let (first = (); second = ()) {
				{
					first = <=$&readline
					second = <=$&readline
				} < $tmp
				assert {~ $first 'first line'} 'read reads valid line'
				assert {~ $second 'result 6'} 'read reads line with zero'
			}
		}
	} {
		rm -f $tmp
	}
}
