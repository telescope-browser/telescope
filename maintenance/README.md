# Maintaining telescope

## Making releases

To create a "chain of trust" for signify keys, we when doing the
release N we also generate the keys for N+1 and include the public key
in the repo, under the `keys/` directory.

Tags should be signed with a known ssh key.  We don't usually use GPG
nor sign every commit, as it makes little sense.

Before making a release it's better to double-check that everything is
in order.  The ChangeLog file should be up-to-date and used as a base
to create the release notes.  Release notes should be useful both for
the end users and for packagers: breaking changes, new or changed
dependencies, bug fixes and new features needs to be clearly stated.

It goes without sayng that before making a release all known bugs must
be fixed.

Every release so far was named after an Italian song.  It started by
trying to keep the "space" theme, but then I (op) started to just use
a song I was listening to many times in the release period.  There may
or may not be any meaning behind the chosen song.  The "limitation"
that songs are from italian groups is not meant to exclude the rest of
the world, it's just an arbitrary choice done by me (op).  Other
projects that I maintain (gmid) use song as a release name, so I just
wanted to differentiate a bit (and maybe advertise some Italian indie
music.)

Minor releases (e.g. 0.10.1) are reserved for bug fixes and inherit the
name of the previous release (e.g. 0.10).

When everything is in place, remove `-current` from `configure.ac` and
bump the version, then run

	$ make release PUBKEY=... PRIVKEY=...
	
signify(1) will then prompt for the password to unlock the private
key.  It will also try to check the signed tarball against the public
key, as a safety measure.

Note to op: on OpenBSD I'm using an `obj` directory for the build, and
`make release` needs to be run after cd'ing there.

Then upload the tarballs, make the tag (and sign it!), add `-current`
to the version in `configure.ac`, make the release on Codeberg and
GitHub, and remember to upload the generated tarballs there as well.

Finally, update and publish the website too.
