// SPDX-License-Identifier: Apache-2.0 OR MIT
// Turns pointer presses on large on-screen buttons into keydown/keyup on the page, for agents that can only click.
(function () {
  var script = document.currentScript
  var base = script ? script.src.replace(/[^/]*$/, '') : ''

  function layout (map, size, gap) {
    return map.keys.map(function (k) {
      var span = k.span || 1
      return { code: k.code, label: k.label || k.code, x: k.col * (size + gap), y: k.row * (size + gap), w: span * size + (span - 1) * gap, h: size }
    })
  }

  function Keypad (map, table, opts) {
    opts = opts || {}
    this.map = map
    this.table = table
    this.size = opts.size || 96
    this.gap = opts.gap || 16
    this.target = opts.target || null
    this.log = []
    this.root = document.createElement('div')
    this.root.style.cssText = 'position:fixed;left:50%;bottom:24px;z-index:99998;user-select:none;touch-action:none'
    document.body.appendChild(this.root)
    this.render()
  }

  Keypad.prototype.render = function () {
    var self = this
    var cells = layout(this.map, this.size, this.gap)
    var width = Math.max.apply(null, cells.map(function (c) { return c.x + c.w }))
    var height = Math.max.apply(null, cells.map(function (c) { return c.y + c.h }))
    this.root.innerHTML = ''
    this.root.style.width = width + 'px'
    this.root.style.height = height + 'px'
    this.root.style.marginLeft = (-width / 2) + 'px'
    cells.forEach(function (c) {
      var b = document.createElement('div')
      b.textContent = c.label
      b.dataset.code = c.code
      b.style.cssText = 'position:absolute;display:flex;align-items:center;justify-content:center;box-sizing:border-box;' +
        'border:3px solid #fff;border-radius:12px;background:#203040;color:#fff;font:bold ' + Math.round(self.size / 3) + 'px sans-serif;' +
        'left:' + c.x + 'px;top:' + c.y + 'px;width:' + c.w + 'px;height:' + c.h + 'px'
      var down = false
      b.addEventListener('pointerdown', function (e) {
        e.preventDefault()
        if (down) return
        down = true
        b.style.background = '#c03080'
        b.setPointerCapture && b.setPointerCapture(e.pointerId)
        self.send(c.code, 'keydown')
      })
      var up = function () {
        if (!down) return
        down = false
        b.style.background = '#203040'
        self.send(c.code, 'keyup')
      }
      b.addEventListener('pointerup', up)
      b.addEventListener('pointercancel', up)
      b.addEventListener('lostpointercapture', up)
      b.addEventListener('mousedown', function (e) { e.preventDefault() })
      self.root.appendChild(b)
    })
  }

  Keypad.prototype.send = function (code, type) {
    var k = this.table[code]
    if (!k) return
    var target = this.target || (document.activeElement && document.activeElement !== document.body ? document.activeElement : document)
    target.dispatchEvent(new KeyboardEvent(type, { key: k.key, code: code, keyCode: k.keyCode, which: k.keyCode, bubbles: true, cancelable: true }))
    this.log.push([type, code])
  }

  Keypad.prototype.resize = function (size, gap) {
    this.size = size
    this.gap = gap
    this.render()
  }

  Keypad.prototype.buttons = function () {
    var dpr = window.devicePixelRatio || 1
    return Array.prototype.map.call(this.root.children, function (b) {
      var r = b.getBoundingClientRect()
      return { code: b.dataset.code, x: (window.screenX + r.left + r.width / 2) * dpr, y: (window.screenY + r.top + r.height / 2) * dpr, w: r.width * dpr, h: r.height * dpr }
    })
  }

  window.Keypad = Keypad
  if (script && script.dataset.map) {
    Promise.all([fetch(script.dataset.map), fetch(script.dataset.keys || base + 'keys.json')].map(function (p) {
      return p.then(function (r) { return r.json() })
    })).then(function (both) {
      window.keypad = new Keypad(both[0], both[1], { size: +script.dataset.size || 96, gap: +script.dataset.gap || 16 })
    })
  }
})()
