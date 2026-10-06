# X3 reading preparation in 1.6.1-2.0

The EPUB file stays the source book. CrossInk creates disposable files in its cache on SD. Preparation runs on the reader with the current font and layout. The Wi-Fi File Manager uses its existing upload flow.

## Reading and wake

On X3, an ordinary EPUB open builds the requested page first. The reader then prepares twelve layout pages ahead, one page per background tick. A completed chapter keeps its exact count. An incomplete chapter shows an estimated count with `~`. Explicit position jumps, clipping searches and footnote builds keep the layout work that they require.

The reader also saves the current text page and four following text pages as completed pixels. Eight SD slots retain recently used pages. A prepared next chapter can supply pages to this window. Pages with images use the existing rendering path. Footnotes, clipping highlights, automatic turns and active feedback use the page model.

The pixel key includes the chapter, page, resolved layout and font signature, orientation, antialiasing and display settings. A normal entry starts a fresh set of pixel files. A wake preserves the set after checking the book path, size and modification time. Each later lookup checks its key.

Sleep saves the visible reader framebuffer before the sleep activity draws over it. Wake loads that frame after display setup and before font setup. A valid gray page supplies its saved gray planes. When that file is unavailable, wake can show the saved black and white frame and finish antialiasing through the regular reader path.

When the reading body and key match, wake keeps the saved footer until the next page turn. This avoids another panel update for a page-count estimate that changes as indexing resumes. Battery and time data are drawn again with the next page.

Antialiased text uses the four gray levels supported by the panel driver. The direct path uploads both complete planes before one activation. It uses the existing page and font raster data. Strong refreshes still run at the selected interval. Images and allocation failures use the existing fallback paths. Moving from direct gray into a black and white screen can require the SDK's extra synchronization refresh.

## Input and memory

Optional work waits for 250 ms without input. Page preparation checks cancellation between bands. Layout yields between pages. Cover preparation checks between thumbnail jobs. An active thumbnail decode finishes its current job before cancellation takes effect.

The page cache reuses the reader's 80-row scratch buffer. When two extra 80-row gray planes fit above the heap floor, it draws 80 rows per step and splits them into saved bands. Monochrome pages reuse one plane. Each saved band contains up to 24 physical rows. The three band planes occupy 7,128 bytes at X3 geometry. Early wake frees this buffer before loading fonts. Direct text composition can use another transient 80-row plane. Allocation can fail, and the reader then uses its existing path.

Extra gray planes use a 60,000-byte free-heap allocation reserve with a contiguous margin. Small-band preparation starts through the existing 64 KiB free-heap and 40 KiB block checks. Existing font and layout work can take free heap below these values. The firmware keeps one live full-screen framebuffer. Completed pixels reside on SD. The cache is bounded to eight files, with a maximum of about 1.2 MiB for uncompressed three-plane X3 pages.

Hold Left or Right for 600 ms in the Carousel theme to switch rows. Release after a short press to move one item. The selected book survives a visit to the menu row. Up and Down retain their row controls.

## SD formats

All integers use the target's little-endian 32-bit representation. Temporary files end in `.part`. A failed optional file returns to the source parsing or rendering path.

### Parser events: MID1, version 1

`<chapter>.html.mid` stores a 16-byte header: magic, version, extracted HTML size and modification time. Each record stores type, byte count and an FNV-1a hash over its payload seeded with the type. Record types are start element, end element, text, entity and chunk boundary. A chunk contains the original source offset and continuation or completion state.

Events hold at most 4,096 bytes with at most 64 attribute pairs. The parser reuses a 4 KiB event buffer, a 1 KiB I/O buffer and one attribute pointer array. Oversized events stop recording. Normal parsing continues. A whole chapter must finish before its event file can be committed. A suspended long chapter keeps its readable layout prefix and can rebuild from the extracted HTML later.

Replay validates the complete file before it calls the layout parser. The original callbacks build pages for the current font and viewport. This saves XML token parsing on later reflow. Text layout still runs, and a partial chapter extension can still replay from its start.

### Completed pixels: RPG1, version 1

`raster_<slot>.rpg` has a 40-byte header: magic, version, session marker, profile, spine, page, physical width, physical height, gray flag and band-row limit. Each band has five integers: rows, stored size of each of its three planes and a hash of the decoded planes.

The planes are the black and white base, gray LSB and gray MSB. A stored size with its high bit set selects raw bytes. Other data uses byte runs with a count from 1 to 255. A 256-byte batch handles writes and reads. Geometry, hashes, exact end of file and the page key are checked before a cached page reaches the panel. The live footer is composed when the page is used.

### Wake frame: RKW1, version 2

`/.crosspoint/reader_wake.bin` has a 168-byte header followed by the live black and white framebuffer. Its fields cover book identity, physical geometry, framebuffer size and hash, orientation, footer extent, the RPG key and a 96-byte optional path to the current gray page.

Wake validates the book stamp and framebuffer hash. A gray page is checked before uploading its planes. The body comparison masks the saved footer in the correct physical coordinates for each orientation. A changed body or key requires a normal reader paint.

## Release scope

Font profiles remain shelved. PNG work and the BMP filename investigation stay deferred. KOReader work remains outside this iteration. The background automation stays paused.

The native RV32 emulator checks firmware output and executed instructions. Its panel model counts refresh activations. It does not set a physical X3 latency in milliseconds.
