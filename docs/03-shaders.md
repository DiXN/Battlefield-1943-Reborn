# Shader preparation (4 Nov 2026)

TekRantGaming's rule: shaders are built on the player's PC from their own game files; we ship no shader code, only our list
(shader hashes, where each shader is in the game files, and pipeline descriptions: shader hashes and render state).

## Why
The runtime translates a Xenos shader and makes a pipeline the first time a draw needs it, on a worker thread; until
it is ready the draw is skipped (objects missing for a moment). Run sh1, fresh cache: 207 pipelines made during play
and 2,191 draws skipped, most of them in the first seconds after spawning. The runtime also keeps what it made in
storage files (`<cache>\shaders\shareable\5841097E.xsh` and `5841097E.rtv.d3d12.xpso`) and makes all of them when
the game starts: run sh2 (seeded storage) translated 290 shaders in 9 ms and made 246 pipelines in 40 ms at start,
and made none during play.

## How
- `tools/shader_list.py` (ours, run by us): reads recorded storage from test runs and the game files, and writes
  `bf/data/shaders/bf1943-shaders.list` plus the merged pipeline descriptions. Pixel shaders are byte for byte in
  the level shader databases (`Levels/*/Shaders/Single.res`, type Dx9ShaderDatabase) and the separate Xenon shader
  resources; vertex shaders differ in 2 to 22 dwords (the vertex fetch fields the game's D3D fills in when a shader
  is bound), kept as XOR masks; two shaders are built into default.xex (read from the loaded image).
- `bf/src/shader_prep.cpp` (in the game): after the xex is loaded and before the runtime opens its storage, reads the
  listed shaders from the player's archives (`.fbrb`, inflated with miniz), applies the masks, checks every shader
  against its hash, and adds them and the pipeline descriptions to the storage. Once per list version (marker
  `bf1943-prepared.txt`). Run sh3, fresh cache: 290 shaders in 2.0 s, then 5 pipelines made during play (new ones).
- The list grows from every recorded run: `python -I tools/shader_list.py --game input/pkg --out bf/data/shaders
  <storage folders>`. 4 Nov: 662 shaders, 669 pipelines (Direct3D 12) from all runs so far.

## NativeXRenderer (6 Nov 2026, what players run)
Players draw through NativeXRenderer (Vulkan), which keeps its own storage, `<cache>\native_x_renderer\5841097E.pipelines`
(shader microcode, translations, pipelines; D42) and makes everything in it at its first draw. Before this, nothing
seeded it, so a first session made every pipeline during play (TekRantGaming's first session: 1,368 pipelines, about 31,000
draws skipped, objects flickering in).
- `tools/shader_list.py` reads NativeXRenderer's storage files too (`--scan runs` finds every storage folder) and
  writes `bf/data/shaders/5841097E.nxr`: that file's translations and pipelines (shader hashes, modifications and render
  state), with no shader records. Its shaders join the list (all 1,079 recorded so far are found in the game files).
- `shader_prep.cpp` writes the player's storage on a first start (and when the list changes): the existing records
  stay; the listed shaders are added from the player's archives, then our translations and pipelines.
- NativeXRenderer reports the progress (D134, `native_x_renderer::StorageProgress`) and the shader screen in our menu
  waits on it. Run shnxr3 (fresh cache, the driver's cache cold for most of them): 1,688 pipelines compiled 17 s after
  the first draw; shnxr5 (fresh cache, the driver's warm): 1.7 s, then 0 pipelines created and 0 draws skipped in a
  Wake Island match.
- First start on a PC whose driver has never compiled them (a fresh NVIDIA cache folder, run first1): the archives
  read in 0.9 s on threads (3.4 s before), then 1,688 pipelines in 24 s behind the shader screen (NXR D135: fourteen
  compile threads at normal priority until then, below normal in play); 2 new pipelines at the spawn and none after in
  a Guadalcanal match. Later starts: 1.4 s. Coverage runs on NativeXRenderer (cov_iwo1, cov_coral1, cov_gc1, shnxr5):
  2 to 7 new pipelines a match on each of the four maps.
- To grow the list: play or script runs on NativeXRenderer (each map, vehicles, weapons, effects), then
  `python -I tools/shader_list.py --game input/pkg --out bf/data/shaders --scan runs`.

## Once per version (11 Nov 2026, King Kong's rule)
TekRantGaming: shaders compile when the game files are first given, then never again until the game updates, when
they are all compiled again; then "Copy Kong exactly". King Kong's own rule (its kk/src/shader_prepared.h, owner 18
Oct 2026; the King Kong Linux lead's notes, side-session note kk-shaders-for-bf1943.md): one preparation per version,
at the first setup or after an update; a version already prepared is never checked or rebuilt at a start; and on an
update nothing is deleted: everything is made again over the old files. Since v0.4.7 (16 Nov 2026) the last part is
TekRantGaming's own: "if the game is updated it removes it shader chashe and builds a fresh batch of shaders with the
same screen then you get dropped back into our main menu" (below).

## The shader screen's place (16 Nov 2026, v0.4.7)
TekRantGaming: "the initial setup screen when you give the game files right after it you should be in the full screen
shader comp screen ... the actual onboaring section when you go through settings should no longer include the shader
part ... make sure nothing overlays over the shaders it should be the only screen visable until its finished".
- First start: the game files (setup step 1), then at once the full screen shader page (no startup cards on that
  run), then the onboarding from the soldier name. The onboarding has eight steps; the shader step 8 of v0.4.1 to
  v0.4.6 is gone. The game starts behind the shader page, so the onboarding's pages run with it started (settings
  the game takes only at the start make Ready's button FINISH AND RESTART).
- After an update: the startup cards (none when the updater started the game again), the shader page over a fresh
  cache ("UPDATED TO VERSION"), then the main menu. A start whose shader page never finished shows it again first,
  before the onboarding or the main menu (`game_menu.cpp` AfterIntro).
- Nothing is drawn over the page: our pop-ups (new version, maintenance and refusal notices, What's new, the news
  window and its video, the version gate, a join's questions) wait for the page after it (`game_menu.cpp` OnDraw),
  the F12 report screenshot and its note are off, our menu sounds are silent, and the performance metrics, F8 panel,
  chat and spawn pointer were already kept off the launcher's pages.
- The marker is `shaders_prepared_for` in `user/bf1943-settings.txt`: the version whose shader page compiled every
  stored pipeline. It is written only when the page reaches its end (a page left half way is not a preparation).
- At each start, before the shaders are prepared and before the renderer opens its storage
  (`bf1943_app.h` OnPostLoadXexImage, `game_menu.cpp` CheckShadersAfterUpdate):
  - marker of another version: an update (`update.cpp` PrepareShadersAfterUpdate). Since v0.4.7 the old cache is
    deleted first: `cache/native_x_renderer` (the renderer's storage, `5841097E.pipelines`, with the pipelines this
    PC learned in play, and DiligentCore's pipeline cache `5841097E.vkcache`) and `cache/shaders` (the prepared
    shaders and their marker). The preparation then writes this version's list from the player's game files and the
    shader page ("UPDATED TO VERSION") compiles every pipeline from nothing. Until the page has finished, each start
    counts as updated; the cache is deleted only once a version (`cache/native_x_renderer/cleared-for.txt`), so a
    later start goes on over what was made. The pipelines learned in play are lost at each update; the players'
    uploads (below) bring them into the next version's shipped list.
  - (11 Nov, first try, replaced the same day at "Copy Kong exactly": the whole cache was deleted on an update.
    From v0.4.1 to v0.4.6 nothing was deleted: the page made every stored pipeline again over the old cache.)
  - marker of this version and the renderer's storage still there: no shader page. The renderer makes its stored
    pipelines behind the menus from the driver's cache (the log says when they are done); pipelines first met in
    play are kept in the storage and made the same way at the next start.
  - no marker (a first start, a setup closed before the page finished) or the storage gone: the shader page.
- A driver or GPU change does not show the page (King Kong's rule too): Vulkan ignores a pipeline cache made by
  another driver or device, so the renderer makes the pipelines again behind the menus, cold.

## Players' pipelines (the Cloudflare Worker)
Players' games send their storage's pipelines and translations (no shader code) to the Kong Worker
(`pipeline_upload.cpp`), which keeps the two newest versions. At a release they are fetched with
`node F:\KK-native-renderer\server-api\scripts\fetch-bf-pipelines.mjs <version> runs\uploads_<date>\<version>` (the
script reads its token itself; never open `.dev.vars`) and merged over a test run's storage prepared from the shipped
list, so every shipped entry stays: `python -I tools/shader_list.py --game input/pkg --out <scratch> <that run's
native_x_renderer and shaders/shareable folders> <the uploads folders>`, then the new `5841097E.nxr` is checked (old
translations and pipelines all kept) and copied to `bf/data/shaders`.
- 8 Nov (0.1.11 to 0.1.14): 91 pipelines and 13 translations added (1,943 pipelines, 1,042 translations).
- 11 Nov (0.1.33 and 0.2.0, 49 files, all four maps): 193 pipelines and 46 translations added (2,136 pipelines,
  1,088 translations); the shader list itself unchanged (1,089 shaders, every one found in the game files).
- 15 Nov (0.4.6.4 and 0.4.6.3, 845 files, all four maps, plus every test run here): 653 pipelines and 41
  translations added (3,015 pipelines, 1,157 translations, 1,117 shaders, all in the game files); 170 pipelines and
  100 translations from the uploads left out, their shaders never recorded here. Run list15a: all 2,839 that a start
  makes compiled in 1.5 s, no errors.

## Next
- More coverage: every map, vehicles, weapons, effects, menus; a Vulkan recording (Linux uses Vulkan).
- The setup page in our menu: one progress bar for preparation plus the runtime's start time creation.
- Third party: `bf/third_party/miniz` (miniz 3.0.2, MIT, from the official GitHub release).
