// Clicks each keypad button through headless Edge's DevTools protocol and checks the page sees exactly that key.
import { test, before, after } from 'node:test'
import assert from 'node:assert/strict'
import { createServer } from 'node:http'
import { readFile, mkdtemp, rm } from 'node:fs/promises'
import { spawn } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const here = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'))
const edge = process.env.EDGE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
const table = JSON.parse(await readFile(path.join(here, '..', 'keymaps', 'keys.json'), 'utf8'))
let server, browser, profile, ws, nextId = 0
const waiting = new Map()

function cdp (method, params = {}) {
  const id = ++nextId
  ws.send(JSON.stringify({ id, method, params }))
  return new Promise((resolve, reject) => waiting.set(id, m => m.error ? reject(new Error(m.error.message)) : resolve(m.result)))
}
const evaluate = async expr => (await cdp('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.value
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))

async function click (x, y) {
  for (const type of ['mousePressed', 'mouseReleased']) {
    await cdp('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1 })
  }
  await sleep(50)
}

before(async () => {
  server = createServer(async (req, res) => {
    try {
      res.end(await readFile(path.join(here, '..', req.url.split('?')[0])))
    } catch { res.statusCode = 404; res.end() }
  }).listen(0, '127.0.0.1')
  await new Promise(resolve => server.once('listening', resolve))
  profile = await mkdtemp(path.join(tmpdir(), 'keypad-'))
  browser = spawn(edge, ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`, '--no-first-run',
    '--window-size=1280,800', `http://127.0.0.1:${server.address().port}/browser/test.html`])
  const port = await new Promise((resolve, reject) => {
    let text = ''
    browser.stderr.on('data', d => { text += d; const m = text.match(/ws:\/\/127\.0\.0\.1:(\d+)\//); if (m) resolve(m[1]) })
    setTimeout(() => reject(new Error('Edge did not open a DevTools port')), 20000)
  })
  let page
  for (let i = 0; i < 50 && !page; i++) {
    page = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(t => t.type === 'page' && t.url.endsWith('test.html'))
    if (!page) await sleep(200)
  }
  ws = new WebSocket(page.webSocketDebuggerUrl)
  ws.onmessage = e => { const m = JSON.parse(e.data); waiting.get(m.id)?.(m); waiting.delete(m.id) }
  await new Promise(resolve => { ws.onopen = resolve })
  for (let i = 0; i < 50 && !(await evaluate('!!window.keypad')); i++) await sleep(100)
})

after(async () => {
  ws?.close()
  browser?.kill()
  server?.close()
  await sleep(500)
  await rm(profile, { recursive: true, force: true }).catch(() => {})
})

test('a click on each button sends exactly its key down then up', async () => {
  const buttons = await evaluate('keypad.buttons()')
  assert.equal(buttons.length, 6)
  for (const b of buttons) {
    await evaluate('window.events = []')
    await click(b.x, b.y)
    const k = table[b.code]
    assert.deepEqual(await evaluate('window.events'), [['keydown', b.code, k.key, k.keyCode], ['keyup', b.code, k.key, k.keyCode]])
  }
})

test('a click beside the buttons sends no key and focus stays on the game', async () => {
  await evaluate('window.events = []')
  await click(20, 20)
  const [first] = await evaluate('keypad.buttons()')
  await click(first.x - first.w, first.y)
  assert.deepEqual(await evaluate('window.events'), [])
  await evaluate("document.getElementById('game').focus()")
  await click(first.x, first.y)
  assert.equal(await evaluate('document.activeElement.id'), 'game')
})

test('size and spacing change the button geometry', async () => {
  await evaluate('keypad.resize(48, 4)')
  const small = await evaluate('keypad.buttons()')
  assert.equal(Math.round(small.find(b => b.code === 'ArrowUp').w), 48)
  assert.equal(Math.round(small.find(b => b.code === 'Space').w), 100)
  const left = small.find(b => b.code === 'ArrowLeft'), down = small.find(b => b.code === 'ArrowDown')
  assert.equal(Math.round(down.x - left.x), 52)
  await evaluate('window.events = []')
  await click(down.x, down.y)
  assert.deepEqual((await evaluate('window.events')).map(e => e[1]), ['ArrowDown', 'ArrowDown'])
})
