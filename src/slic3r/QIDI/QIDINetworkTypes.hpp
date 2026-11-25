// WarRig Slicer: types QIDI keeps in its unpublished src/slic3r/QIDI folder.
// Rebuilt from how the public QIDI Studio code uses them.
#ifndef slic3r_QIDI_QIDINetworkTypes_hpp_
#define slic3r_QIDI_QIDINetworkTypes_hpp_

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

// QIDI BOX contents as read from the printer: 16 box slots (4 per BOX) + the external spool last.
struct Box_info
{
    static constexpr int kSlots = 17;

    int                      box_count          = 0;
    int                      auto_reload_detect = 0;
    std::vector<int>         slot_state;      // 0 empty, 1 loaded, 2 loaded in the head
    std::vector<int>         slot_id;
    std::vector<int>         filament_vendor; // vendor index used to build QIDI filament ids
    std::vector<int>         filament_index;  // filament index used to build QIDI filament ids
    std::vector<std::string> filament_id;
    std::vector<std::string> filament_colors; // "#RRGGBB"
    std::vector<std::string> filament_type;   // e.g. "PLA"

    Box_info()
        : slot_state(kSlots, 0), slot_id(kSlots, 0), filament_vendor(kSlots, 1), filament_index(kSlots, 1),
          filament_id(kSlots), filament_colors(kSlots, "#FFFFFF"), filament_type(kSlots, "PLA")
    {
        for (int i = 0; i < kSlots; ++i) slot_id[i] = i;
    }
};

}} // namespace Slic3r::GUI

#endif
