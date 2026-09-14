const state = {
  files: [],
  file: null, // detail with pages
  pageIndex: 0,
  zoom: 1,
  pan: { x: 0, y: 0 },
}

const $ = (id) => document.getElementById(id)
const stage = $('stage')
const pageWrap = $('page-wrap')
const pageEl = $('page')
const pageImage = $('page-image')
const emptyState = $('empty')

// ---------------------------------------------------------------- library
async function loadFiles(selectId) {
  const response = await fetch('/api/files')
  const data = await response.json()
  state.files = data.files || []
  renderFiles()
  const target = selectId || state.file?.id || state.files[0]?.id
  if (target) await selectFile(target)
  else showEmpty('还没有图纸', '上传一个 .vsd 文件即可逐页查看')
}

function renderFiles() {
  const list = $('file-list')
  list.innerHTML = ''
  $('file-count').textContent = state.files.length ? `${state.files.length} 份` : ''
  for (const file of state.files) {
    const li = document.createElement('li')
    li.dataset.testid = 'file-item'
    li.className = state.file?.id === file.id ? 'active' : ''
    const name = document.createElement('span')
    name.className = 'name'
    name.textContent = file.originalName
    const sub = document.createElement('span')
    sub.className = 'sub'
    const created = new Date(file.createdAt)
    sub.textContent =
      file.status === 'ready'
        ? `${file.pageCount} 页 · ${created.toLocaleString()}`
        : `渲染失败 · ${created.toLocaleString()}`
    li.append(name, sub)
    li.onclick = () => selectFile(file.id)
    list.appendChild(li)
  }
}

async function selectFile(id) {
  const response = await fetch(`/api/files/${id}`)
  if (!response.ok) {
    setStatus('读取图纸失败', true)
    return
  }
  state.file = await response.json()
  renderFiles()
  renderPageSelect()
  if (state.file.status !== 'ready' || !state.file.pages?.length) {
    showEmpty('这份图纸没有可显示的页面', state.file.error || '渲染失败')
    return
  }
  await selectPage(0)
}

function renderPageSelect() {
  const select = $('page-select')
  select.innerHTML = ''
  const pages = state.file?.pages || []
  for (const page of pages) {
    const option = document.createElement('option')
    option.value = String(page.index)
    option.textContent = `${page.index}. ${page.name}`
    select.appendChild(option)
  }
  select.disabled = pages.length === 0
}

// ------------------------------------------------------------------- page
async function selectPage(index) {
  const pages = state.file?.pages || []
  if (!pages.length) {
    showEmpty('还没有图纸', '上传一个 .vsd 文件即可逐页查看')
    return
  }
  state.pageIndex = Math.max(0, Math.min(index, pages.length - 1))
  const page = pages[state.pageIndex]
  $('page-select').value = String(page.index)
  hideEmpty()
  pageEl.style.width = `${page.widthPt}px`
  pageEl.style.height = `${page.heightPt}px`
  pageImage.src = page.svg
  pageImage.alt = page.name
  $('download-page').href = `${page.svg}?download=1`
  $('page-meta').textContent =
    `${page.name} · ${page.widthPt.toFixed(0)}×${page.heightPt.toFixed(0)}pt · ${page.widthPt > page.heightPt ? '横向' : '纵向'}`
  fitPage()
}

function showEmpty(title, hint) {
  pageWrap.hidden = true
  emptyState.hidden = false
  emptyState.querySelector('p').textContent = title
  emptyState.querySelector('.muted').textContent = hint
}

function hideEmpty() {
  emptyState.hidden = true
  pageWrap.hidden = false
}

// ------------------------------------------------------------- view maths
function applyTransform() {
  pageWrap.style.transform = `translate(${state.pan.x}px, ${state.pan.y}px) scale(${state.zoom})`
  $('zoom-label').textContent = `${Math.round(state.zoom * 100)}%`
}

function fitPage() {
  const page = state.file?.pages?.[state.pageIndex]
  if (!page) return
  const padding = 24
  const zoom = Math.min(
    (stage.clientWidth - padding) / page.widthPt,
    (stage.clientHeight - padding) / page.heightPt,
  )
  state.zoom = Math.max(0.05, Math.min(8, zoom))
  state.pan = {
    x: (stage.clientWidth - page.widthPt * state.zoom) / 2,
    y: (stage.clientHeight - page.heightPt * state.zoom) / 2,
  }
  applyTransform()
}

function setZoom(zoom, center) {
  const page = state.file?.pages?.[state.pageIndex]
  if (!page || pageWrap.hidden) return
  const next = Math.max(0.05, Math.min(12, zoom))
  const cx = center?.x ?? stage.clientWidth / 2
  const cy = center?.y ?? stage.clientHeight / 2
  const px = (cx - state.pan.x) / state.zoom
  const py = (cy - state.pan.y) / state.zoom
  state.zoom = next
  state.pan = { x: cx - px * next, y: cy - py * next }
  applyTransform()
}

// -------------------------------------------------------------- interaction
stage.addEventListener('mousedown', (event) => {
  if (pageWrap.hidden) return
  const start = { x: event.clientX, y: event.clientY, pan: { ...state.pan } }
  stage.classList.add('dragging')
  const move = (e) => {
    state.pan = { x: start.pan.x + (e.clientX - start.x), y: start.pan.y + (e.clientY - start.y) }
    applyTransform()
  }
  const up = () => {
    stage.classList.remove('dragging')
    window.removeEventListener('mousemove', move)
    window.removeEventListener('mouseup', up)
  }
  window.addEventListener('mousemove', move)
  window.addEventListener('mouseup', up)
})

stage.addEventListener('wheel', (event) => {
  if (pageWrap.hidden) return
  event.preventDefault()
  const rect = stage.getBoundingClientRect()
  const center = { x: event.clientX - rect.left, y: event.clientY - rect.top }
  if (event.ctrlKey || event.metaKey) {
    setZoom(state.zoom * (event.deltaY < 0 ? 1.1 : 1 / 1.1), center)
    return
  }
  const dx = event.shiftKey ? event.deltaY : event.deltaX
  const dy = event.shiftKey ? 0 : event.deltaY
  state.pan = { x: state.pan.x - dx, y: state.pan.y - dy }
  applyTransform()
}, { passive: false })

stage.addEventListener('dblclick', (event) => {
  const rect = stage.getBoundingClientRect()
  setZoom(state.zoom * 1.6, { x: event.clientX - rect.left, y: event.clientY - rect.top })
})

$('prev-page').onclick = () => selectPage(state.pageIndex - 1)
$('next-page').onclick = () => selectPage(state.pageIndex + 1)
$('page-select').onchange = (event) => selectPage(Number(event.target.value) - 1)
$('zoom-in').onclick = () => setZoom(state.zoom * 1.25)
$('zoom-out').onclick = () => setZoom(state.zoom / 1.25)
$('zoom-fit').onclick = fitPage
$('zoom-reset').onclick = () => setZoom(1)

window.addEventListener('keydown', (event) => {
  if (event.target.tagName === 'INPUT' || event.target.tagName === 'SELECT') return
  if (event.key === 'ArrowLeft') selectPage(state.pageIndex - 1)
  else if (event.key === 'ArrowRight') selectPage(state.pageIndex + 1)
  else if (event.key === '+' || event.key === '=') setZoom(state.zoom * 1.25)
  else if (event.key === '-') setZoom(state.zoom / 1.25)
  else if (event.key === 'f' || event.key === 'F') fitPage()
  else if (event.key === '1') setZoom(1)
})

window.addEventListener('resize', () => {
  if (!pageWrap.hidden) fitPage()
})

// ----------------------------------------------------------------- upload
function setStatus(message, isError = false) {
  const status = $('status')
  status.textContent = message
  status.classList.toggle('error', isError)
}

async function upload(file) {
  if (!file) return
  if (!/\.vsd$/i.test(file.name)) {
    setStatus('仅支持 .vsd 文件', true)
    return
  }
  setStatus(`正在渲染 ${file.name}…`)
  try {
    const response = await fetch(`/api/files?name=${encodeURIComponent(file.name)}`, {
      method: 'POST',
      headers: { 'content-type': 'application/octet-stream' },
      body: file,
    })
    const record = await response.json()
    if (!response.ok || record.status !== 'ready') {
      setStatus(`渲染失败：${record.error || record.status || response.status}`, true)
      return
    }
    setStatus(`已渲染 ${record.pageCount} 页`)
    await loadFiles(record.id)
    await selectFile(record.id)
  } catch (error) {
    setStatus(`上传失败：${error.message}`, true)
  }
}

$('file-input').addEventListener('change', (event) => upload(event.target.files?.[0]))

const dropzone = $('dropzone')
document.body.addEventListener('dragover', (event) => {
  event.preventDefault()
  dropzone.classList.add('hover')
})
document.body.addEventListener('dragleave', () => dropzone.classList.remove('hover'))
document.body.addEventListener('drop', (event) => {
  event.preventDefault()
  dropzone.classList.remove('hover')
  upload(event.dataTransfer?.files?.[0])
})

// ------------------------------------------------------------------ start
loadFiles()
