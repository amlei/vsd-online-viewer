#include "metrics.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace v2s
{

namespace
{

std::string trim(const std::string &s)
{
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos)
    return std::string();
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

} // namespace

double emFraction(char32_t cp, const FontConf &conf)
{
  if (cp >= 0x20 && cp <= 0x7e)
    return conf.asciiWidth;
  // half width forms and combining ranges keep the ASCII-ish advance
  if (cp >= 0xff61 && cp <= 0xffdc)
    return conf.asciiWidth;
  return conf.defaultWidth;
}

Metrics::Metrics()
{
  // Defaults tuned for the CJK engineering drawings this tool was built for.
  m_fallback.asciiWidth = 0.5;
  m_fallback.defaultWidth = 1.0;
}

bool Metrics::load(const std::string &path)
{
  std::ifstream in(path.c_str());
  if (!in)
    return false;
  std::string line;
  while (std::getline(in, line))
  {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    std::vector<std::string> fields;
    std::string field;
    std::istringstream ss(line);
    while (std::getline(ss, field, '\t'))
      fields.push_back(trim(field));
    if (fields.empty())
      continue;
    FontConf c = m_fallback;
    if (fields.size() > 1 && !fields[1].empty())
      c.asciiWidth = std::atof(fields[1].c_str());
    if (fields.size() > 2 && !fields[2].empty())
      c.defaultWidth = std::atof(fields[2].c_str());
    if (fields.size() > 3 && !fields[3].empty())
      c.ascent = std::atof(fields[3].c_str());
    if (fields.size() > 4 && !fields[4].empty())
      c.lineGap = std::atof(fields[4].c_str());
    if (fields.size() > 5)
      c.substitute = fields[5];
    m_confs[fields[0]] = c;
  }
  return true;
}

const FontConf &Metrics::conf(const std::string &family) const
{
  std::map<std::string, FontConf>::const_iterator it = m_confs.find(family);
  if (it != m_confs.end())
    return it->second;
  it = m_confs.find("*");
  if (it != m_confs.end())
    return it->second;
  return m_fallback;
}

std::string Metrics::mapFamily(const std::string &family) const
{
  const FontConf &c = conf(family);
  return c.substitute.empty() ? family : c.substitute;
}

double Metrics::advance(char32_t cp, const std::string &family, double sizePt) const
{
  return emFraction(cp, conf(family)) * sizePt;
}

double Metrics::width(const std::u32string &text, const std::string &family, double sizePt) const
{
  double w = 0.0;
  for (char32_t cp : text)
    w += advance(cp, family, sizePt);
  return w;
}

} // namespace v2s
