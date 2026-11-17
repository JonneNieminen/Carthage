#pragma once

namespace carthage::bench
{
    /** Workgroup joins seen in this process so far (WorkgroupJoins.cpp). */
    struct WorkgroupJoins
    {
        bool counted = false;   ///< false where the platform has no workgroups to count
        int attempts = 0, joined = 0, failed = 0, left = 0;

        int inWorkgroupNow() const noexcept    { return joined - left; }
    };

    WorkgroupJoins getWorkgroupJoins() noexcept;
}
