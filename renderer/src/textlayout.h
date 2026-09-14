/* Text layout for the VSD->SVG output backend.
 *
 * Visio stores the *inputs* of text layout (text block box, padding, vertical
 * alignment, per paragraph alignment, line height, character runs) but not the
 * result, so the writer has to lay the text out itself:
 *   - hard breaks come from the paragraphs libvisio reports,
 *   - soft breaks (Visio's automatic wrapping) are computed here from the
 *     font metrics,
 *   - horizontal and vertical alignment are applied here.
 * Every produced line carries absolute coordinates, and the SVG writer emits
 * them as explicit <tspan x= y=> so no re-layout can happen in the browser.
 */
#ifndef V2S_TEXTLAYOUT_H
#define V2S_TEXTLAYOUT_H

#include "metrics.h"

#include <string>
#include <vector>

namespace v2s
{

struct Span
{
  std::string family;
  std::string color = "#000000";
  double sizePt = 10.0;
  bool bold = false;
  bool italic = false;
  double opacity = 1.0;
  std::u32string text;
  bool superscript = false;
  bool subscript = false;
};

struct Para
{
  std::string align = "left"; // left | center | right | justify
  double lineHeightPct = 100.0;
  double marginLeft = 0.0;
  double marginRight = 0.0;
  double indent = 0.0;
  std::string bullet;         // optional bullet prefix
  std::vector<Span> spans;
};

struct TextBox
{
  double x = 0.0, y = 0.0;  // top-left of the text block (pt)
  double w = 0.0, h = 0.0;  // size of the text block (pt), 0 = unknown
  std::string valign = "top";
  double rotate = 0.0;
  double padL = 0.0, padR = 0.0, padT = 0.0, padB = 0.0;
  std::vector<Para> paras;
  bool empty() const;
  std::u32string plainText() const;
};

struct LineRun
{
  std::u32string text;
  Span style;
  double x = 0.0;      // absolute pt
  double width = 0.0;
};

struct Line
{
  std::vector<LineRun> runs;
  double baseline = 0.0; // absolute pt
  double width = 0.0;
  double height = 0.0;
};

std::vector<Line> layoutTextBox(const TextBox &box, const Metrics &metrics);

std::u32string utf8ToU32(const std::string &s);
std::string u32ToUtf8(const std::u32string &s);
bool isCJK(char32_t cp);
bool canBreakBefore(char32_t prev, char32_t cur);

} // namespace v2s

#endif
