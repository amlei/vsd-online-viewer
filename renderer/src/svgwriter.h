/* SVG output backend for libvisio.
 *
 * libvisio drives librevenge::RVNGDrawingInterface; this class implements that
 * interface directly and writes one self contained SVG per Visio page.  Owning
 * the backend is what allows us to
 *   - emit hair lines instead of stroke-width:0 (invisible),
 *   - honour fo:text-align / paragraph breaks / wrapping,
 *   - emit Visio's real arrow head paths (libvisio provides them),
 *   - emit hatch fills as SVG <pattern>,
 *   - and export a JSON side car with every text run for downstream use.
 */
#ifndef V2S_SVGWRITER_H
#define V2S_SVGWRITER_H

#include <librevenge/librevenge.h>

#include <map>
#include <string>
#include <vector>

#include "metrics.h"
#include "textlayout.h"

namespace v2s
{

struct TextRecord
{
  std::u32string text;
  double x = 0.0;
  double baseline = 0.0;
  double sizePt = 0.0;
  std::string family;
  std::string color;
};

struct PageInfo
{
  int index = 0;
  std::string name;
  double widthPt = 0.0;
  double heightPt = 0.0;
  std::string svg;
  std::vector<TextRecord> texts;
};

class SvgWriter : public librevenge::RVNGDrawingInterface
{
public:
  explicit SvgWriter(const Metrics &metrics);
  virtual ~SvgWriter() {}

  const std::vector<PageInfo> &pages() const { return m_pages; }

  // document / pages
  void startDocument(const librevenge::RVNGPropertyList &propList) override;
  void endDocument() override;
  void setDocumentMetaData(const librevenge::RVNGPropertyList &propList) override;
  void defineEmbeddedFont(const librevenge::RVNGPropertyList &propList) override;
  void startPage(const librevenge::RVNGPropertyList &propList) override;
  void endPage() override;
  void startMasterPage(const librevenge::RVNGPropertyList &propList) override;
  void endMasterPage() override;
  void setStyle(const librevenge::RVNGPropertyList &propList) override;
  void startLayer(const librevenge::RVNGPropertyList &propList) override;
  void endLayer() override;
  void startEmbeddedGraphics(const librevenge::RVNGPropertyList &propList) override;
  void endEmbeddedGraphics() override;
  void openGroup(const librevenge::RVNGPropertyList &propList) override;
  void closeGroup() override;
  void drawRectangle(const librevenge::RVNGPropertyList &propList) override;
  void drawEllipse(const librevenge::RVNGPropertyList &propList) override;
  void drawPolygon(const librevenge::RVNGPropertyList &propList) override;
  void drawPolyline(const librevenge::RVNGPropertyList &propList) override;
  void drawPath(const librevenge::RVNGPropertyList &propList) override;
  void drawGraphicObject(const librevenge::RVNGPropertyList &propList) override;
  void drawConnector(const librevenge::RVNGPropertyList &propList) override;

  // text
  void startTextObject(const librevenge::RVNGPropertyList &propList) override;
  void endTextObject() override;
  void startTableObject(const librevenge::RVNGPropertyList &propList) override;
  void openTableRow(const librevenge::RVNGPropertyList &propList) override;
  void closeTableRow() override;
  void openTableCell(const librevenge::RVNGPropertyList &propList) override;
  void closeTableCell() override;
  void insertCoveredTableCell(const librevenge::RVNGPropertyList &propList) override;
  void endTableObject() override;
  void insertTab() override;
  void insertSpace() override;
  void insertText(const librevenge::RVNGString &text) override;
  void insertLineBreak() override;
  void insertField(const librevenge::RVNGPropertyList &propList) override;
  void openOrderedListLevel(const librevenge::RVNGPropertyList &propList) override;
  void openUnorderedListLevel(const librevenge::RVNGPropertyList &propList) override;
  void closeOrderedListLevel() override;
  void closeUnorderedListLevel() override;
  void openListElement(const librevenge::RVNGPropertyList &propList) override;
  void closeListElement() override;
  void defineParagraphStyle(const librevenge::RVNGPropertyList &propList) override;
  void openParagraph(const librevenge::RVNGPropertyList &propList) override;
  void closeParagraph() override;
  void defineCharacterStyle(const librevenge::RVNGPropertyList &propList) override;
  void openSpan(const librevenge::RVNGPropertyList &propList) override;
  void closeSpan() override;
  void openLink(const librevenge::RVNGPropertyList &propList) override;
  void closeLink() override;

private:
  std::string styleAttr(bool closed);
  std::string markerDef(const std::string &path, const std::string &viewBox, double widthPt, bool start);
  std::string hatchDef(const librevenge::RVNGPropertyList &style);
  std::string pathData(const librevenge::RVNGPropertyListVector &path);
  void emitText();
  void writeTextRecord(const TextRecord &rec);

  const Metrics &m_metrics;
  std::vector<PageInfo> m_pages;
  PageInfo m_page;
  std::string m_body;
  std::string m_defs;
  librevenge::RVNGPropertyList m_style;
  int m_layerDepth = 0;
  int m_markerCount = 0;
  int m_hatchCount = 0;
  bool m_pageOpen = false;

  // text state
  bool m_inText = false;
  bool m_inParagraph = false;
  bool m_inSpan = false;
  TextBox m_textBox;
  Para m_para;
  Span m_span;
  std::string m_pendingBullet;
  std::map<int, librevenge::RVNGPropertyList> m_characterStyles;
  std::map<int, librevenge::RVNGPropertyList> m_paragraphStyles;
};

} // namespace v2s

#endif
