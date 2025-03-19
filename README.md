This is jpco's fork of the extensible shell.  Very little interesting is in the
`main` branch; it just exists to (imperfectly) track upstream.  Everything is
in other feature branches which target the upstream master branch.

I am primarily interested in developing on the "upsteam" _es_, rather than
forking off my own version of _es_; I believe there has been good work happening
on _es_, but because it's mostly all in individual forks, that good work has
been fragmented and there hasn't been much real community momentum for years.
This is a shame!

Here's a big list of the further development on _es_ I'm interested in, in
roughly descending priority:

-   Job control.  Sorry to the job-control haters, but I have been surprised at
    how many people still use the busted, old job-control branch in the repo,
    and to me that indicates that people still really care about the feature.
    I think those people deserve "first-class" support in the shell.  I also
    think my design (see the newjobcontrol2 branch) isn't too disruptive to
    those who don't want job control.

-   Input extensibility.  This is the extensibility that every other shell has
    completely lapped _es_ on at this point -- and for some shells, like zsh,
    interactive features are practically the entire selling point of the shell.
    The challenge is making the memory model work with the parser, but that's
    "only" a matter of implementation; once it's done, we can rewrite `%parse`
    to take a "reader command" which reads from the shell input and returns
    lines of code with the same API as `%read`.  To illustrate, I picture
    something like this in `%interactive-loop` (using a novel `%readline`
    function and primitive which we'd need to design extensibility around):

```
...
if {!~ $#fn-%prompt 0} {
    %prompt
}
let (input = (); pr = $prompt(1))
let (code = <={%parse {
    let (in = <={%readline $pr}) {
        input = $input $in
        pr = $prompt(2)
        result $in
    }
}}) {
    $fn-%write-history <={%flatten \n $input}
    if {!~ $#code 0} {
        ...
```

-   Runtime extensibility.  See the es-main branch.  I would probably want to
    merge this into upstream piecemeal; there are a lot of ideas in that change
    and I'd want to take some real time to carefully design each one.  For
    example, I haven't verified that `%run-file` as-is can do `ignoreeof`-like
    behavior, and I'd also really like to merge `$&runfile` and `$&runstring` if
    possible.  (Maybe that would have to be co-designed with the "reader
    commands" up above.)

-   Better error handling.  Since _es_ has exceptions, we could (should?) lean
    on them harder.  Ideally the error handling could be good enough that
    `es -e` is even a useful default to use in the interactive case.  See the
    throwonfalse branch.  (This also intersects with the es-main branch, since
    throwonfalse can be a runflag.)

-   Small syntax/ergonomic changes.  These are probably the least-worthwile
    changes I want, but for example: `catch` could specify an exception that
    will be caught instead of always catching everything; `$$x(2)` could have
    different precedence than `$($x(2))`; `cmd < <{foo}` could work;
    `$x(3 ... 1)` could return elements in reverse order rather than an empty
    list.

-   Fixing closure serialization to resolve the recursive-closure problem and
    the shared-closure problem.  Someday!

Some "further out" things I haven't messed with yet but which would be
interesting to explore:

-   General runtime improvements -- switching to a bytecode interpreter, getting
    tail call optimization, exploring alternative GC strategies, etc.  Using
    bytecode would also make it easier to port _es_ to other
    (non-exception-using) languages, such as Zig, but that isn't something I'm
    immediately interested in.

-   "Pluggable" primitives.  This would play well with input extensibility,
    allowing alternative line-editing libraries to be used with relatively
    little fuss.  It would also enable "per-OS" behavior to be added, such as
    the GUI scripting in Haiku, some capsicum-based mechanism to limit
    binary invocation in the BSDs, etc.

-   Building on the above, it could be possible to swap out the fork/exec
    concurrency model in the shell with a pthreads/spawn-based model, which
    could allow _es_ to run concurrently internally.  This would require a lot
    of refactoring in the shell runtime to allow for multi-threaded code,
    though.

-   Also building on the pluggable-primitives idea, further development could be
    done on the "librarified _es_" concept.  Ideally, "core" _es_ would only
    require ANSI C, and anything POSIX-y would go in the "library" layer and
    could be swapped out for something Plan 9-y, or Windows-y, or Plan 9-y, etc.

-   Extensible syntax.  I have half an idea of how this could be configured in
    the shell, and 5% of an idea how this could be implemented in the parser.
    To be honest, I'm a little skeptical of how possible a fully-general
    solution here is.
