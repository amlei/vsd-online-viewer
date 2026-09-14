#include "svgwriter.h"

#include "props.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace v2s
{

namespace
{

/** Below this (in pt) a line weight is Visio's "hairline": drawn as a single
 *  device pixel regardless of zoom.  SVG's stroke-width:0 paints nothing at
 *  all, which is what made whole drawings disappear. */
const double kHairlinePt = 0.02;

double num(const librevenge::RVNGPropertyList &list, const char *key, double fallback = 0.0)
{
  const librevenge::RVNGProperty *p = list[key];
  return p ? p->getDouble() : fallback;
}

std::string fmt(double v, int maxDecimals = 4)
{
  if (std::fabs(v) < 1e-9)
    return "0";
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", maxDecimals, v);
  std::string s(buf);
  if (s.find('.') != std::string::npos)
  {
    while (!s.empty() && s[s.size() - 1] == '0')
      s.erase(s.size() - 1);
    if (!s.empty() && s[s.size() - 1] == '.')
      s.erase(s.size() - 1);
  }
  return s.empty() ? "0" : s;
}

std::string xmlEscape(const std::string &in)
{
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i)
  {
    const char c = in[i];
    switch (c)
    {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    case '\'': out += "&apos;"; break;
    default: out.push_back(c); break;
    }
  }
  return out;
}

std::vector<double> parseNumbers(const std::string &s)
{
  std::vector<double> out;
  const char *p = s.c_str();
  while (*p)
  {
    while (*p && !(*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9')))
      ++p;
    if (!*p)
      break;
    char *end = nullptr;
    const double v = std::strtod(p, &end);
    if (end == p)
      break;
    out.push_back(v);
    p = end;
  }
  return out;
}

bool isSigned(const std::string &s, const char *word)
{
  return s.find(word) != std::string::npos;
}

} // namespace

SvgWriter::SvgWriter(const Metrics &metrics) : m_metrics(metrics) {}

std::string SvgWriter::pathData(const librevenge::RVNGPropertyListVector &path)
{
  std::string d;
  for (unsigned long i = 0; i < path.count(); ++i)
  {
    const librevenge::RVNGPropertyList p(path[i]);
    const std::string action = str(p, "librevenge:path-action");
    if (action.size() != 1)
      continue;
    const char a = action[0];
    const bool ok = exists(p, "svg:x") && exists(p, "svg:y");
    const bool ok1 = ok && exists(p, "svg:x1") && exists(p, "svg:y1");
    const bool ok2 = ok1 && exists(p, "svg:x2") && exists(p, "svg:y2");
    if (a == 'M' || a == 'L' || a == 'T')
    {
      if (!ok) continue;
      d += std::string("\n") + a + fmt(pt(p, "svg:x")) + "," + fmt(pt(p, "svg:y"));
    }
    else if (a == 'Q' || a == 'S')
    {
      if (!ok1) continue;
      d += std::string("\n") + a + fmt(pt(p, "svg:x1")) + "," + fmt(pt(p, "svg:y1")) + " " +
           fmt(pt(p, "svg:x")) + "," + fmt(pt(p, "svg:y"));
    }
    else if (a == 'C')
    {
      if (!ok2) continue;
      d += std::string("\nC") + fmt(pt(p, "svg:x1")) + "," + fmt(pt(p, "svg:y1")) + " " +
           fmt(pt(p, "svg:x2")) + "," + fmt(pt(p, "svg:y2")) + " " +
           fmt(pt(p, "svg:x")) + "," + fmt(pt(p, "svg:y"));
    }
    else if (a == 'A')
    {
      if (!ok || !exists(p, "svg:rx") || !exists(p, "svg:ry"))
        continue;
      d += "\nA" + fmt(pt(p, "svg:rx")) + "," + fmt(pt(p, "svg:ry")) + " " +
           fmt(num(p, "librevenge:rotate")) + " " +
           (exists(p, "librevenge:large-arc") ? std::to_string(p["librevenge:large-arc"]->getInt()) : std::string("1")) + "," +
           (exists(p, "librevenge:sweep") ? std::to_string(p["librevenge:sweep"]->getInt()) : std::string("1")) + " " +
           fmt(pt(p, "svg:x")) + "," + fmt(pt(p, "svg:y"));
    }
    else if (a == 'H')
    {
      if (!exists(p, "svg:x")) continue;
      d += std::string("\nH") + fmt(pt(p, "svg:x"));
    }
    else if (a == 'V')
    {
      if (!exists(p, "svg:y")) continue;
      d += std::string("\nV") + fmt(pt(p, "svg:y"));
    }
    else if (a == 'Z')
      d += "\nZ";
  }
  return d;
}

std::string SvgWriter::markerDef(const std::string &path, const std::string &viewBox, double widthPt, bool start)
{
  std::vector<double> vb = parseNumbers(viewBox);
  if (vb.size() != 4 || vb[2] <= 0.0 || vb[3] <= 0.0)
  {
    vb.clear();
    vb.push_back(0); vb.push_back(0); vb.push_back(20); vb.push_back(30);
  }
  const double vx = vb[0], vy = vb[1], vw = vb[2], vh = vb[3];
  // rotate(90) maps (x,y) -> (-y,x), so the marker box becomes
  // x in [-(vy+vh), -vy], y in [vx, vx+vw]
  const double nx = -(vy + vh), ny = vx, nw = vh, nh = vw;
  const double cx = nx + nw / 2.0, cy = ny + nh / 2.0;
  const double refX = start ? nx : (nx + nw);
  const double refY = cy;
  const std::string fill = str(m_style, "svg:stroke-color", "#000000");

  const std::string id = std::string(start ? "ms" : "me") + std::to_string(m_markerCount++);
  const double length = std::max(0.5, widthPt);
  const double thickness = length * (nh / nw);

  m_defs += "<marker id=\"" + id + "\" markerUnits=\"userSpaceOnUse\" orient=\"auto\"";
  m_defs += " markerWidth=\"" + fmt(length) + "\" markerHeight=\"" + fmt(thickness) + "\"";
  m_defs += " viewBox=\"" + fmt(nx) + " " + fmt(ny) + " " + fmt(nw) + " " + fmt(nh) + "\"";
  m_defs += " refX=\"" + fmt(refX) + "\" refY=\"" + fmt(refY) + "\">";
  m_defs += "<g transform=\"rotate(90)\">";
  if (start)
    m_defs += "<g transform=\"rotate(180," + fmt(cx) + "," + fmt(cy) + ")\">";
  m_defs += "<path d=\"" + path + "\" fill=\"" + fill + "\" stroke=\"none\"/>";
  if (start)
    m_defs += "</g>";
  m_defs += "</g></marker>\n";
  return id;
}

std::string SvgWriter::hatchDef(const librevenge::RVNGPropertyList &s)
{
  const std::string key = str(s, "draw:color") + "|" + str(s, "draw:style") + "|" +
                          fmt(num(s, "draw:distance")) + "|" + fmt(num(s, "draw:rotation")) + "|" +
                          str(s, "draw:fill-color");
  static std::map<std::string, std::string> cache;
  std::map<std::string, std::string>::const_iterator it = cache.find(key);
  if (it != cache.end())
    return it->second;

  const std::string id = "h" + std::to_string(m_hatchCount++);
  double distance = 0.05; // inches, fallback
  if (exists(s, "draw:distance"))
    distance = isRelative(s, "draw:distance") ? num(s, "draw:distance") * inch(s, "svg:stroke-width", 0.05)
                                              : inch(s, "draw:distance", 0.05);
  const double d = std::max(0.5, distance * 72.0);
  const std::string style = str(s, "draw:style", "single");
  const int lines = (style == "triple") ? 3 : (style == "double" ? 2 : 1);
  const double rotation = num(s, "draw:rotation");
  const std::string color = str(s, "draw:color", "#000000");

  m_defs += "<pattern id=\"" + id + "\" patternUnits=\"userSpaceOnUse\" width=\"" + fmt(d) +
            "\" height=\"" + fmt(d) + "\" patternTransform=\"rotate(" + fmt(rotation) + ")\">";
  if (flag(s, "draw:fill-hatch-solid") && exists(s, "draw:fill-color"))
    m_defs += "<rect width=\"" + fmt(d) + "\" height=\"" + fmt(d) + "\" fill=\"" +
              str(s, "draw:fill-color", "#ffffff") + "\"/>";
  for (int i = 0; i < lines; ++i)
  {
    const double y = d * (i + 1.0) / (lines + 1.0);
    m_defs += "<line x1=\"0\" y1=\"" + fmt(y) + "\" x2=\"" + fmt(d) + "\" y2=\"" + fmt(y) +
              "\" stroke=\"" + color + "\" stroke-width=\"" + fmt(std::max(0.4, d * 0.16)) + "\"/>";
  }
  m_defs += "</pattern>\n";
  cache[key] = id;
  return id;
}

std::string SvgWriter::styleAttr(bool /*closed*/)
{
  const librevenge::RVNGPropertyList &s = m_style;
  std::string out;

  const std::string stroke = str(s, "draw:stroke");
  const bool noStroke = (stroke == "none") || (stroke.empty() && !exists(s, "svg:stroke-width"));
  if (!noStroke)
  {
    const double widthPt = inch(s, "svg:stroke-width", 0.0) * 72.0;
    if (widthPt <= kHairlinePt)
    {
      // Visio hairline: one device pixel, independent of zoom
      out += "stroke-width:1;vector-effect:non-scaling-stroke;";
    }
    else
      out += "stroke-width:" + fmt(widthPt) + ";";
    out += "stroke:" + str(s, "svg:stroke-color", "#000000") + ";";
    const double so = fraction(s, "svg:stroke-opacity", 1.0);
    if (so < 0.999)
      out += "stroke-opacity:" + fmt(so) + ";";
    if (stroke == "dash")
    {
      const int dots1 = exists(s, "draw:dots1") ? s["draw:dots1"]->getInt() : 0;
      const int dots2 = exists(s, "draw:dots2") ? s["draw:dots2"]->getInt() : 0;
      const double w = inch(s, "svg:stroke-width", 0.01);
      double dots1len = 72.0 * w, dots2len = 72.0 * w, gap = 72.0 * w;
      if (exists(s, "draw:dots1-length"))
        dots1len = isRelative(s, "draw:dots1-length") ? 72.0 * num(s, "draw:dots1-length") * w
                                                      : 72.0 * inch(s, "draw:dots1-length", w);
      if (exists(s, "draw:dots2-length"))
        dots2len = isRelative(s, "draw:dots2-length") ? 72.0 * num(s, "draw:dots2-length") * w
                                                      : 72.0 * inch(s, "draw:dots2-length", w);
      if (exists(s, "draw:distance"))
        gap = isRelative(s, "draw:distance") ? 72.0 * num(s, "draw:distance") * w
                                             : 72.0 * inch(s, "draw:distance", w);
      std::string dash;
      for (int i = 0; i < dots1; ++i)
        dash += (dash.empty() ? "" : ",") + fmt(dots1len) + "," + fmt(gap);
      for (int j = 0; j < dots2; ++j)
        dash += (dash.empty() ? "" : ",") + fmt(dots2len) + "," + fmt(gap);
      if (!dash.empty())
        out += "stroke-dasharray:" + dash + ";";
    }
    out += "stroke-linecap:" + str(s, "svg:stroke-linecap", "butt") + ";";
    out += "stroke-linejoin:" + str(s, "svg:stroke-linejoin", "miter") + ";";
    if (exists(s, "draw:marker-start-path"))
    {
      const std::string id = markerDef(str(s, "draw:marker-start-path"),
                                       str(s, "draw:marker-start-viewbox", "0 0 20 30"),
                                       inch(s, "draw:marker-start-width", 0.1) * 72.0, true);
      out += "marker-start:url(#" + id + ");";
    }
    if (exists(s, "draw:marker-end-path"))
    {
      const std::string id = markerDef(str(s, "draw:marker-end-path"),
                                       str(s, "draw:marker-end-viewbox", "0 0 20 30"),
                                       inch(s, "draw:marker-end-width", 0.1) * 72.0, false);
      out += "marker-end:url(#" + id + ");";
    }
  }
  else
    out += "stroke:none;";

  const std::string fill = str(s, "draw:fill");
  if (fill == "none" || fill.empty())
    out += "fill:none;";
  else if (fill == "solid")
    out += "fill:" + str(s, "draw:fill-color", "#000000") + ";";
  else if (fill == "hatch")
    out += "fill:url(#" + hatchDef(s) + ");";
  else if (fill == "gradient")
    out += "fill:" + str(s, "draw:start-color", "#000000") + ";"; // gradient: solid fallback
  else
    out += "fill:none;";
  if (exists(s, "svg:fill-rule"))
    out += "fill-rule:" + str(s, "svg:fill-rule") + ";";
  const double opacity = num(s, "draw:opacity", 1.0);
  if (opacity < 0.999)
    out += "fill-opacity:" + fmt(opacity) + ";";
  return out;
}

void SvgWriter::startDocument(const librevenge::RVNGPropertyList &) {}
void SvgWriter::endDocument() {}
void SvgWriter::setDocumentMetaData(const librevenge::RVNGPropertyList &) {}
void SvgWriter::defineEmbeddedFont(const librevenge::RVNGPropertyList &) {}
void SvgWriter::startMasterPage(const librevenge::RVNGPropertyList &) {}
void SvgWriter::endMasterPage() {}
void SvgWriter::startEmbeddedGraphics(const librevenge::RVNGPropertyList &) {}
void SvgWriter::endEmbeddedGraphics() {}
void SvgWriter::startTableObject(const librevenge::RVNGPropertyList &) {}
void SvgWriter::openTableRow(const librevenge::RVNGPropertyList &) {}
void SvgWriter::closeTableRow() {}
void SvgWriter::openTableCell(const librevenge::RVNGPropertyList &) {}
void SvgWriter::closeTableCell() {}
void SvgWriter::insertCoveredTableCell(const librevenge::RVNGPropertyList &) {}
void SvgWriter::endTableObject() {}
void SvgWriter::insertField(const librevenge::RVNGPropertyList &) {}
void SvgWriter::openOrderedListLevel(const librevenge::RVNGPropertyList &) {}
void SvgWriter::closeOrderedListLevel() {}
void SvgWriter::closeUnorderedListLevel() {}
void SvgWriter::openLink(const librevenge::RVNGPropertyList &) {}
void SvgWriter::closeLink() {}

void SvgWriter::startPage(const librevenge::RVNGPropertyList &propList)
{
  m_page = PageInfo();
  m_page.index = static_cast<int>(m_pages.size() + 1);
  m_page.name = str(propList, "draw:name");
  m_page.widthPt = inch(propList, "svg:width", 11.6929) * 72.0;
  m_page.heightPt = inch(propList, "svg:height", 8.2677) * 72.0;
  m_body.clear();
  m_defs.clear();
  m_layerDepth = 0;
  m_pageOpen = true;
}

void SvgWriter::endPage()
{
  if (!m_pageOpen)
    return;
  std::ostringstream svg;
  svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" ";
  svg << "version=\"1.1\" width=\"" << fmt(m_page.widthPt) << "pt\" height=\"" << fmt(m_page.heightPt)
      << "pt\" viewBox=\"0 0 " << fmt(m_page.widthPt) << " " << fmt(m_page.heightPt) << "\">\n";
  if (!m_defs.empty())
    svg << "<defs>\n" << m_defs << "</defs>\n";
  svg << "<rect x=\"0\" y=\"0\" width=\"" << fmt(m_page.widthPt) << "\" height=\"" << fmt(m_page.heightPt)
      << "\" fill=\"#ffffff\"/>\n";
  svg << m_body;
  svg << "</svg>\n";
  m_page.svg = svg.str();
  m_pages.push_back(m_page);
  m_pageOpen = false;
}

void SvgWriter::setStyle(const librevenge::RVNGPropertyList &propList)
{
  m_style = propList;
}

void SvgWriter::startLayer(const librevenge::RVNGPropertyList &propList)
{
  std::string id = str(propList, "draw:name");
  if (id.empty())
    id = "layer" + std::to_string(m_layerDepth + 1);
  m_body += "<g id=\"" + xmlEscape(id) + "\">\n";
  ++m_layerDepth;
}

void SvgWriter::endLayer()
{
  if (m_layerDepth > 0)
  {
    m_body += "</g>\n";
    --m_layerDepth;
  }
}

void SvgWriter::openGroup(const librevenge::RVNGPropertyList &)
{
  m_body += "<g>\n";
}

void SvgWriter::closeGroup()
{
  m_body += "</g>\n";
}

void SvgWriter::drawPath(const librevenge::RVNGPropertyList &propList)
{
  const librevenge::RVNGPropertyListVector *path = propList.child("svg:d");
  if (!path)
    return;
  const std::string d = pathData(*path);
  if (d.empty())
    return;
  m_body += "<path d=\"" + d + "\"\nstyle=\"" + styleAttr(false) + "\"/>\n";
}

void SvgWriter::drawRectangle(const librevenge::RVNGPropertyList &propList)
{
  if (!exists(propList, "svg:x") || !exists(propList, "svg:y"))
    return;
  m_body += "<rect x=\"" + fmt(pt(propList, "svg:x")) + "\" y=\"" + fmt(pt(propList, "svg:y")) +
            "\" width=\"" + fmt(pt(propList, "svg:width")) + "\" height=\"" + fmt(pt(propList, "svg:height")) +
            "\" style=\"" + styleAttr(true) + "\"/>\n";
}

void SvgWriter::drawEllipse(const librevenge::RVNGPropertyList &propList)
{
  if (!exists(propList, "svg:cx") || !exists(propList, "svg:cy"))
    return;
  m_body += "<ellipse cx=\"" + fmt(pt(propList, "svg:cx")) + "\" cy=\"" + fmt(pt(propList, "svg:cy")) +
            "\" rx=\"" + fmt(pt(propList, "svg:rx")) + "\" ry=\"" + fmt(pt(propList, "svg:ry")) +
            "\" style=\"" + styleAttr(true) + "\"/>\n";
}

void SvgWriter::drawPolygon(const librevenge::RVNGPropertyList &propList)
{
  const librevenge::RVNGPropertyListVector *points = propList.child("svg:points");
  if (!points)
    return;
  std::string pts;
  for (unsigned long i = 0; i < points->count(); ++i)
  {
    const librevenge::RVNGPropertyList p((*points)[i]);
    if (!exists(p, "svg:x") || !exists(p, "svg:y"))
      continue;
    pts += fmt(pt(p, "svg:x")) + "," + fmt(pt(p, "svg:y")) + " ";
  }
  if (pts.empty())
    return;
  m_body += "<polygon points=\"" + pts + "\" style=\"" + styleAttr(true) + "\"/>\n";
}

void SvgWriter::drawPolyline(const librevenge::RVNGPropertyList &propList)
{
  const librevenge::RVNGPropertyListVector *points = propList.child("svg:points");
  if (!points)
    return;
  std::string pts;
  for (unsigned long i = 0; i < points->count(); ++i)
  {
    const librevenge::RVNGPropertyList p((*points)[i]);
    if (!exists(p, "svg:x") || !exists(p, "svg:y"))
      continue;
    pts += fmt(pt(p, "svg:x")) + "," + fmt(pt(p, "svg:y")) + " ";
  }
  if (pts.empty())
    return;
  m_body += "<polyline points=\"" + pts + "\" style=\"" + styleAttr(false) + "\"/>\n";
}

void SvgWriter::drawConnector(const librevenge::RVNGPropertyList &propList)
{
  drawPath(propList);
}

void SvgWriter::drawGraphicObject(const librevenge::RVNGPropertyList &propList)
{
  const std::string mime = str(propList, "librevenge:mime-type");
  if (mime.empty() || !exists(propList, "office:binary-data"))
    return;
  std::string transform;
  const bool flipX = flag(propList, "draw:mirror-horizontal");
  const bool flipY = flag(propList, "draw:mirror-vertical");
  const double x = pt(propList, "svg:x"), y = pt(propList, "svg:y");
  const double w = pt(propList, "svg:width"), h = pt(propList, "svg:height");
  if (flipX || flipY || exists(propList, "librevenge:rotate"))
  {
    const double cx = x + w / 2.0, cy = y + h / 2.0;
    transform = " transform=\"translate(" + fmt(cx) + "," + fmt(cy) + ") scale(" +
                (flipX ? "-1" : "1") + "," + (flipY ? "-1" : "1") + ") rotate(" +
                fmt(num(propList, "librevenge:rotate")) + ") translate(" + fmt(-cx) + "," + fmt(-cy) + ")\"";
  }
  m_body += "<image" + transform + " x=\"" + fmt(x) + "\" y=\"" + fmt(y) + "\" width=\"" + fmt(w) +
            "\" height=\"" + fmt(h) + "\" xlink:href=\"data:" + mime + ";base64," +
            str(propList, "office:binary-data") + "\"/>\n";
}

// ---------------------------------------------------------------------------
// text
// ---------------------------------------------------------------------------

void SvgWriter::startTextObject(const librevenge::RVNGPropertyList &propList)
{
  m_textBox = TextBox();
  m_textBox.x = pt(propList, "svg:x");
  m_textBox.y = pt(propList, "svg:y");
  m_textBox.w = pt(propList, "svg:width");
  m_textBox.h = pt(propList, "svg:height");
  m_textBox.padL = inch(propList, "fo:padding-left") * 72.0;
  m_textBox.padR = inch(propList, "fo:padding-right") * 72.0;
  m_textBox.padT = inch(propList, "fo:padding-top") * 72.0;
  m_textBox.padB = inch(propList, "fo:padding-bottom") * 72.0;
  m_textBox.valign = str(propList, "draw:textarea-vertical-align", "top");
  m_textBox.rotate = num(propList, "librevenge:rotate");
  m_textBox.paras.clear();
  m_inText = true;
  m_inParagraph = false;
  m_inSpan = false;
}

namespace
{

Span spanFromProps(const librevenge::RVNGPropertyList &p)
{
  Span s;
  s.family = str(p, "style:font-name");
  if (s.family.empty())
    s.family = str(p, "fo:font-family");
  const double size = pt(p, "fo:font-size", 10.0);
  s.sizePt = size > 0.0 ? size : 10.0;
  s.color = str(p, "fo:color", "#000000");
  s.bold = str(p, "fo:font-weight") == "bold" || str(p, "fo:font-weight") == "700";
  s.italic = str(p, "fo:font-style") == "italic";
  s.opacity = fraction(p, "svg:fill-opacity", 1.0);
  const std::string pos = str(p, "style:text-position");
  s.superscript = isSigned(pos, "super");
  s.subscript = isSigned(pos, "sub");
  return s;
}

} // namespace

void SvgWriter::defineParagraphStyle(const librevenge::RVNGPropertyList &propList)
{
  if (exists(propList, "librevenge:para-id"))
    m_paragraphStyles[propList["librevenge:para-id"]->getInt()] = propList;
}

void SvgWriter::defineCharacterStyle(const librevenge::RVNGPropertyList &propList)
{
  if (exists(propList, "librevenge:span-id"))
    m_characterStyles[propList["librevenge:span-id"]->getInt()] = propList;
}

void SvgWriter::openParagraph(const librevenge::RVNGPropertyList &propList)
{
  librevenge::RVNGPropertyList p(propList);
  if (exists(propList, "librevenge:para-id"))
  {
    const int id = propList["librevenge:para-id"]->getInt();
    std::map<int, librevenge::RVNGPropertyList>::const_iterator it = m_paragraphStyles.find(id);
    if (it != m_paragraphStyles.end())
      p = it->second;
  }
  m_para = Para();
  const std::string align = str(p, "fo:text-align", "left");
  m_para.align = (align == "end") ? "right" : align;
  m_para.lineHeightPct = 100.0;
  if (exists(p, "fo:line-height"))
  {
    if (isRelative(p, "fo:line-height"))
      m_para.lineHeightPct = num(p, "fo:line-height") * 100.0;
    else
      m_para.lineHeightPct = 100.0;
  }
  m_para.indent = inch(p, "fo:text-indent") * 72.0;
  m_para.marginLeft = inch(p, "fo:margin-left") * 72.0;
  m_para.marginRight = inch(p, "fo:margin-right") * 72.0;
  if (exists(p, "text:bullet-char"))
    m_para.bullet = str(p, "text:bullet-char");
  m_para.spans.clear();
  m_inParagraph = true;
}

void SvgWriter::closeParagraph()
{
  if (!m_inParagraph)
    return;
  if (m_pendingBullet.size())
  {
    Span s;
    s.text = utf8ToU32(m_pendingBullet);
    m_para.spans.insert(m_para.spans.begin(), s);
    m_pendingBullet.clear();
  }
  if (!m_para.spans.empty())
    m_textBox.paras.push_back(m_para);
  m_para = Para();
  m_inParagraph = false;
}

void SvgWriter::openSpan(const librevenge::RVNGPropertyList &propList)
{
  librevenge::RVNGPropertyList p(propList);
  if (exists(propList, "librevenge:span-id"))
  {
    const int id = propList["librevenge:span-id"]->getInt();
    std::map<int, librevenge::RVNGPropertyList>::const_iterator it = m_characterStyles.find(id);
    if (it != m_characterStyles.end())
    {
      librevenge::RVNGPropertyList merged(it->second);
      for (librevenge::RVNGPropertyList::Iter i(propList); i.next();)
        merged.insert(i.key(), i());
      p = merged;
    }
  }
  m_span = spanFromProps(p);
  m_span.text.clear();
  m_inSpan = true;
}

void SvgWriter::closeSpan()
{
  if (!m_inSpan)
    return;
  if (!m_span.text.empty() && m_inParagraph)
    m_para.spans.push_back(m_span);
  m_span.text.clear();
  m_inSpan = false;
}

void SvgWriter::insertText(const librevenge::RVNGString &text)
{
  if (!m_inSpan)
    return;
  m_span.text += utf8ToU32(text.cstr());
}

void SvgWriter::insertSpace()
{
  if (m_inSpan)
    m_span.text += U" ";
}

void SvgWriter::insertTab()
{
  if (m_inSpan)
    m_span.text += U"    ";
}

void SvgWriter::insertLineBreak()
{
  if (m_inSpan)
    m_span.text.push_back(U'\n');
}

void SvgWriter::openUnorderedListLevel(const librevenge::RVNGPropertyList &propList)
{
  if (exists(propList, "text:bullet-char"))
    m_pendingBullet = str(propList, "text:bullet-char");
}

void SvgWriter::openListElement(const librevenge::RVNGPropertyList &) {}
void SvgWriter::closeListElement() {}

void SvgWriter::endTextObject()
{
  m_inText = false;
  emitText();
}

void SvgWriter::writeTextRecord(const TextRecord &rec)
{
  m_page.texts.push_back(rec);
}

void SvgWriter::emitText()
{
  if (m_textBox.empty())
    return;
  const std::vector<Line> lines = layoutTextBox(m_textBox, m_metrics);
  if (lines.empty())
    return;

  std::string transform;
  if (m_textBox.rotate != 0.0)
  {
    const double cx = m_textBox.x + m_textBox.w / 2.0;
    const double cy = m_textBox.y + m_textBox.h / 2.0;
    transform = " transform=\"rotate(" + fmt(m_textBox.rotate) + "," + fmt(cx) + "," + fmt(cy) + ")\"";
  }

  std::string out = "<text" + transform + " xml:space=\"preserve\">\n";
  for (size_t i = 0; i < lines.size(); ++i)
  {
    const Line &line = lines[i];
    // one sidecar record per laid out line: downstream entity extraction works
    // on complete labels, not on individual font runs
    TextRecord rec;
    std::u32string lineText;
    for (size_t r = 0; r < line.runs.size(); ++r)
    {
      const LineRun &run = line.runs[r];
      if (run.text.empty())
        continue;
      double runBaseline = line.baseline;
      if (run.style.superscript)
        runBaseline -= run.style.sizePt * 0.35;
      else if (run.style.subscript)
        runBaseline += run.style.sizePt * 0.15;
      out += "<tspan x=\"" + fmt(run.x) + "\" y=\"" + fmt(runBaseline) + "\"";
      out += " font-family=\"" + xmlEscape(m_metrics.mapFamily(run.style.family)) + "\"";
      out += " font-size=\"" + fmt(run.style.sizePt) + "\"";
      if (run.style.bold)
        out += " font-weight=\"bold\"";
      if (run.style.italic)
        out += " font-style=\"italic\"";
      out += " fill=\"" + xmlEscape(run.style.color) + "\"";
      if (run.style.opacity < 0.999)
        out += " fill-opacity=\"" + fmt(run.style.opacity) + "\"";
      out += ">" + xmlEscape(u32ToUtf8(run.text)) + "</tspan>\n";

      if (lineText.empty())
      {
        rec.x = run.x;
        rec.baseline = runBaseline;
        rec.sizePt = run.style.sizePt;
        rec.family = run.style.family;
        rec.color = run.style.color;
      }
      lineText += run.text;
    }
    if (!lineText.empty())
    {
      rec.text = lineText;
      writeTextRecord(rec);
    }
  }
  out += "</text>\n";
  m_body += out;
}

} // namespace v2s
