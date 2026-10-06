# Upload folders through File Manager

Open File Manager and enter the destination folder. Press **Upload**, then **Choose folder**. Select the unzipped folder and press **Upload**.

The selected folder and its subfolders are created on the SD card. Files with the same name in separate subfolders keep their names. A name collision within one destination adds ` (2)`, ` (3)`, or the next free number before the extension. Transfers use the existing WebSocket path and HTTP fallback.

Folder selection requires browser support for `webkitdirectory`. The [MDN browser guide](https://developer.mozilla.org/en-US/docs/Web/API/HTMLInputElement/webkitdirectory) lists support. Desktop browsers can also accept dropped folders. Empty folders have no selected files and are omitted. Folder uploads skip `.DS_Store` and `._` files.

The folder picker keeps each relative path. HTTP uploads pass the basename as the multipart filename. A folder whose name contains `%` is decoded once from the page URL.

## BMP investigation

The image study ZIP has repeated basenames across its sample folders. Combining those samples in one destination can cause File Manager to add suffixes during the first upload. Each suffix can refer to a different source image.

The original BMP bytes opened with identical pixels under `this_file.bmp`, `this_file (2).bmp`, and `this_file (3).bmp` in native stock CrossInk 1.6.1. Accepted v15 preserved those bytes through guest HTTP and WebSocket uploads. Each received file and download matched the source SHA-256.

The reported invalid BMP remains unresolved. A failing file downloaded from the X3 through File Manager is needed to compare its header and image bytes with the original archive. The filename alone does not identify its source sample.

## Checks

Run `node test/scripts/test_folder_upload.mjs` for the production JavaScript helpers. It checks folder paths, repeated directory-reader batches, FAT name case, cancellation, collision bytes and both transfer methods. The generated portal check is `python3 scripts/test_web_minify.py` after `python3 scripts/build_web.py`.

The browser run selected four BMPs in two subfolders. File Manager kept the nested paths and source bytes. Repeated single-file uploads produced the expected suffixes with unchanged bytes. Closing and reopening the upload dialog reset its selection.
