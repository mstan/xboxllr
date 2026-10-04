# XDK symbol discovery

Borrowed from phobos665/xboxrecomp, commit
`f01b20ded38856abd3d37b561521cc7013363725`, `tools/xdk_symbols`.
Upstream license: GPL-3.0 (see `LICENSES/GPL-3.0.txt`).
The wrapper invokes the real XbSymbolDatabase CLI; it does not identify
functions by guessed addresses. Output belongs in ignored private build data.

Signatures are discovery evidence, not verified hook ABIs. The stock dashboard
DrawIndexedVertices signature marks VertexCount as EAX, while its machine code
reads all three arguments from the stack. Verify argument locations and RET
cleanup before emitting a replacement. No hooks are installed by this tool.
