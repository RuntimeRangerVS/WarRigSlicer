// WarRig Slicer: QIDI's cloud preset sync needs a QIDI account and cloud, which WarRig Slicer does not use.
// Presets stay on this computer. This keeps the public code compiling.
#ifndef slic3r_QIDI_UserPresetSyncManager_hpp_
#define slic3r_QIDI_UserPresetSyncManager_hpp_

namespace Slic3r { namespace GUI {

class UserPresetSyncManager
{
public:
    static UserPresetSyncManager& instance();
    void start(bool with_progress_dlg = false);
    void stop();
    void triggerSync();
};

}} // namespace Slic3r::GUI

#endif
