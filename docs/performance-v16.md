# X3 performance v16

V16 focuses on EPUB opening, page turns, lock and wake.

## Reading with antialiasing

Text pages can build each complete gray plane with one page traversal when the
heap has room. Under tighter memory limits, a pair of 80-row strips shares glyph
lookup and layout work. The allocation checks retain 60,000 bytes of free
internal heap and a 16 KiB contiguous margin. The old single-strip path remains
available when allocation fails. Pages with images keep their previous path.

The native daily fixture uses about 75% fewer `drawText` calls on its checked
pages. This metric measures repeated text preparation. A page turn also waits
for the panel and sends display data.

Small caps use the weighted area of source pixels when scaled to 75%.
Superscripts and subscripts average each 2 by 2 source area. The result uses the
four gray levels supported by the current X3 driver. Regular text retains its
pixels. With antialiasing disabled, the existing black and white footprint stays
equal.

## Indexing

A 32-slot cache reuses widths of repeated short words within one paragraph.
Text, style and focus boundary must match. Each position applies its own inline
padding. The cache uses 256 bytes on the ESP32-C3 stack and ends with the call.
The first-open native fixture performs 305 fewer width measurements, a drop of
11.6%.

The indexing popup previously refreshed the panel twice. V16 uses the refresh
already performed by the theme.

Library rescans locate a prior entry with a binary search inside the existing
cached block. The 16-book unchanged rescan uses 3,076 own instructions in
`PriorTable::find`, down from 3,936. It reuses all 16 books. Simulated unchanged
scan time stays at 255 ms. Cold scan time is also equal within 1 ms.

EPUB indexing still parses chapter content, resolves styles, lays out pages and
writes the cache to the SD card. Full Section finishes the chapter before its
first page appears. For books with large chapters, select **Settings > Reader >
Indexing Method > Incremental** to start reading earlier. V16 keeps the current
default and cache format, so this update preserves existing chapter caches.

## Sleep feedback

X3 shows its final dark, light or blank screen before the reader saves and
releases its state. Progress still finishes saving before deep sleep.

Custom images and covers retain the reader-first cleanup order on X3 because
image decoding needs that memory. The final bitmap has a `Sleeping` label near
the bottom. The cover and label share one display activation. X3 also skips the
separate entering-sleep popup, removing one refresh per lock.

Stats, quick resume and the retained-page overlay keep their current lifecycle.

## Checks

All 954 selected host checks pass. The 18 firmware PNG checks stay deferred.
The native daily cycle matches the baseline in all 14 captured states and
restores the same page after both wakes and a saved-book reopen.

Independent native checks cover Lexend Deca with antialiasing enabled and
disabled, Bitter with antialiasing, Library cold scan and Menu > Rescan, and
custom BMP sleep through two locks and wakes. Font differences stay inside the
scaled-letter regions. Sleep differences stay inside the label rectangle.
The checked native runs have zero panel protocol errors and zero guest faults.

The earlier Library scenario opened Sort. The corrected scenario opens Menu >
Rescan and preserves that evidence correction. Daily function rates remain
assumptions based on the user's reading cycle. The whole firmware body audit
remains open. Automated work stays paused; BMP investigation stays on hold.
