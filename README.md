# VSD Online Viewer

**自托管的 Microsoft Visio 图纸（`.vsd`）在线查看器。** 上传一份 `.vsd`，服务端用
[libvisio](https://wiki.documentfoundation.org/DLP/Libraries/libvisio) 解析，再由本项目
自研的 SVG 渲染器逐页渲染成自包含 SVG，浏览器里翻页、缩放、平移查看，并可下载任意页。

不需要安装 Visio，不需要 Windows，也不依赖任何第三方在线转换服务——图纸只落在你自己的服务器上。

> 关键词：Visio viewer · VSD viewer · `.vsd` 在线预览 · Visio 图纸浏览器 · libvisio ·
> SVG 渲染 · self-hosted · Node.js · 内网离线部署

![VSD Online Viewer 界面](https://raw.githubusercontent.com/amlei/vsd-online-viewer/main/assets/preview-empty.png)

项目主页：<https://amlei.github.io/vsd-online-viewer/>

## 这是什么

一个"打开就能用"的图纸查看服务，由三部分组成：

1. **渲染器**（`renderer/`）：libvisio 的补丁版 fork + 自研 SVG 输出层，把 `.vsd` 逐页转成
   自包含 SVG（不引用外部字体或图片）；
2. **服务**（`server/`）：零运行时依赖的 `node:http` 服务，负责上传、落盘、调用渲染器、提供页面与文字接口；
3. **界面**（`public/`）：原生 ES module 单页应用，无构建步骤。

## 不是什么

- 不是 Visio 编辑器：只读查看，不能修改图纸。
- 不是通用文档转换器：渲染器的修正针对工程图纸（Visio hairline、文本排版、箭头、填充），
  目标是"看起来和 Visio 一致"，不是格式互转。
- 不是 SaaS：没有账号、没有云端转换，数据不出你的机器。

## 关键特性

| 特性 | 说明 |
|---|---|
| 逐页自包含 SVG | 每页一个 SVG 文件，无外部依赖，可直接下载、打印或嵌入其他系统 |
| hairline 正确渲染 | Visio 的 0 线宽按 1 设备像素绘制；通用转换器会把这类线条整条丢弃 |
| 文本按 Visio 排版 | 段落硬换行、水平/垂直对齐、按文本框宽度自动折行（CJK）都在服务端算好，输出逐行 `<tspan x y>` |
| 真实箭头 | 使用 libvisio 给出的箭头路径/视口/尺寸，而非通用近似形状 |
| 图案填充 | Visio 的斜线/图案填充输出为 SVG `<pattern>` |
| 图纸库与去重 | 相同内容（sha256）自动复用，不重复渲染 |
| 无依赖服务端 | 只用 Node 内置模块；上传走原始请求体，无需 multipart 解析 |
| 覆盖率体检（可选） | `renderer/tools/census.py` 统计解析器尚未处理的 chunk，量化"未知问题"规模 |

## 快速开始

### 环境要求

- Node.js ≥ 20（开发验证于 Node 25）
- 构建渲染器需要：`librevenge`、`boost`、`icu4c`、`libxml2`、`curl`、`patch`、C++17 编译器
  （macOS：`brew install librevenge boost icu4c libxml2`）
- 可选：`python3`（仅"覆盖率体检"用，脚本自带 CFB 读取，无需额外 pip 包）

### 安装与运行

```sh
# 1. 构建渲染器（首次会下载 libvisio 源码并打补丁）
npm run build:renderer          # 等价于 ./renderer/build.sh

# 2. 启动
npm start                       # http://localhost:4310
```

### 环境变量

| 变量 | 默认值 | 说明 |
|---|---|---|
| `PORT` | `4310` | 监听端口 |
| `VSD_VIEWER_DATA` | `./data` | 数据目录（每份图纸一个子目录） |
| `VSD_VIEWER_MAX_UPLOAD` | `209715200`（200MB） | 上传大小上限 |
| `VSD2SVG_BIN` | `renderer/build/vsd2svg` | 渲染器可执行文件路径 |
| `VSD2SVG_FONTS` | `renderer/fonts.conf` | 字体替换与度量配置 |
| `VSD_VIEWER_PYTHON` | `python3` | 覆盖率体检使用的解释器 |

## 界面

| 区域 | 功能 |
|---|---|
| 左栏 | 图纸库：文件名、页数、上传时间；点击切换。拖拽 `.vsd` 到窗口或点「打开 VSD」上传 |
| 主区 | 翻页、缩放（适应窗口 / 1:1 / ±）、拖拽平移、`⌘/Ctrl+滚轮` 以光标为中心缩放、双击放大、下载当前页 SVG |

快捷键：`←` `→` 翻页，`+` `-` 缩放，`F` 适应窗口，`1` 恢复 1:1。

## API

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/api/files` | 图纸库列表 |
| `POST` | `/api/files?name=x.vsd` | 上传并渲染；请求体即文件本身（`application/octet-stream`） |
| `GET` | `/api/files/:id` | 文件详情与页索引（含页尺寸 pt） |
| `GET` | `/api/files/:id/pages/:page.json` | 该页文字与坐标：`text/x/y/fontSize/fontFamily/color`（单位 pt） |
| `GET` | `/api/files/:id/pages/page-NN.svg` | 页面 SVG（`?download=1` 触发下载） |
| `GET` | `/api/files/:id/census` | chunk 覆盖率报告 |
| `DELETE` | `/api/files/:id` | 删除图纸及其渲染产物 |

上传示例（浏览器端，无需 multipart）：

```js
await fetch(`/api/files?name=${encodeURIComponent(file.name)}`, { method: 'POST', body: file })
```

## 工作原理

```
.vsd ──► libvisio（fork，打了 1 个补丁） ──► 自研 SVG 输出层 ──► pages/page-NN.svg
                                                  │
                                                  └─► pages/page-NN.json（文字 + 坐标）
```

libvisio 的公开 API 是 `VisioDocument::parse(input, painter)`，`painter` 是实现
`librevenge::RVNGDrawingInterface` 的任意对象。本项目用同一套 API，但**替换掉了通用的
librevenge SVG 后端**——因为通用后端在这些图纸上有系统性缺陷：

| 现象 | 原因 | 本项目做法 |
|---|---|---|
| 细线、里程标尺、符号整片消失 | line width 被按图纸比例缩小，且 0 线宽写成 `stroke-width:0`（SVG 语义=不画） | 补丁修正线宽单位，hairline 用 1px `non-scaling-stroke` |
| 图签文字错位/重叠 | SVG 后端忽略 `fo:text-align` | 自己排版，输出 `text-anchor` 与逐行坐标 |
| 两行文字被挤成一行 | 段落信息在 SVG 里没有载体 | 按段落输出 `<tspan x y>` |
| 文本框内该折行的标签不折行 | 折行结果不在文件里（Visio 渲染时才算） | 用字体度量计算 CJK 折行 |
| 斜线填充丢失 | SVG 后端没有 `hatch` 分支 | 生成 `<pattern>` |
| 箭头是通用近似 | 后端用固定 polyline 代替所有箭头 | 使用 libvisio 提供的真实箭头路径 |

细节与源码结构见 [renderer/README.md](https://github.com/amlei/vsd-online-viewer/blob/main/renderer/README.md)。

## 常见问题

**需要安装 Visio、Windows 或 Office 365 吗？**
不需要。解析用 libvisio，渲染是本项目自己的代码，纯跨平台。

**图纸会被上传到第三方服务吗？**
不会。文件写入 `data/<id>/`，渲染在本地进程完成，界面不请求任何外部服务，可内网离线部署。

**支持 `.vsdx` / `.vdx` 吗？**
渲染器底层的 libvisio 具备二进制 `.vsd`、OPC（`.vsdx`）与 XML（`.vdx`）三条解析分支，
但查看器目前的扩展名校验只放行 `.vsd`。要尝试 `.vsdx`，放开 `server/server.mjs` 里的校验即可
（本项目未对 `.vsdx` 做完整验证）。

**为什么渲染结果和 Visio 不是像素级一致？**
Visio 的排版（自动折行、AutoFit）与字体度量是渲染时计算的，文件里并不存储。本项目复刻了其中
大部分规则，剩余差异见下方「已知限制」。

**中文图纸显示正常吗？字体怎么处理？**
正常。`renderer/fonts.conf` 把 Visio 字体名（如 `宋体`、`仿宋_GB2312`）映射到系统字体，并为
西文字体补 CJK 回退（模拟 Windows 的 font linking）。**渲染机上必须存在相应字体**，否则换行点
会漂移；生产环境建议把字体固化进镜像。

**一份图纸最多多少页？**
没有页数上限，取决于内存与磁盘；实测 26～28 页的工程图纸单份渲染耗时不到 1 秒。

**能只用渲染器、不用界面吗？**
可以。渲染器是独立 CLI：`renderer/build/vsd2svg --outdir DIR --metrics renderer/fonts.conf 图纸.vsd`。

## 测试

```sh
# 集成测试（起真实服务 + 上传真实图纸；不设样本时渲染用例自动跳过）
VSD_SAMPLE=/path/to/drawing.vsd VSD_SAMPLE_HAIRLINE=/path/to/hairline.vsd npm test

# 解析覆盖率（哪些 chunk 尚未被解析器处理）
python3 renderer/tools/census.py --json 图纸.vsd

# 与 Visio 截图做逐页回归（ink 覆盖率 / 多余墨迹 / 平均差 + overlay 图）
python3 test/vsd-render/regression.py --pages /tmp/out/pages --out /tmp/reg \
    --ref 1=/path/to/page-01-reference.png
```

`npm test` 覆盖：图纸库读写、非 `.vsd` 与空文件拒绝、静态页面、逐页渲染、页索引、
文字坐标与字号，以及两条关键回归断言——**任何页面都不得出现不可见的 `stroke-width:0`**、
**hairline 图纸必须渲染出 `vector-effect:non-scaling-stroke`**。

界面冒烟测试可用 [playwright-cli](https://github.com/microsoft/playwright-cli)：

```sh
playwright-cli open http://localhost:4310
playwright-cli upload drawing.vsd
playwright-cli screenshot
```

## 已知限制

1. **AutoFit / 文字自动缩小**：libvisio 未解析该标志，使用该特性的形状文字大小会与 Visio 有差异。
2. **字体依赖**：渲染机缺字体时换行与位置会漂移（见常见问题）。
3. **部分 chunk 未处理**：抽样图纸中约 1.90%～8.48% 的 chunk 未被 libvisio 分发，主要为
   EVENT、CONNECTION_POINTS、USER_DEFINED_CELLS、CUSTOM_PROPS 等非绘制类型；可用
   `census.py` 列出清单后按需补齐。
4. **渐变填充**：目前退化为起始色（`draw:fill: gradient`）。
5. **只读**：不支持编辑或写回 `.vsd`。

## 目录结构

```
renderer/   渲染器：libvisio 补丁、自研 SVG 输出层、排版与字体度量、census 工具
server/     HTTP 服务与存储（node:http，零运行时依赖）
public/     查看器前端（原生 ES module，无构建步骤）
test/       node:test 集成测试、逐页回归脚本
assets/     README 用图
data/       运行期数据（每份图纸一个目录：source.vsd + pages/ + meta.json）
```

## 许可与致谢

- 渲染器 fork 自 [libvisio](https://wiki.documentfoundation.org/DLP/Libraries/libvisio)（MPL-2.0），
  补丁见 `renderer/patches/`；SVG 生成依赖 [librevenge](https://sourceforge.net/p/libwpd/wiki/librevenge/)（LGPL-2.1+/MPL-2.0，仅链接不修改）。
- 本仓库自身的 LICENSE 尚未添加，发布前需确定许可。

---

重新生成 README 截图：启动服务后 `playwright-cli open http://localhost:4310 && playwright-cli screenshot --filename=assets/preview-empty.png`。
