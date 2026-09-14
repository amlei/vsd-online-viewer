/**
 * VSD online viewer - HTTP server.
 *
 *   GET    /                                   the viewer UI
 *   GET    /api/files                          library
 *   POST   /api/files?name=x.vsd               upload (raw body) + render
 *   GET    /api/files/:id                      file + page index
 *   GET    /api/files/:id/census               chunk coverage report
 *   GET    /api/files/:id/pages/:page.json     laid out text runs (pt)
 *   GET    /api/files/:id/pages/page-NN.svg    rendered page
 *   DELETE /api/files/:id
 *
 * Uploads use the raw request body (no multipart dependency): the browser sends
 * `fetch('/api/files?name=' + encodeURIComponent(file.name), {method:'POST', body:file})`.
 */
import { createServer } from 'node:http'
import { createReadStream } from 'node:fs'
import { readFile, stat } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import {
  ROOT,
  addFile,
  census,
  deleteFile,
  listFiles,
  pageFile,
  readRecord,
  readTexts,
} from './store.mjs'

const PUBLIC_DIR = path.join(ROOT, 'public')
const PORT = Number(process.env.PORT || 4310)
const MAX_UPLOAD = Number(process.env.VSD_VIEWER_MAX_UPLOAD || 200 * 1024 * 1024)

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
}

function sendJson(res, status, body) {
  const payload = JSON.stringify(body)
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(payload),
    'cache-control': 'no-store',
  })
  res.end(payload)
}

async function readBody(req, limit) {
  const chunks = []
  let size = 0
  for await (const chunk of req) {
    size += chunk.length
    if (size > limit) throw new Error(`文件超过 ${Math.round(limit / 1024 / 1024)}MB`)
    chunks.push(chunk)
  }
  return Buffer.concat(chunks)
}

async function serveFile(res, filePath, { download = false } = {}) {
  const info = await stat(filePath)
  const type = MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream'
  const headers = {
    'content-type': type,
    'content-length': info.size,
    // the UI is served from disk on every request; never let a stale copy stick
    'cache-control': 'no-cache',
  }
  if (download) headers['content-disposition'] = `attachment; filename="${path.basename(filePath)}"`
  res.writeHead(200, headers)
  createReadStream(filePath).pipe(res)
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`)
  const { pathname } = url
  try {
    if (pathname === '/api/files' && req.method === 'GET') {
      return sendJson(res, 200, { files: await listFiles() })
    }

    if (pathname === '/api/files' && req.method === 'POST') {
      const name = url.searchParams.get('name') || 'upload.vsd'
      if (!/\.vsd$/i.test(name)) return sendJson(res, 400, { error: '仅支持 .vsd 文件' })
      const body = await readBody(req, MAX_UPLOAD)
      if (body.length === 0) return sendJson(res, 400, { error: '空文件' })
      const record = await addFile(body, name)
      return sendJson(res, record.status === 'ready' ? 201 : 422, record)
    }

    const fileMatch = pathname.match(/^\/api\/files\/([A-Za-z0-9_-]+)$/)
    if (fileMatch) {
      const id = fileMatch[1]
      if (req.method === 'GET') return sendJson(res, 200, await readRecord(id))
      if (req.method === 'DELETE') {
        await deleteFile(id)
        return sendJson(res, 200, { ok: true })
      }
      return sendJson(res, 405, { error: 'method not allowed' })
    }

    const censusMatch = pathname.match(/^\/api\/files\/([A-Za-z0-9_-]+)\/census$/)
    if (censusMatch && req.method === 'GET') {
      return sendJson(res, 200, await census(censusMatch[1]))
    }

    const textMatch = pathname.match(/^\/api\/files\/([A-Za-z0-9_-]+)\/pages\/(\d+)\.json$/)
    if (textMatch && req.method === 'GET') {
      return sendJson(res, 200, await readTexts(textMatch[1], Number(textMatch[2])))
    }

    const svgMatch = pathname.match(/^\/api\/files\/([A-Za-z0-9_-]+)\/pages\/(page-\d+\.svg)$/)
    if (svgMatch && req.method === 'GET') {
      const full = await pageFile(svgMatch[1], svgMatch[2])
      if (!full) return sendJson(res, 404, { error: 'page not found' })
      return serveFile(res, full, { download: url.searchParams.has('download') })
    }

    // static UI
    let target = pathname === '/' ? '/index.html' : pathname
    const full = path.join(PUBLIC_DIR, path.normalize(target).replace(/^(\.\.[/\\])+/, ''))
    if (!full.startsWith(PUBLIC_DIR)) return sendJson(res, 403, { error: 'forbidden' })
    try {
      await stat(full)
      return serveFile(res, full)
    } catch {
      return sendJson(res, 404, { error: 'not found' })
    }
  } catch (error) {
    const message = String(error?.message || error)
    const status = /ENOENT/.test(message) ? 404 : 500
    return sendJson(res, status, { error: message })
  }
})

/** Start listening; resolves with the actual port (0 picks a free one). */
export function start(port = PORT) {
  return new Promise((resolve, reject) => {
    server.once('error', reject)
    server.listen(port, () => resolve(server.address().port))
  })
}

if (process.env.VSD_VIEWER_NO_LISTEN !== '1') {
  start()
    .then((port) => {
      console.log(`vsd-online-viewer listening on http://localhost:${port}`)
      console.log(`  data:     ${process.env.VSD_VIEWER_DATA || 'data/'}`)
      console.log(`  renderer: ${process.env.VSD2SVG_BIN || 'renderer/build/vsd2svg'}`)
    })
    .catch((error) => {
      console.error('failed to start:', error)
      process.exit(1)
    })
}

export { server }
