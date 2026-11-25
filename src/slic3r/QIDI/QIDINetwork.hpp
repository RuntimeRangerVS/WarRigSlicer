// WarRig Slicer: QIDI's cloud account and online-model service is not used by WarRig Slicer.
// This keeps the public code compiling; cloud calls report "not available".
#ifndef slic3r_QIDI_QIDINetwork_hpp_
#define slic3r_QIDI_QIDINetwork_hpp_

#include <functional>
#include <string>
#include "QIDINetworkTypes.hpp"

namespace Slic3r { namespace GUI {

class MakerHttpHandle
{
public:
    static MakerHttpHandle& getInstance() { static MakerHttpHandle h; return h; }
    // QIDI's online model list: not available without QIDI's cloud.
    void get_qidi_model_list(std::function<void(std::string, unsigned)>              on_done,
                             std::function<void(std::string, std::string, unsigned)> on_error = nullptr)
    {
        if (on_error) on_error(std::string(), "QIDI online models are not available in WarRig Slicer", 404u);
    }
};

}} // namespace Slic3r::GUI

#endif
