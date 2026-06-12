# AMusicBrainz

AMusicBrainz v1.0 is a small dynamically resizeable Amiga Workbench client for the MusicBrainz web service.

It uses the public `bsdsocket.library` API directly, so it is intended to work with TheWire13 as well as other AmiTCP/Roadshow-compatible stacks.

## Features

- Resizeable Workbench window
- Search input field and `Search` button
- Search modes: `Artist`, `Releases`, `Track`
- AND-linked multi-word searches
- Status warning when the result list reaches its capacity
- Normal search results are limited to 20 entries; artist album drill-down can show larger discographies
- Plain HTTP MusicBrainz requests on port 80
- Concise result list with artist/release/track names and dates where available
- Main window position/size is restored from `AMusicBrainz.conf`
- `? -> Info` dialog with version/about text
- Artist-result click loads the artist album list sorted by year
- Album click loads the album track list in track order
- Scroll buttons for long result lists
- Basic UTF-8/JSON escape decoding for umlauts and Latin-1 characters
- Classic Intuition only, no MUI/ReAction/GadTools requirement

## Build

```sh
make clean && make
```

Output:

```text
build/AMusicBrainz
```

## Runtime

Start a compatible TCP/IP stack and make sure `bsdsocket.library` is available, then run `AMusicBrainz`.

MusicBrainz rate-limits clients. Avoid repeated rapid searches.
