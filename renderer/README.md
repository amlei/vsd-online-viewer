# vsd2svg —— VSD → SVG 渲染器

把 `.vsd` 渲染成前端可用的 SVG。它替换了原先 `vsd2xhtml`（libvisio + librevenge
的通用 SVG 后端）那条链路，因为那条链路在这些图纸上会系统性地丢内容。

## 为什么要自己写渲染器

逐层定位后确认的问题、责任层与处理方式：

| 现象 | 责任层 | 处理方式 |
|---|---|---|
| 整张图细线/标尺/符号消失 | libvisio 把 LineWeight 乘了图纸比例（`m_scale`），librevenge 又把 0 线宽写成 `stroke-width:0`（SVG 语义=不画） | `patches/0001-*` 修 libvisio；writer 里把 hairline 画成 1 设备像素 |
| 标题栏文字左移/重叠 | librevenge 的 SVG 后端忽略 `fo:text-align`（`openParagraph` 是空实现） | writer 自己排版 |
| 硬换行丢失（图签里两行文字被挤成一行） | 同上，段落信息在 SVG 里没有载体 | writer 按段落输出 `<tspan x y>` |
| 自动折行错（文本框内本应折行的标签变成一行） | 文件里没有折行结果（Visio 渲染时才算） | writer 按字体度量做 CJK 折行 |
| 斜线/图案填充丢失 | librevenge 的 SVG 后端没有 `hatch` 分支 | writer 生成 `<pattern>` |
| 箭头形状是通用近似 | librevenge 用固定的 polyline 当所有箭头 | writer 使用 libvisio 给出的真实箭头路径/视口/尺寸 |

libvisio 本身是"导入过滤器"，不是 Visio 兼容渲染器；它的公开 API 只是
`VisioDocument::parse(input, painter)`，`painter` 是实现
`librevenge::RVNGDrawingInterface` 的任意对象（`vsd2raw` 用的是 librevenge 自带的
raw 后端）。我们用同一套 API，换自己的后端，因此 **只需要 fork libvisio 一个仓库**，
librevenge 只作为头文件/工具库链接，不修改。

## 结构

```
renderer/
  build.sh                     下载 libvisio、打补丁、编译，产出 build/vsd2svg
  patches/0001-*.patch         libvisio 补丁（线宽/箭头按纸张单位处理）
  fonts.conf                   字体替换 + 度量（折行、基线依赖它）
  src/vsd2svg.cpp              CLI：解析参数、驱动 libvisio、写页面与 sidecar
  src/svgwriter.{h,cpp}        我们的 SVG 后端（RVNGDrawingInterface 实现）
  src/textlayout.{h,cpp}       文本排版：折行、水平/垂直对齐、行高
  src/metrics.{h,cpp}          字体度量（半角/全角宽度、ascent）
  src/props.h                  librevenge 属性读取（英寸/点/百分比换算）
  tools/census.py              未处理 chunk 普查（把"未知问题"变成清单）
```

## 构建

依赖：`brew install librevenge boost icu4c libxml2`（或 Linux 上等价的
pkg-config 包）、`curl`、`patch`、C++17 编译器。

```sh
./renderer/build.sh            # 结果：renderer/build/vsd2svg
./renderer/build.sh --clean    # 重新下载并编译
```

产物布局（`build/` 已在 .gitignore 中）：
`build/libvisio-<ver>/`（打补丁的上游源码）、`build/vsd2svg`（可执行文件）。

## 使用

```sh
renderer/build/vsd2svg --outdir /tmp/out --metrics renderer/fonts.conf 图纸.vsd
```

输出：

| 文件 | 内容 |
|---|---|
| `pages/page-NN.svg` | 每页一个自包含 SVG（无外部字体/图片引用） |
| `pages/page-NN.json` | 该页文本清单：text / x / y / fontSize / fontFamily / color（坐标单位为 pt） |
| `meta.json` | 页序、页名、页尺寸（pt）、文件清单 |

stdout 输出一行 JSON 摘要，便于被服务端调用方解析。

## 排版与字体

Visio 只存"排版输入"（文本框、内边距、垂直对齐、段落对齐、行高、字符 run），
折行位置要靠字体度量算。因此：

* `font-family` 由 `fonts.conf` 映射到本机可用字体（含 CJK 回退，模拟 Windows 的
  font linking：西文字体缺字时用宋体回退）；
* 折行、居中都使用 `fonts.conf` 里的宽度/上升值；
* 渲染机必须安装相应字体，否则度量不同、换行点就会不同。生产环境建议把字体随镜像
  一起固化。

`fonts.conf` 每行格式（Tab 分隔）：

```
<Visio 字体名> <半角em> <其他em> <上升em> <行距em> <CSS font stack>
```

## 集成方式

渲染器是一个无状态 CLI：一次调用 = 一份图纸。宿主程序只需要

1. 调 `renderer/build/vsd2svg --outdir <dir> --metrics renderer/fonts.conf <file.vsd>`；
2. 读 `<dir>/meta.json` 拿页序/页名/页尺寸（pt），读 `<dir>/pages/page-NN.json`
   拿该页文字与坐标；
3. 把 `<dir>/pages/page-NN.svg` 当作自包含图片提供出去（浏览器 `<img>` 或
   `background-image` 均可，SVG 不引用外部资源）。

可用 `VSD2SVG_BIN`、`VSD2SVG_FONTS` 环境变量覆盖默认路径。
本仓库的 `server/` 就是这样一个宿主：上传 → 渲染 → 提供页面与文字接口。

## 诊断工具

```sh
# 未处理 chunk 普查：这些 chunk 目前被 libvisio 静默忽略
python3 renderer/tools/census.py [--json] 图纸.vsd

# 与 Visio 截图做逐页回归（ink 覆盖率 / 多余墨迹 / 平均差 + overlay 图）
python3 test/vsd-render/regression.py --pages /tmp/out/pages --out /tmp/reg \
    --ref 1=/path/to/page-01-reference.png
```

`census.py` 自带 CFB/OLE 读取（有 `olefile` 时优先用它，没有也能跑）；
`regression.py` 需要 Pillow，并用本机 Chrome 渲染（`CHROME_BIN` 可覆盖）。

回归用的参考图必须是**纯页面图**（例如 Visio 截图后裁到页面边缘）。如果参考图里带
浏览器/观看器界面（工具栏、页签），自动对齐会失败，此时用 `--scale` / `--ox` / `--oy`
显式给出映射；`--scale` 是参考图每 pt 的像素数。

## 当前已知限制

1. **AutoFit / 自动缩小文字**：libvisio 没有解析该标志（`VSDMisc` 只有 `HideText`），
   使用该特性的形状文字大小会与 Visio 不一致。census + 回归可发现这类页面。
2. **字体**：见上，渲染机缺字体就会漂移。
3. **渐变**：目前退化为起始色（`draw:fill: gradient`），本项目的图纸未使用。
4. **未覆盖 chunk**：抽样图纸里有 1.90%～8.48% 的 chunk 未被 libvisio 分发，主要是
   EVENT / CONNECTION_POINTS / USER_DEFINED_CELLS / CUSTOM_PROPS / PRINT_PROPS 等
   非绘制类；如果后续发现视觉影响，按 `census.py` 的清单逐类补。
5. **箭头类型**：libvisio 源码里 marker 7/26/31–45 仍标着 TODO；writer 会画出
   libvisio 给出的路径，未实现的类型仍是近似。

## 升级 libvisio

`build.sh` 用 `LIBVISIO_VERSION` 指定上游版本（默认 `0.1.11`）。升版本时：

```sh
LIBVISIO_VERSION=0.1.12 ./renderer/build.sh --clean
```

补丁若失效需要按新的 `VSDContentCollector.cpp` 位置重做；回归脚本可用来确认升级
没有引入渲染回退。
