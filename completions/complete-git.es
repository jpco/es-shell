# Obviously we're not doing all of git... but this might be a start?

# Completion helpers
let (
	fn git-repo-path {
		result `{git rev-parse --git-dir >[2] /dev/null}
	}
	fn git-remotes repo {
		let (rs = ()) {
			for (d = $repo^/refs/remotes/*)
			let (ds = <={%fsplit / $d})
			rs = $rs $ds($#ds)
			result $rs
		}
	}
	fn git-complete-refs type word {
		result `` \n {git for-each-ref --format='%(refname:strip=2)' 'refs/'^$type^'/'^$word^'*'}
	}
)
fn %complete-git prefix word {
	# Subcommand completions.
	# TODO: I'm not so satisfied with the "dynamic subcommand detection"
	#       we've got here.
	local (
		fn %complete-git-checkout cmd {
			git-complete-refs heads $cmd($#cmd)
		}
		fn %complete-git-fetch cmd {
			result $word^<={~~ <={git-remotes <=git-repo-path} $word^*}
		}
		fn %complete-git-pull cmd {
			result $word^<={~~ <={git-remotes <=git-repo-path} $word^*}
		}
		fn %complete-git-push cmd {
			result $word^<={~~ <={git-remotes <=git-repo-path} $word^*}
		}
	)
	let (cmd = <={%split ' '\t $prefix}) {
		if {~ $#cmd 0} {
			result $word^<={~~ <={~~ <=$&vars fn-%complete-git-^*} $word^*}
		} {!~ $#(fn-%complete-git-$cmd(1)) 0} {
			%complete-git-$cmd(1) $cmd(2 ...) $word
		} {
			# lame fallback.
			%file-complete {} $word
		}
	}
}
