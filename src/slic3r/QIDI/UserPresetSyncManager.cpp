// WarRig Slicer: cloud preset sync is not used; presets stay local.
#include "UserPresetSyncManager.hpp"

namespace Slic3r { namespace GUI {

UserPresetSyncManager& UserPresetSyncManager::instance()
{
    static UserPresetSyncManager m;
    return m;
}
void UserPresetSyncManager::start(bool) {}
void UserPresetSyncManager::stop() {}
void UserPresetSyncManager::triggerSync() {}

}} // namespace Slic3r::GUI
