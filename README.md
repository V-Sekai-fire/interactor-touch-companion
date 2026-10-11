# Touch Companion

Lets an agent that can only click, such as [XR Pilot](https://github.com/V-Sekai-fire/interactor-xr-pilot) through
the SteamVR desktop view, play a keyboard game. Large on-screen buttons turn clicks into key presses. Both forms
below read the same key map.

## Key maps

A game declares its keys in a JSON file. Each key is named by its DOM
[`KeyboardEvent.code`](https://developer.mozilla.org/docs/Web/API/UI_Events/Keyboard_event_code_values) and placed on a
grid:

```json
{
 "name": "default",
 "keys": [
  {"code": "ArrowUp", "label": "↑", "row": 0, "col": 1},
  {"code": "ArrowLeft", "label": "←", "row": 1, "col": 0},
  {"code": "Space", "label": "Space", "row": 1, "col": 4, "span": 2}
 ]
}
```

A button sits at `col × (size + gap)`, `row × (size + gap)`. A `span` of n is `n × size + (n − 1) × gap` wide.
Button `size` and `gap` are run-time settings, so a harness can treat them as level attributes.

[`keymaps/keys.json`](keymaps/keys.json) maps each code to its `key`, `keyCode`, Windows virtual key, scancode and
extended flag. It covers:

- the arrows
- Space, Enter, Escape, Tab and Backspace
- left Shift and left Control
- A to Z and 0 to 9

Alt and the Windows key are left out on purpose, so that no button can make Alt+F4 or Alt+Tab. A map that names a
code not in the table is refused.

[`keymaps/default.json`](keymaps/default.json) is the arrows, Enter and Space.

## Browser games: `browser/keypad.js`

Add the keypad to the game's page:

```html
<script src="keypad.js" data-map="mygame.json" data-keys="keys.json" data-size="96" data-gap="16"></script>
```

- The buttons sit at the bottom centre of the page.
- A button sends `keydown` when the pointer goes down and `keyup` when it lifts. Each event carries `key`, `code` and
  `keyCode`, bubbles, and goes to the focused element or the document.
- Pressing a button does not move focus away from the game.
- `keypad.resize(size, gap)` changes the layout.
- `keypad.buttons()` returns each button's centre and size in physical screen pixels, so a harness knows where to
  click.

## Native Windows games: `touch-companion`

```
touch-companion --map mygame.json --keys keymaps/keys.json --size 96 --gap 16 --view-scale 1.0 \
                --state state.json -- game.exe args...
```

The companion either launches the game or attaches to it with `--pid N`. It then places one window over the game's
window.

- **Picture.** The window shows the game's frames above a strip of buttons. Windows Graphics Capture records the
  frames into a D3D11 texture. That texture is shared with the window's Vulkan device through an NT handle and
  scaled into the window by `vkCmdBlitImage`. The pixels are never copied through the CPU.
- **Window.** The window and buttons are [panelspun](https://github.com/V-Sekai-fire/interactor-panelspun)
  (SDL3 and ThorVG on Vulkan), the toolkit XR Pilot uses. The window is topmost, borderless and `WS_EX_NOACTIVATE`.
  A click on it never takes focus, so the game stays the foreground window.
- **Fit.** The picture is drawn at `--view-scale` (at most 1) of the largest size that fits its region, keeping
  the game's aspect. The buttons shrink, never grow, to fit the strip, which takes at most 40% of the window. A
  layout guard refuses to start (exit 5) if any button or the picture would leave its region or overlap, or a
  button would be under 32 px.
- **Guard.** A button sends its key with `SendInput`, as a scancode. It does so only after the guard confirms that
  the foreground window belongs to the game's process. Otherwise the key is refused and counted, and the window
  shows why. A key can never reach another app.
- **Clicks on the picture.** These are mapped back to the game's client coordinates and posted to the game's window
  under the same guard.
- **State.** `--state` writes JSON for the harness: the picture's rect, each button's centre in desktop pixels,
  frames captured and presented, keys sent, and how many were refused.

## Build and test

```
cmake -S . -B build -G Ninja -DCOMPANION_GPU_TESTS=ON   # in an MSVC developer shell
cmake --build build
ctest --test-dir build --output-on-failure
node --test browser/keypad.test.mjs                      # headless Edge, driven over the DevTools protocol
```

The tests check:

- **Browser keypad.** A real click on each button produces exactly that key's keydown and keyup on the page. A click
  beside the buttons produces none, and focus stays on the game.
- **Exact keys.** Each native button produces exactly its scancode down and up.
- **Guard.** It refuses when the foreground window belongs to another process, including the live foreground
  against a game with no window. A refused key or click sends nothing.
- **Capture** (`COMPANION_GPU_TESTS`). Frames are captured from a test window, and a repaint shows the new colour.
- **Companion** (`COMPANION_GPU_TESTS`). Run against a test game, it presents the game's colour in its picture.

The GPU tests use on-screen windows at alpha 1/255 that clicks pass through. Capture records windows that are off
every monitor as black, and this way the person at the desk sees nothing. CI runs everything except the GPU tests.

## Licences

This repository is Apache-2.0 OR MIT.

- `third_party/panelspun` is a git subtree of interactor-panelspun (Apache-2.0 OR MIT) and keeps its own licence
  files.
- nlohmann/json 3.11.3 (MIT) is fetched at configure time and pinned by hash.
- The D3D11-to-Vulkan sharing in `src/frame_share.*` was written here from the Vulkan and D3D11 specifications. XR
  Pilot's OXRSys interop does the same job but is MPL-2.0, so none of it is copied.
