# CrossInk 1.6.1-2.0 on X3

This release focuses on opening an EPUB, reading pages and returning after sleep.

Wake shows the saved reading page before fonts and book metadata load. A valid completed gray page brings back its antialiasing in that first update. The footer stays as saved until the next page turn when an incomplete index changes its estimate.

An ordinary X3 open starts with the requested page. Background layout prepares twelve pages ahead. A separate cache keeps the current text page and four following pages as completed pixels. Eight SD slots roll across chapters that have a prepared layout. The first pass still prepares the book. Later cache hits reuse that work.

The EPUB remains the master. A completed chapter can save parser events that later font or layout changes reuse. A long partial chapter can still rebuild from its start. Preparation runs on X3. The Wi-Fi File Manager retains its upload flow.

Supported text antialiasing uploads a finished gray page before one panel activation. The existing strong-refresh interval stays active. The driver can add sync work when a later screen changes from direct gray to black and white. Image pages keep their existing path.

Optional work yields after input and resumes after 250 ms without input. Cover decoding finishes its current thumbnail job before yielding. The cache uses bounded bands and fallible allocations.

In Carousel, hold Left or Right for 600 ms to switch between books and the menu. Returning to books preserves the selection. Release a short press to move one item.

## Saved checks

The source passes 974 selected host checks. PNG work stays deferred. The exact release image passes 61 native captured states. Both daily sleep cycles restore their previous pages, as do the custom SD-font and AA-off cases. A four-tap burst reaches the expected page. Its next tap and wake preserve the correct position.

The default daily fixture uses 34 panel activations across its captured cycle, compared with 42 in v16. Each tested wake uses one activation, compared with five in v16. Native instruction counters and refresh counts cover the saved fixtures. Physical X3 panel and SD timing stays uncalibrated.

Source commit: 735e34c0fe027b517d39c9c10bc246b92bdff5c6.
Source tree: 4f143730d6410e7db5ae7029f9bb34e00773b00f.
App SHA-256: 67847a8dc2ce29578db7e2a8db74951323278779cf46588f1494c211f311e924.

The daily-use tables rank 42,319 source entries under the saved use assumptions. Full body checks match 759 entries. That work list still has pending reviews. The whole firmware audit stays open. Font profiles remain shelved; BMP filename work stays on hold. The automation remains paused.
