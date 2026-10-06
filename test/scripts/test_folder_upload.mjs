// Run the production upload helpers with an isolated browser I/O model.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { File } from "node:buffer";
import vm from "node:vm";

const source = readFileSync(new URL("../../web/pages/files.js", import.meta.url), "utf8");
function productionFunction(name) {
  const match = source.match(new RegExp(`^(?:async )?function ${name}\\(`, "m"));
  assert.ok(match, name);
  return source.slice(match.index, source.indexOf("\n}", match.index) + 2);
}
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, { files: [], disabled: false });
  return elements.get(id);
}
class Transfer {
  files = [];
  items = { add: (file) => this.files.push(file) };
}
const context = vm.createContext({
  File, TextEncoder, URLSearchParams, WeakMap, Map, Set, DataTransfer: Transfer,
  document: { getElementById: element },
  window: { location: { search: "?path=%2FBooks%2F100%25" } },
  validateFile() {}, console: { log() {}, warn() {}, error() {} },
  setTimeout, clearTimeout,
});
const names = [
  "uploadRelativePath", "uploadDestination", "setUploadSelection", "selectUploadFolder",
  "readDroppedUploadEntry", "prepareUploadDirectory", "reserveAvailableUploadFilename",
  "isSafeFileName", "uploadFileHTTP", "uploadFileWebSocket",
];
vm.runInContext([
  source.match(/^const currentPath = .*$/m)[0],
  source.match(/^const SAFE_FILE_NAME_PATTERN = .*$/m)[0],
  "const uploadRelativePaths = new WeakMap(); let operationCancelled = false;",
  "let isUploadInProgress = false; let currentUploadXhr = null; let currentUploadWs = null;",
  "const WS_CHUNK_SIZE = 8192; function getWsUrl() { return 'ws://test'; }",
  ...names.map(productionFunction),
].join("\n"), context);

assert.equal(vm.runInContext("currentPath", context), "/Books/100%");
const bytes = Buffer.from(Array.from({ length: 24017 }, (_, i) => i % 251));
const file = new File([bytes], "cover.bmp", { type: "image/bmp" });
Object.defineProperty(file, "webkitRelativePath", { value: "Study/detail/cover.bmp" });
element("folderInput").files = [file];
context.selectUploadFolder();
assert.equal(context.uploadDestination(element("fileInput").files[0]), "/Books/100%/Study/detail");
context.setUploadSelection([
  { file, path: "Study/detail/cover.bmp" },
  { file: new File([bytes], ".DS_Store"), path: "Study/.DS_Store" },
  { file: new File([bytes], "._cover.bmp"), path: "Study/._cover.bmp" },
]);
assert.equal(element("fileInput").files.length, 1);
for (const folder of ["..", ".", "", "Bad:", "end.", " space", "x\u0001", "é".repeat(76)]) {
  const invalid = new File([bytes], "cover.bmp");
  context.setUploadSelection([{ file: invalid, path: `${folder}/cover.bmp` }]);
  assert.throws(() => context.uploadDestination(invalid), /Folder name/);
}

// Directory readers can return several batches. Preserve all 105 paths.
const leaves = Array.from({ length: 105 }, (_, i) => ({
  name: `${i}.bmp`, isFile: true,
  file: (resolve) => resolve(new File([bytes], `${i}.bmp`)),
}));
let batch = 0;
const directory = {
  name: "Study", isDirectory: true,
  createReader: () => ({ readEntries: (resolve) => resolve([leaves.slice(0, 100), leaves.slice(100), []][batch++]) }),
};
const dropped = [];
await context.readDroppedUploadEntry(directory, "", dropped);
assert.equal(dropped.length, 105);
assert.equal(dropped.at(-1).path, "Study/104.bmp");

// A FAT directory can already exist with different letter case.
const folders = new Map([
  ["/Books/100%", [{ name: "STUDY", isDirectory: true }]],
  ["/Books/100%/STUDY", []],
]);
const mkdirCalls = [];
context.fetch = async (url, options) => {
  if (options?.method === "POST") {
    const fields = options.body;
    const parent = fields.get("path"), name = fields.get("name");
    mkdirCalls.push({ parent, name });
    folders.set(`${parent}/${name}`, [{ name: "cover.bmp", isDirectory: false }]);
    return { ok: true };
  }
  const path = new URL(url, "http://test").searchParams.get("path");
  assert.ok(folders.has(path), path);
  return { ok: true, json: async () => folders.get(path).map((entry) => ({ ...entry })) };
};
const prepared = await context.prepareUploadDirectory("/Books/100%/Study/detail", new Map());
assert.equal(prepared.path, "/Books/100%/STUDY/detail");
assert.deepEqual(mkdirCalls, [{ parent: "/Books/100%/STUDY", name: "detail" }]);
assert.ok(prepared.names.has("cover.bmp"));
folders.set("/Books/100%", [{ name: "Study", isDirectory: false }]);
await assert.rejects(context.prepareUploadDirectory("/Books/100%/Study/detail", new Map()), /file already/);
vm.runInContext("operationCancelled = true", context);
await assert.rejects(context.prepareUploadDirectory("/Books/100%/Study/detail", new Map()), /aborted/);
assert.equal(mkdirCalls.length, 1);
vm.runInContext("operationCancelled = false", context);

// Check the multipart filename separately from the directory selection path.
const requests = [];
class Multipart {
  fields = [];
  append(...field) { this.fields.push(field); }
}
context.FormData = Multipart;
context.XMLHttpRequest = class {
  upload = {};
  open(method, url) { this.method = method; this.url = url; }
  send(body) { requests.push({ method: this.method, url: this.url, fields: body.fields }); this.status = 200; this.onload(); }
};
await context.uploadFileHTTP(file, null, null, null, prepared.path);
assert.equal(requests[0].fields[0][2], "cover.bmp");
assert.equal(new URL(requests[0].url, "http://test").searchParams.get("path"), prepared.path);
assert.deepEqual(Buffer.from(await requests[0].fields[0][1].arrayBuffer()), bytes);
const used = new Set();
for (const expected of ["cover.bmp", "cover (2).bmp", "cover (3).bmp"]) {
  const name = context.reserveAvailableUploadFilename(file.name, used);
  assert.equal(name, expected);
  const renamed = new File([file], name, { type: file.type });
  await context.uploadFileHTTP(renamed, null, null, null, "/Images");
  assert.deepEqual(Buffer.from(await requests.at(-1).fields[0][1].arrayBuffer()), bytes);
}
assert.equal(context.reserveAvailableUploadFilename(file.name, new Set()), "cover.bmp");

// Exercise the real START message and chunk loop with a renamed file.
const frames = [];
let received = 0;
context.WebSocket = class {
  static OPEN = 1;
  readyState = 1;
  bufferedAmount = 0;
  constructor() { queueMicrotask(() => this.onopen()); }
  send(data) {
    frames.push(data);
    if (typeof data === "string") queueMicrotask(() => this.onmessage({ data: "READY" }));
    else {
      received += data.byteLength;
      if (received === bytes.length) queueMicrotask(() => this.onmessage({ data: "DONE" }));
    }
  }
  close() { this.readyState = 3; this.onclose?.({ code: 1000, reason: "" }); }
};
await context.uploadFileWebSocket(new File([file], "cover (3).bmp"), null, null, null, prepared.path);
assert.equal(frames[0], `START:cover (3).bmp:${bytes.length}:${prepared.path}`);
assert.deepEqual(Buffer.concat(frames.slice(1).map((frame) => Buffer.from(frame))), bytes);
console.log("PASS: folder paths, 105 dropped files, FAT name case, cancellation, multipart filenames, collision bytes and WebSocket chunks");
