# AMusicBrainz

AMusicBrainz v1.0 queries MusicBrainz from an Amiga Workbench window.

## Network Compatibility

The program uses only the public `bsdsocket.library` interface:

- `OpenLibrary("bsdsocket.library")`
- `gethostbyname`
- `socket`
- `IoctlSocket(FIONBIO)`
- `connect`
- `WaitSelect`
- `getsockopt(SO_ERROR)`
- `send`
- `recv` with flags `0`
- `CloseSocket`

It does not call any TheWire13-internal APIs.

## Usage

1. Enter a search term.
2. Select `Artist`, `Releases`, or `Track`.
3. Press `Search`. Multi-word searches are sent as AND-linked MusicBrainz queries.

The result area resizes with the window. The main window position and size are saved to `AMusicBrainz.conf` in the startup directory and restored on the next launch. In artist mode, clicking an artist loads that artist's album list sorted by release year. Clicking an album opens the track list in album order. Use the Up/Down buttons to scroll longer lists. The `? -> Info` menu opens the version/about dialog. Incoming UTF-8 and simple JSON Unicode escapes are converted to single-byte display characters for umlauts and common Latin-1 names.

## Limitations

- Uses plain HTTP, not HTTPS.
- JSON parsing is lightweight and extracts common fields only.
- Result detail views are not implemented yet.
- MusicBrainz may reject excessive request rates.
