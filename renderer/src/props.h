/* Property helpers for the VSD->SVG output backend.
 *
 * libvisio hands everything over through librevenge property lists.  Lengths
 * are inserted either as inches (svg:* keys), points (fo:font-size) or
 * percentages (stroke/fill opacity, line-width relative dash lengths), so all
 * access goes through these converters.
 */
#ifndef V2S_PROPS_H
#define V2S_PROPS_H

#include <librevenge/librevenge.h>

#include <string>

namespace v2s
{

inline const librevenge::RVNGProperty *prop(const librevenge::RVNGPropertyList &list, const char *key)
{
  return list[key];
}

inline bool exists(const librevenge::RVNGPropertyList &list, const char *key)
{
  return list[key] != nullptr;
}

inline std::string str(const librevenge::RVNGPropertyList &list, const char *key, const char *fallback = "")
{
  const librevenge::RVNGProperty *p = list[key];
  return p ? std::string(p->getStr().cstr()) : std::string(fallback);
}

inline bool flag(const librevenge::RVNGPropertyList &list, const char *key)
{
  const librevenge::RVNGProperty *p = list[key];
  return p && p->getInt() != 0;
}

/** Value converted to inches.  Percent properties keep their raw value. */
inline double inch(const librevenge::RVNGPropertyList &list, const char *key, double fallback = 0.0)
{
  const librevenge::RVNGProperty *p = list[key];
  if (!p)
    return fallback;
  const double v = p->getDouble();
  switch (p->getUnit())
  {
  case librevenge::RVNG_INCH:
  case librevenge::RVNG_GENERIC:
    return v;
  case librevenge::RVNG_POINT:
    return v / 72.0;
  case librevenge::RVNG_TWIP:
    return v / 1440.0;
  default:
    // RVNG_PERCENT (and unknown units): relative value, handled by caller
    return v;
  }
}

inline bool isRelative(const librevenge::RVNGPropertyList &list, const char *key)
{
  const librevenge::RVNGProperty *p = list[key];
  return p && p->getUnit() == librevenge::RVNG_PERCENT;
}

/** Value converted to points. */
inline double pt(const librevenge::RVNGPropertyList &list, const char *key, double fallback = 0.0)
{
  const librevenge::RVNGProperty *p = list[key];
  if (!p)
    return fallback;
  if (p->getUnit() == librevenge::RVNG_POINT)
    return p->getDouble();
  return inch(list, key, fallback / 72.0) * 72.0;
}

/** Percentage property (libvisio stores 0..1 fractions) as 0..1 factor. */
inline double fraction(const librevenge::RVNGPropertyList &list, const char *key, double fallback = 1.0)
{
  const librevenge::RVNGProperty *p = list[key];
  if (!p)
    return fallback;
  double v = p->getDouble();
  if (p->getUnit() == librevenge::RVNG_PERCENT && v > 1.0)
    v /= 100.0;
  return v;
}

} // namespace v2s

#endif
