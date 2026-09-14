#include "textlayout.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace v2s
{

namespace
{

struct Item
{
  char32_t cp = 0;
  size_t span = 0;
  bool hardBreak = false;
};

const std::set<char32_t> &noLineStart()
{
  // 行首禁则：这些字符不能出现在行首
  static const std::set<char32_t> s = {
      0x3001, 0x3002, 0xff0c, 0xff0e, 0xff1a, 0xff1b, 0xff01, 0xff1f,
      0x300d, 0x300f, 0x3011, 0x3015, 0x3017, 0xff09, 0xff3d, 0xff3b,
      0x201d, 0x2019, 0x00b7, 0x2026, 0x2014, 0xff5e, 0x2025, 0x00b0,
      '%', 0x2103, 0x2109, 0x2032, 0x2033,
  };
  return s;
}

const std::set<char32_t> &noLineEnd()
{
  // 行尾禁则：这些字符不能出现在行尾
  static const std::set<char32_t> s = {
      0x300c, 0x300e, 0x3010, 0x3014, 0x3016, 0xff08, 0xff3b, 0xff5b,
      0x201c, 0x2018,
  };
  return s;
}

bool isSpaceCp(char32_t cp)
{
  return cp == ' ' || cp == '\t' || cp == 0x3000;
}

struct FlatRun
{
  std::u32string text;
  Span style;
};

} // namespace

bool isCJK(char32_t cp)
{
  if (cp >= 0x1100 && cp <= 0x11ff) return true;   // Hangul Jamo
  if (cp >= 0x2e80 && cp <= 0x303f) return true;   // CJK radicals + punctuation
  if (cp >= 0x3040 && cp <= 0x30ff) return true;   // kana
  if (cp >= 0x3100 && cp <= 0x312f) return true;
  if (cp >= 0x3400 && cp <= 0x4dbf) return true;
  if (cp >= 0x4e00 && cp <= 0x9fff) return true;
  if (cp >= 0xf900 && cp <= 0xfaff) return true;
  if (cp >= 0xff00 && cp <= 0xff60) return true;   // full width forms
  if (cp >= 0x20000 && cp <= 0x2ffff) return true;
  return false;
}

bool canBreakBefore(char32_t prev, char32_t cur)
{
  if (cur == 0 || prev == 0)
    return false;
  if (noLineStart().count(cur))
    return false;
  if (noLineEnd().count(prev))
    return false;
  if (isSpaceCp(cur))
    return true;
  if (isCJK(cur) || isCJK(prev))
    return true;
  return false;
}

bool TextBox::empty() const
{
  for (const Para &p : paras)
    for (const Span &s : p.spans)
      if (!s.text.empty())
        return false;
  return true;
}

std::u32string TextBox::plainText() const
{
  std::u32string out;
  bool first = true;
  for (const Para &p : paras)
  {
    if (!first)
      out.push_back('\n');
    first = false;
    for (const Span &s : p.spans)
      out += s.text;
  }
  return out;
}

std::u32string utf8ToU32(const std::string &s)
{
  std::u32string out;
  size_t i = 0;
  while (i < s.size())
  {
    unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp = 0;
    size_t extra = 0;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; }
    else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
    else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
    else { ++i; continue; }
    ++i;
    for (size_t k = 0; k < extra && i < s.size(); ++k, ++i)
      cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3f);
    out.push_back(cp);
  }
  return out;
}

std::string u32ToUtf8(const std::u32string &s)
{
  std::string out;
  for (char32_t cp : s)
  {
    if (cp < 0x80)
      out.push_back(static_cast<char>(cp));
    else if (cp < 0x800)
    {
      out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
    else if (cp < 0x10000)
    {
      out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
    else
    {
      out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
  }
  return out;
}

namespace
{

/** Greedy line breaking with a simplified CJK 禁则. */
std::vector<std::vector<Item> > breakParagraph(const std::vector<Item> &items, double limit,
                                              const Metrics &metrics, const std::vector<FlatRun> &runs)
{
  std::vector<std::vector<Item> > lines;
  size_t i = 0;
  const bool wrap = limit > 0.5;
  while (i < items.size())
  {
    if (items[i].hardBreak)
    {
      lines.push_back(std::vector<Item>());
      ++i;
      continue;
    }
    const size_t start = i;
    double width = 0.0;
    size_t lastBreak = start;
    size_t j = i;
    while (j < items.size() && !items[j].hardBreak)
    {
      const Item &it = items[j];
      const double adv = metrics.advance(it.cp, runs[it.span].style.family, runs[it.span].style.sizePt);
      if (wrap && width + adv > limit && j > start)
        break;
      width += adv;
      ++j;
      if (j < items.size() && !items[j].hardBreak &&
          canBreakBefore(items[j - 1].cp, items[j].cp))
        lastBreak = j;
    }
    size_t breakAt = j;
    if (wrap && j < items.size() && !items[j].hardBreak)
      breakAt = (lastBreak > start) ? lastBreak : j;

    // 禁则：如果换行点会让禁则字符落到行首，则把它拉到本行
    while (breakAt > start && breakAt < items.size() && !items[breakAt].hardBreak &&
           !canBreakBefore(items[breakAt - 1].cp, items[breakAt].cp))
      --breakAt;
    if (breakAt == start)
      breakAt = (j > start) ? j : start + 1;

    std::vector<Item> line(items.begin() + start, items.begin() + breakAt);
    lines.push_back(line);
    i = breakAt;
    // 跳过下一行开头的空格
    while (i < items.size() && !items[i].hardBreak && isSpaceCp(items[i].cp))
      ++i;
  }
  if (lines.empty())
    lines.push_back(std::vector<Item>());
  return lines;
}

} // namespace

std::vector<Line> layoutTextBox(const TextBox &box, const Metrics &metrics)
{
  std::vector<Line> result;
  if (box.empty())
    return result;

  const double usable = (box.w > 1.0) ? std::max(1.0, box.w - box.padL - box.padR) : 0.0;

  // Pass 1: create lines (per paragraph) with their runs.
  struct PLine
  {
    std::vector<LineRun> runs;
    double width = 0.0;
    double height = 0.0;
    double maxSize = 0.0;
    std::string family;
    std::string align;
    double indent = 0.0;
  };
  std::vector<PLine> plines;

  for (const Para &para : box.paras)
  {
    std::vector<FlatRun> runs;
    std::vector<Item> items;
    for (const Span &s : para.spans)
    {
      if (s.text.empty())
        continue;
      FlatRun fr;
      fr.text = s.text;
      fr.style = s;
      runs.push_back(fr);
      const size_t idx = runs.size() - 1;
      for (char32_t cp : s.text)
      {
        Item it;
        it.cp = cp;
        it.span = idx;
        it.hardBreak = (cp == '\n');
        items.push_back(it);
      }
    }
    const double limit = usable > 0.0 ? std::max(1.0, usable - para.indent) : 0.0;
    const std::vector<std::vector<Item> > lines = breakParagraph(items, limit, metrics, runs);
    for (const std::vector<Item> &lineItems : lines)
    {
      PLine pl;
      pl.align = para.align;
      pl.indent = para.indent;
      double maxSize = 0.0;
      for (const Item &it : lineItems)
        maxSize = std::max(maxSize, runs[it.span].style.sizePt);
      if (maxSize <= 0.0)
        maxSize = 10.0;
      pl.maxSize = maxSize;
      pl.height = maxSize * std::max(1.0, para.lineHeightPct / 100.0);

      // group consecutive items that share the same span
      size_t k = 0;
      while (k < lineItems.size())
      {
        const size_t spanIdx = lineItems[k].span;
        std::u32string text;
        while (k < lineItems.size() && lineItems[k].span == spanIdx)
        {
          text.push_back(lineItems[k].cp);
          ++k;
        }
        LineRun run;
        run.text = text;
        run.style = runs[spanIdx].style;
        run.width = metrics.width(text, run.style.family, run.style.sizePt);
        pl.width += run.width;
        pl.runs.push_back(run);
        if (pl.family.empty())
          pl.family = run.style.family;
      }
      plines.push_back(pl);
    }
    if (para.spans.empty())
    {
      PLine pl;
      pl.height = 12.0;
      pl.maxSize = 12.0;
      pl.align = para.align;
      plines.push_back(pl);
    }
  }

  if (plines.empty())
    return result;

  double total = 0.0;
  for (const PLine &pl : plines)
    total += pl.height;

  double y = box.y + box.padT;
  if (box.valign == "middle")
    y = box.y + (box.h - total) / 2.0;
  else if (box.valign == "bottom")
    y = box.y + box.h - total - box.padB;

  for (PLine &pl : plines)
  {
    double x = box.x + box.padL + pl.indent;
    if (usable > 0.0)
    {
      if (pl.align == "center")
        x = box.x + box.padL + std::max(0.0, (usable - pl.width) / 2.0);
      else if (pl.align == "right" || pl.align == "end")
        x = box.x + box.padL + std::max(0.0, usable - pl.width);
    }
    const FontConf &conf = metrics.conf(pl.family.empty() ? std::string() : pl.family);
    Line line;
    line.width = pl.width;
    line.height = pl.height;
    line.baseline = y + conf.ascent * pl.maxSize;
    double cx = x;
    for (LineRun run : pl.runs)
    {
      run.x = cx;
      cx += run.width;
      line.runs.push_back(run);
    }
    result.push_back(line);
    y += pl.height;
  }
  return result;
}

} // namespace v2s
