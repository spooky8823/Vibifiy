// Default filter lists that Vibrium downloads into the per-user filter
// directory. They are fetched at run time, never bundled, so no list text is
// redistributed inside Vibrium builds (see filters/README.md for licences).
//
// Measured with rust/vibrium-adblock/examples/turtlecute_bench.rs against the
// adblock.turtlecute.org test (estimated score, see c++/adblock/README.md):
//   EasyList + EasyPrivacy + uBO lists            ~67%
//   + HaGeZi Multi Light (hosts-style coverage)   ~96%
#ifndef VIBRIUM_FILTER_LIST_CATALOG_H_
#define VIBRIUM_FILTER_LIST_CATALOG_H_

#include <array>

namespace vibrium {

struct FilterListSpec {
  const char* name;  // stored as <name>.txt; [a-z0-9-] only
  const char* url;   // must be https
};

inline constexpr std::array<FilterListSpec, 8> kDefaultFilterLists = {{
    {"easylist", "https://easylist.to/easylist/easylist.txt"},
    {"easyprivacy", "https://easylist.to/easylist/easyprivacy.txt"},
    {"ubo-filters",
     "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/filters.txt"},
    {"ubo-badware",
     "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/badware.txt"},
    {"ubo-privacy",
     "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/privacy.txt"},
    {"ubo-unbreak",
     "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/unbreak.txt"},
    {"ubo-quick-fixes",
     "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/quick-fixes.txt"},
    {"hagezi-light",
     "https://raw.githubusercontent.com/hagezi/dns-blocklists/main/adblock/light.txt"},
}};

}  // namespace vibrium

#endif  // VIBRIUM_FILTER_LIST_CATALOG_H_
