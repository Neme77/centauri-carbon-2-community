# .13 qualification driver sources

These are the exact workspace drivers used for the accepted 4.4.4 RC4. They
are retained with the release sources so the orchestration is reviewable, not
presented as an input-free one-command public build.

The recorded workspace is `work/cc2-13-port`, with this repository linked as
its `source` directory. Copy these driver files there. The adjacent stock
`.07`/`.13` extraction directories, qualified native receipts, Reactor output,
SSH stage and runtime archive are external inputs referenced by the scripts.
Obtain vendor inputs separately and reproduce native modules with the sources;
never replace a missing input or hash check with an unverified file. The
`connections13-source` directory preserves the exact shim sources used.

The qualification directory preserves the release-local regression drivers.
Their relative paths describe the recorded workspace/output layout; read them
before running in another layout. Private signing/AES keys and vendor programs
are not included. The stock13 RootFS and component hashes remain explicit in
the builder. The signed RC4's contents and signatures were checked by the
recorded build before its physical trial; renaming that accepted outer file
for release does not rebuild it.

Until the workspace orchestration is converted into the normal public builder
entry point, this directory documents that limitation explicitly. Do not claim
a clean generic build using only `Build-Release.cmd` has been qualified for .13.
