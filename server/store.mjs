/**
 * Storage + conversion for the VSD viewer.
 *
 * data/<id>/
 *   file.json         library record (name, size, status, page count)
 *   source.vsd        the uploaded drawing
 *   pages/page-NN.svg rendered pages (self contained SVG)
 *   pages/page-NN.json side car: text runs with laid out positions (pt)
 *   meta.json         page index produced by the renderer
 */
import { createHash } from 'node:crypto'
import { execFile } from 'node:child_process'
import { mkdir, readFile, readdir, rm, writeFile, stat } from 'node:fs/promises'
import path from 'node:path'
import { promisify } from 'node:util'
import { fileURLToPath } from 'node:url'

const execFileAsync = promisify(execFile)

const HERE = path.dirname(fileURLToPath(import.meta.url))
export const ROOT = path.resolve(HERE, '..')
export const DATA_DIR = process.env.VSD_VIEWER_DATA || path.join(ROOT, 'data')
export const RENDERER_BIN =
  process.env.VSD2SVG_BIN || path.join(ROOT, 'renderer', 'build', 'vsd2svg')
export const RENDERER_FONTS =
  process.env.VSD2SVG_FONTS || path.join(ROOT, 'renderer', 'fonts.conf')

export function sha256(buffer) {
  return createHash('sha256').update(buffer).digest('hex')
}

async function exists(p) {
  try {
    await stat(p)
    return true
  } catch {
    return false
  }
}

export async function listFiles() {
  await mkdir(DATA_DIR, { recursive: true })
  const entries = await readdir(DATA_DIR, { withFileTypes: true })
  const files = []
  for (const entry of entries) {
    if (!entry.isDirectory()) continue
    try {
      const record = JSON.parse(
        await readFile(path.join(DATA_DIR, entry.name, 'file.json'), 'utf8'),
      )
      files.push(record)
    } catch {
      // ignore half written / foreign directories
    }
  }
  files.sort((a, b) => String(b.createdAt).localeCompare(String(a.createdAt)))
  return files
}

export async function readRecord(id) {
  const record = JSON.parse(await readFile(path.join(DATA_DIR, id, 'file.json'), 'utf8'))
  if (record.status !== 'ready') return record
  const meta = JSON.parse(await readFile(path.join(DATA_DIR, id, 'meta.json'), 'utf8'))
  const pages = []
  for (const page of meta.pages) {
    let textCount = 0
    try {
      const sidecar = JSON.parse(
        await readFile(path.join(DATA_DIR, id, page.json), 'utf8'),
      )
      textCount = (sidecar.texts || []).length
    } catch {
      textCount = 0
    }
    pages.push({
      index: page.index,
      name: page.name,
      svg: `/api/files/${id}/pages/page-${String(page.index).padStart(2, '0')}.svg`,
      source: `/api/files/${id}/pages/page-${String(page.index).padStart(2, '0')}.svg?download=1`,
      widthPt: page.widthPt,
      heightPt: page.heightPt,
      textCount,
    })
  }
  return { ...record, pages }
}

export async function readTexts(id, pageIndex) {
  const name = `page-${String(pageIndex).padStart(2, '0')}.json`
  const sidecar = JSON.parse(await readFile(path.join(DATA_DIR, id, 'pages', name), 'utf8'))
  return sidecar
}

export async function pageFile(id, file) {
  if (!/^page-\d+\.svg$/.test(file)) return null
  const full = path.join(DATA_DIR, id, 'pages', file)
  return (await exists(full)) ? full : null
}

export async function deleteFile(id) {
  await rm(path.join(DATA_DIR, id), { recursive: true, force: true })
}

export async function convert(id, vsdPath, originalName) {
  const dir = path.join(DATA_DIR, id)
  const record = {
    id,
    originalName,
    status: 'pending',
    createdAt: new Date().toISOString(),
    pageCount: 0,
    renderer: path.basename(RENDERER_BIN),
  }
  await writeFile(path.join(dir, 'file.json'), JSON.stringify(record, null, 2))

  try {
    await execFileAsync(
      RENDERER_BIN,
      ['--outdir', dir, '--metrics', RENDERER_FONTS, vsdPath],
      { maxBuffer: 128 * 1024 * 1024 },
    )
    const meta = JSON.parse(await readFile(path.join(dir, 'meta.json'), 'utf8'))
    record.status = 'ready'
    record.pageCount = meta.pageCount
    record.pages = meta.pages.map((p) => ({ index: p.index, name: p.name }))
  } catch (error) {
    record.status = 'failed'
    record.error = String(error?.stderr || error?.message || error).slice(0, 2000)
  }
  await writeFile(path.join(dir, 'file.json'), JSON.stringify(record, null, 2))
  return record
}

/** Store an uploaded drawing and render it; identical content is deduplicated. */
export async function addFile(buffer, originalName) {
  const id = sha256(buffer).slice(0, 12)
  const dir = path.join(DATA_DIR, id)
  await mkdir(path.join(dir, 'pages'), { recursive: true })
  const vsdPath = path.join(dir, 'source.vsd')
  if (!(await exists(vsdPath))) {
    await writeFile(vsdPath, buffer)
  }
  const record = await convert(id, vsdPath, originalName)
  return record
}

/** Optional chunk coverage census (needs python3 + olefile). */
export async function census(id) {
  const script = path.join(ROOT, 'renderer', 'tools', 'census.py')
  const vsdPath = path.join(DATA_DIR, id, 'source.vsd')
  const python = process.env.VSD_VIEWER_PYTHON || 'python3'
  try {
    const { stdout } = await execFileAsync(python, [script, '--json', vsdPath], {
      maxBuffer: 32 * 1024 * 1024,
    })
    const report = JSON.parse(stdout)
    await writeFile(path.join(DATA_DIR, id, 'census.json'), JSON.stringify(report, null, 2))
    return report
  } catch (error) {
    return { error: String(error?.message || error).slice(0, 500) }
  }
}
