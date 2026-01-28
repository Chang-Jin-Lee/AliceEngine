#include "C_ActionData.h"

namespace Alice::Combat
{
    ActionDatabase ActionDatabase::MakeDefault()
    {
        ActionDatabase db{};

        db.attackLight = {
            "AttackLight",
            "attack_01",
            0.70f,
            /*hit*/ {0.20f, 0.45f},
            /*inv*/ {0.0f, 0.0f},
            /*guard*/ {0.0f, 0.0f},
            /*parry*/ {0.0f, 0.0f},
            /*cancel*/ {0.35f, 0.70f}
        };

        db.dodge = {
            "Dodge",
            "dodge_roll",
            0.60f,
            /*hit*/ {0.0f, 0.0f},
            /*inv*/ {0.10f, 0.45f},
            /*guard*/ {0.0f, 0.0f},
            /*parry*/ {0.0f, 0.0f},
            /*cancel*/ {0.0f, 0.60f}
        };

        db.guard = {
            "Guard",
            "guard_hold",
            9999.0f,
            /*hit*/ {0.0f, 0.0f},
            /*inv*/ {0.0f, 0.0f},
            /*guard*/ {0.0f, 9999.0f},
            /*parry*/ {0.0f, 0.12f},
            /*cancel*/ {0.0f, 9999.0f}
        };

        return db;
    }
}
