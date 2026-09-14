/* vsd2svg - Visio (.vsd) to SVG converter.
 *
 * Pipeline: libvisio (patched fork) parses the binary VSD and drives our own
 * SVG backend (renderer/src/svgwriter.cpp).  For every page it writes
 *   pages/page-NN.svg   self contained SVG (no external fonts/images)
 *   pages/page-NN.json  text runs with their laid out positions
 * plus meta.json describing the document.
 */
#include <librevenge-stream/librevenge-stream.h>
#include <libvisio/libvisio.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

#include "metrics.h"
#include "svgwriter.h"
#include "textlayout.h"

namespace
{

std::string jsonEscape(const std::string &in)
{
  std::string out;
  out.reserve(in.size() + 8);
  for (size_t i = 0; i < in.size(); ++i)
  {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    switch (c)
    {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20)
      {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      }
      else
        out.push_back(static_cast<char>(c));
      break;
    }
  }
  return out;
}

std::string pageFileName(int index, const char *ext)
{
  char buf[64];
  std::snprintf(buf, sizeof(buf), "page-%02d.%s", index, ext);
  return std::string(buf);
}

bool writeFile(const std::string &path, const std::string &content)
{
  std::ofstream out(path.c_str(), std::ios::binary);
  if (!out)
    return false;
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return out.good();
}

void mkdirs(const std::string &path)
{
  std::string acc;
  for (size_t i = 0; i < path.size(); ++i)
  {
    acc.push_back(path[i]);
    if (path[i] == '/' || i + 1 == path.size())
    {
      if (acc.size() > 1)
        ::mkdir(acc.c_str(), 0777);
    }
  }
}

std::string baseName(const std::string &path)
{
  const size_t pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

double fmtDouble(double v)
{
  return v;
}

} // namespace

int main(int argc, char **argv)
{
  std::string input;
  std::string outDir = ".";
  std::string metricsFile;

  for (int i = 1; i < argc; ++i)
  {
    const std::string arg(argv[i]);
    if (arg == "--outdir" || arg == "-o")
    {
      if (i + 1 >= argc)
      {
        std::cerr << "missing value for " << arg << "\n";
        return 2;
      }
      outDir = argv[++i];
    }
    else if (arg == "--metrics")
    {
      if (i + 1 >= argc)
      {
        std::cerr << "missing value for " << arg << "\n";
        return 2;
      }
      metricsFile = argv[++i];
    }
    else if (arg == "--help" || arg == "-h")
    {
      std::cout << "usage: vsd2svg [--outdir DIR] [--metrics FILE] FILE.vsd\n";
      return 0;
    }
    else
      input = arg;
  }

  if (input.empty())
  {
    std::cerr << "usage: vsd2svg [--outdir DIR] [--metrics FILE] FILE.vsd\n";
    return 2;
  }

  v2s::Metrics metrics;
  if (!metricsFile.empty())
    metrics.load(metricsFile);

  mkdirs(outDir);
  mkdirs(outDir + "/pages");

  librevenge::RVNGFileStream stream(input.c_str());
  if (!libvisio::VisioDocument::isSupported(&stream))
  {
    std::cerr << "{\"ok\":false,\"error\":\"unsupported file\"}\n";
    return 1;
  }

  v2s::SvgWriter writer(metrics);
  if (!libvisio::VisioDocument::parse(&stream, &writer))
  {
    std::cerr << "{\"ok\":false,\"error\":\"parse failed\"}\n";
    return 1;
  }

  std::ostringstream meta;
  meta << "{\n  \"file\": \"" << jsonEscape(baseName(input)) << "\",\n";
  meta << "  \"pageCount\": " << writer.pages().size() << ",\n";
  meta << "  \"pages\": [\n";

  std::ostringstream summary;
  summary << "{\"ok\":true,\"pages\":[";

  for (size_t i = 0; i < writer.pages().size(); ++i)
  {
    const v2s::PageInfo &page = writer.pages()[i];
    const std::string svgName = pageFileName(page.index, "svg");
    const std::string jsonName = pageFileName(page.index, "json");
    if (!writeFile(outDir + "/pages/" + svgName, page.svg))
    {
      std::cerr << "{\"ok\":false,\"error\":\"cannot write " << svgName << "\"}\n";
      return 1;
    }

    std::ostringstream texts;
    texts << "{\n  \"index\": " << page.index << ",\n";
    texts << "  \"name\": \"" << jsonEscape(page.name) << "\",\n";
    texts << "  \"widthPt\": " << fmtDouble(page.widthPt) << ",\n";
    texts << "  \"heightPt\": " << fmtDouble(page.heightPt) << ",\n";
    texts << "  \"texts\": [\n";
    for (size_t t = 0; t < page.texts.size(); ++t)
    {
      const v2s::TextRecord &rec = page.texts[t];
      texts << "    {\"text\": \"" << jsonEscape(v2s::u32ToUtf8(rec.text)) << "\", "
            << "\"x\": " << fmtDouble(rec.x) << ", \"y\": " << fmtDouble(rec.baseline)
            << ", \"fontSize\": " << fmtDouble(rec.sizePt)
            << ", \"fontFamily\": \"" << jsonEscape(rec.family) << "\""
            << ", \"color\": \"" << jsonEscape(rec.color) << "\"}";
      if (t + 1 < page.texts.size())
        texts << ",";
      texts << "\n";
    }
    texts << "  ]\n}\n";
    if (!writeFile(outDir + "/pages/" + jsonName, texts.str()))
    {
      std::cerr << "{\"ok\":false,\"error\":\"cannot write " << jsonName << "\"}\n";
      return 1;
    }

    meta << "    {\"index\": " << page.index << ", \"name\": \"" << jsonEscape(page.name)
         << "\", \"svg\": \"pages/" << svgName << "\", \"json\": \"pages/" << jsonName
         << "\", \"widthPt\": " << fmtDouble(page.widthPt)
         << ", \"heightPt\": " << fmtDouble(page.heightPt)
         << ", \"textCount\": " << page.texts.size() << "}";
    if (i + 1 < writer.pages().size())
      meta << ",";
    meta << "\n";

    summary << (i ? "," : "") << "{\"index\":" << page.index << ",\"name\":\""
            << jsonEscape(page.name) << "\",\"texts\":" << page.texts.size() << "}";
  }
  meta << "  ]\n}\n";
  summary << "]}";

  if (!writeFile(outDir + "/meta.json", meta.str()))
  {
    std::cerr << "{\"ok\":false,\"error\":\"cannot write meta.json\"}\n";
    return 1;
  }

  std::cout << summary.str() << std::endl;
  return 0;
}
