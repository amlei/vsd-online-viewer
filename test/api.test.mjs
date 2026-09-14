/**
 * End to end API tests: upload a real .vsd, render it with the libvisio fork and
 * read the pages back.
 *
 *   VSD_SAMPLE=/path/to/drawing.vsd node --test test/
 *
 * Without VSD_SAMPLE the rendering tests are skipped, so `npm test` still works
 * on a machine that has no sample drawing.
 */
import assert from 'node:assert/strict'
import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { existsSync } from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { after, before, describe, it } from 'node:test'

const SAMPLE = process.env.VSD_SAMPLE || ''
const hasSample = SAMPLE && existsSync(SAMPLE)
const HAIRLINE_SAMPLE = process.env.VSD_SAMPLE_HAIRLINE || ''
const hasHairlineSample = HAIRLINE_SAMPLE && existsSync(HAIRLINE_SAMPLE)

let server
let baseUrl
let dataDir

async function api(pathname, options) {
  const response = await fetch(baseUrl + pathname, options)
  const text = await response.text()
  let body
  try {
    body = JSON.parse(text)
  } catch {
    body = text
  }
  return { status: response.status, body, response }
}

before(async () => {
  dataDir = await mkdtemp(path.join(os.tmpdir(), 'vsd-viewer-test-'))
  process.env.VSD_VIEWER_DATA = dataDir
  process.env.VSD_VIEWER_NO_LISTEN = '1'
  const mod = await import('../server/server.mjs')
  server = mod.server
  const port = await mod.start(0)
  baseUrl = `http://127.0.0.1:${port}`
})

after(async () => {
  await new Promise((resolve) => server.close(resolve))
  await rm(dataDir, { recursive: true, force: true })
})

describe('library api', () => {
  it('starts with an empty library', async () => {
    const { status, body } = await api('/api/files')
    assert.equal(status, 200)
    assert.deepEqual(body.files, [])
  })

  it('rejects non vsd uploads', async () => {
    const { status, body } = await api('/api/files?name=notes.txt', {
      method: 'POST',
      body: Buffer.from('hello'),
    })
    assert.equal(status, 400)
    assert.match(body.error, /vsd/i)
  })

  it('rejects empty uploads', async () => {
    const { status } = await api('/api/files?name=empty.vsd', {
      method: 'POST',
      body: Buffer.alloc(0),
    })
    assert.equal(status, 400)
  })

  it('serves the viewer ui', async () => {
    const { status, body } = await api('/')
    assert.equal(status, 200)
    assert.match(body, /VSD Online Viewer/)
  })

  it('404s unknown api routes', async () => {
    const { status } = await api('/api/nope')
    assert.equal(status, 404)
  })
})

describe('rendering', { skip: hasSample ? false : 'set VSD_SAMPLE to run' }, () => {
  let fileId
  let detail

  it('uploads and renders every page', async () => {
    const buffer = await readFile(SAMPLE)
    const { status, body } = await api(
      `/api/files?name=${encodeURIComponent(path.basename(SAMPLE))}`,
      { method: 'POST', body: buffer },
    )
    assert.equal(status, 201, JSON.stringify(body).slice(0, 400))
    assert.equal(body.status, 'ready')
    assert.ok(body.pageCount > 0)
    fileId = body.id
  })

  it('exposes the page index', async () => {
    const { status, body } = await api(`/api/files/${fileId}`)
    assert.equal(status, 200)
    assert.equal(body.pages.length, body.pageCount)
    assert.ok(body.pages[0].widthPt > 100)
    assert.ok(body.pages[0].name.length > 0)
    detail = body
  })

  it('serves page svg and text sidecars', async () => {
    const page = detail.pages[0]
    const svg = await fetch(baseUrl + page.svg)
    assert.equal(svg.status, 200)
    const markup = await svg.text()
    assert.match(markup, /<svg[^>]*viewBox="0 0 [\d.]+ [\d.]+"/)

    const texts = await api(`/api/files/${fileId}/pages/${page.index}.json`)
    assert.equal(texts.status, 200)
    assert.ok(Array.isArray(texts.body.texts))
  })

  it('never emits the invisible stroke-width:0 on any page', async () => {
    // Regression guard: librevenge used to write stroke-width:0 for Visio
    // hair lines, and SVG paints nothing for a zero width stroke.
    for (const page of detail.pages) {
      const markup = await (await fetch(baseUrl + page.svg)).text()
      assert.ok(
        !/stroke-width:\s*(?:0|0\.0+)\s*;/.test(markup),
        `page ${page.index} contains an invisible stroke-width:0`,
      )
    }
  })

  it('renders text with explicit per line positions', async () => {
    const page = detail.pages[0]
    const texts = await api(`/api/files/${fileId}/pages/${page.index}.json`)
    const nonEmpty = texts.body.texts.filter((t) => t.text.trim().length > 0)
    assert.ok(nonEmpty.length > 0, 'expected text runs on the first page')
    for (const text of nonEmpty.slice(0, 50)) {
      assert.ok(Number.isFinite(text.x) && Number.isFinite(text.y))
      assert.ok(text.fontSize > 0)
    }
  })

  it('reports chunk coverage', async () => {
    const { status, body } = await api(`/api/files/${fileId}/census`)
    assert.equal(status, 200)
    if (body.error) {
      // olefile / python3 not available - acceptable on a bare machine
      assert.match(String(body.error), /python|olefile|ENOENT|not found/i)
      return
    }
    assert.ok(Array.isArray(body))
    assert.ok(body[0].chunks > 0)
    assert.ok(body[0].ratio >= 0 && body[0].ratio < 1)
  })

  it('deduplicates identical uploads', async () => {
    const buffer = await readFile(SAMPLE)
    const { status, body } = await api('/api/files?name=again.vsd', {
      method: 'POST',
      body: buffer,
    })
    assert.equal(status, 201)
    assert.equal(body.id, fileId)
  })

  it('deletes a drawing', async () => {
    const { status } = await api(`/api/files/${fileId}`, { method: 'DELETE' })
    assert.equal(status, 200)
    const list = await api('/api/files')
    assert.deepEqual(list.body.files, [])
  })
})

describe(
  'hairline drawings',
  { skip: hasHairlineSample ? false : 'set VSD_SAMPLE_HAIRLINE to run' },
  () => {
    it('emulates Visio hair lines as non scaling 1px strokes', async () => {
      const buffer = await readFile(HAIRLINE_SAMPLE)
      const { status, body } = await api(
        `/api/files?name=${encodeURIComponent(path.basename(HAIRLINE_SAMPLE))}`,
        { method: 'POST', body: buffer },
      )
      assert.equal(status, 201, JSON.stringify(body).slice(0, 300))
      const detail = await api(`/api/files/${body.id}`)
      const markup = await (await fetch(baseUrl + detail.body.pages[0].svg)).text()
      assert.match(markup, /vector-effect:non-scaling-stroke/)
      const covered = /vector-effect:non-scaling-stroke/.test(markup)
      assert.ok(covered)
      await api(`/api/files/${body.id}`, { method: 'DELETE' })
    })
  },
)
