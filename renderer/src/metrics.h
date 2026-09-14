/* Font metrics used by the text layout engine.
 *
 * The VSD file only stores font *names* (宋体, 仿宋_GB2312, Times New Roman...).
 * Text wrapping and centring need advance widths, so the writer owns a small
 * metrics model instead of relying on the browser:
 *   - CJK / full width characters advance 1 em,
 *   - half width (latin, digits) advance 0.5 em for the CJK engineering fonts
 *     used by these drawings,
 *   - per family overrides and a font substitution map can be loaded from a
 *     config file (see --metrics).
 */
#ifndef V2S_METRICS_H
#define V2S_METRICS_H

#include <map>
#include <string>

namespace v2s
{

struct FontConf
{
  double asciiWidth = 0.5;   // em fraction for U+0020..U+007E
  double defaultWidth = 1.0; // em fraction for everything else
  double ascent = 0.88;      // baseline offset from the top of the line box
  double lineGap = 0.0;      // extra leading, em fraction
  std::string substitute;    // font family actually emitted in the SVG
};

class Metrics
{
public:
  Metrics();

  /** Load "family<TAB>ascii<TAB>default<TAB>ascent<TAB>gap<TAB>substitute" lines. */
  bool load(const std::string &path);

  const FontConf &conf(const std::string &family) const;
  double advance(char32_t cp, const std::string &family, double sizePt) const;
  double width(const std::u32string &text, const std::string &family, double sizePt) const;
  std::string mapFamily(const std::string &family) const;

private:
  std::map<std::string, FontConf> m_confs;
  FontConf m_fallback;
};

/** Half width if the character is a latin/ASCII glyph, full width otherwise. */
double emFraction(char32_t cp, const FontConf &conf);

} // namespace v2s

#endif
