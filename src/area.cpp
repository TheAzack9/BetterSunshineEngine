#include <JGadget/UnorderedMap.hxx>
#include <JSystem/JKernel/JKRDvdRipper.hxx>
#include <SMS/GC2D/SelectMenu.hxx>
#include <SMS/Manager/FlagManager.hxx>
#include <SMS/raw_fn.hxx>

#include "module.hxx"
#include "p_area.hxx"

#define MESSAGE_NO_DATA "NO DATA"

static void moveStage_override(TMarDirector *director);

namespace BetterSMS {

    namespace Stage {

        static ShineAreaInfo *sShineAreaInfos[BETTER_SMS_AREA_MAX];
        static NormalAreaInfo sNormalAreaInfos[BETTER_SMS_AREA_MAX];
        static ExAreaInfo sExAreaInfos[BETTER_SMS_EXAREA_MAX];

        static NextStageCallback sNextStageHandler = moveStage_override;

        ShineAreaInfo **getShineAreaInfos() { return sShineAreaInfos; }
        NormalAreaInfo *getNormalAreaInfos() { return sNormalAreaInfos; }
        ExAreaInfo *getExAreaInfos() { return sExAreaInfos; }

    }  // namespace Stage

}  // namespace BetterSMS

using namespace BetterSMS;
using namespace BetterSMS::Stage;

static size_t getScenariosForScene(int sceneID) {
    if (sceneID == 0 || sceneID == 1)
        return 0;

    return 8;
}

static size_t getExScenariosForScene(int sceneID) { return 3; }

BETTER_SMS_FOR_CALLBACK void initAreaInfo() {
    const u8 **baseGameShineTable =
        reinterpret_cast<const u8 **>(SMS_PORT_REGION(0x803C0CC8, 0, 0, 0));
    const u8 **baseGameExShineTable =
        reinterpret_cast<const u8 **>(SMS_PORT_REGION(0x803C0CF0, 0, 0, 0));
    const s32 *baseGameScenarioNameTable =
        reinterpret_cast<const s32 *>(SMS_PORT_REGION(0x803C0D18, 0, 0, 0));
    const s32 *baseGameNormalStageTable =
        reinterpret_cast<const s32 *>(SMS_PORT_REGION(0x803C0E30, 0, 0, 0));
    const u8 *baseGameStageTable =
        reinterpret_cast<const u8 *>(SMS_PORT_REGION(0x803DF498, 0, 0, 0));
    const u8 *baseGameExShineTable2 =
        reinterpret_cast<const u8 *>(SMS_PORT_REGION(0x803DF4D8, 0, 0, 0));
    {
        JKRHeap *oldHeap = JKRHeap::sRootHeap->becomeCurrentHeap();

        const u32 scenePaneIDs[] = {0,      0, 'bi_0', 'rc_0', 'mm_0', 'pi_0',
                                    'sr_0', 0, 'mo_0', 'mr_0', 0};

        // TODO: Initialize shine stage IDs to -1
        for (size_t i = 0; i < BETTER_SMS_AREA_MAX; ++i) {
            sShineAreaInfos[i]  = nullptr;
            sNormalAreaInfos[i] = {-1};
            sExAreaInfos[i]     = {-1, -1};
        }

        for (int i = 0; i < 64; ++i) {
            ShineAreaInfo *info = sShineAreaInfos[baseGameStageTable[i]];

            bool needs_new_info = info == nullptr;
            if (needs_new_info) {
                info = new ShineAreaInfo(baseGameStageTable[i], scenePaneIDs[i]);
            }

            if (i < 10 && baseGameShineTable[info->getShineStageID()]) {
                for (int j = 0; j < getScenariosForScene(i); ++j) {
                    u8 scenarioID = baseGameShineTable[info->getShineStageID()][j];
                    info->addScenario(scenarioID, baseGameScenarioNameTable[scenarioID]);
                }
            }
            if (i < 10 && baseGameExShineTable[info->getShineStageID()]) {
                // First and last entries always unused
                for (int j = 0; j < getExScenariosForScene(i); ++j) {
                    u8 scenarioID = baseGameExShineTable[info->getShineStageID()][j];
                    info->addExScenario(scenarioID,
                                        j > 0 ? baseGameScenarioNameTable[scenarioID] : -1);
                }
            }

            if (needs_new_info) {
                registerShineStage(info);
            }

            registerNormalStage(i, info->getShineStageID());
        }

        for (int i = 0; i < 32; ++i) {
            registerExStage(i + 0x15, baseGameStageTable[i + 0x15],
                            baseGameExShineTable2[i] != 0xFF ? baseGameExShineTable2[i] : -1);
        }

        // Load custom scenes dynamically
        void *customScenesBin = JKRDvdRipper::loadToMainRAM(
            "/data/customScenes.bin", 0x0, NOP, 0, JKRHeap::sRootHeap, JKRDvdRipper::HEAD, 0, 0);
        if (customScenesBin != nullptr) {
            int size = JKRHeap::sRootHeap->getSize(customScenesBin);
            JSUMemoryInputStream memStream(customScenesBin, size);
            LevelNameRefGen data;
            JDrama::TNameRefGen::instance = &data;
            data.load(memStream);
        } else {
            OSReport("[WARN] Could not find customStages.bin, will not add any custom stages.\n");
        }

        oldHeap->becomeCurrentHeap();
    }
}

bool BetterSMS::Stage::registerShineStage(ShineAreaInfo *info) {
    if (sShineAreaInfos[info->getShineStageID()]) {
        OSReport("[WARN] Overwriting stage info for stage %d\n", info->getShineStageID());
    }
    sShineAreaInfos[info->getShineStageID()] = info;
    return true;
}

bool BetterSMS::Stage::registerNormalStage(u8 normalStageID, u8 shineStageID) {
    if (sNormalAreaInfos[normalStageID].mShineStageID != -1) {
        OSReport("[WARN] Overwriting stage info for stage %d\n", normalStageID);
    }
    sNormalAreaInfos[normalStageID] = {shineStageID};
    return true;
}

bool BetterSMS::Stage::registerExStage(u8 exStageID, u8 shineStageID, s32 shineID) {
    if (sExAreaInfos[exStageID].mShineStageID != -1) {
        OSReport("[WARN] Overwriting ex stage info for stage %d\n", exStageID);
    }
    sExAreaInfos[exStageID] = {shineStageID, shineID};
    return registerNormalStage(exStageID, shineStageID);
}

void BetterSMS::Stage::setNextStageHandler(NextStageCallback callback) {
    sNextStageHandler = callback;
}

static void moveStageHandler(TMarDirector *director) { sNextStageHandler(director); }
SMS_PATCH_BL(SMS_PORT_REGION(0x80297E40, 0, 0, 0), moveStageHandler);
SMS_PATCH_BL(SMS_PORT_REGION(0x80299244, 0, 0, 0), moveStageHandler);
SMS_PATCH_BL(SMS_PORT_REGION(0x8029933C, 0, 0, 0), moveStageHandler);
SMS_PATCH_BL(SMS_PORT_REGION(0x8029946C, 0, 0, 0), moveStageHandler);

static s32 SMS_getShineID(u32 stageID, u32 scenarioID, bool isExStage) {
    if (!sShineAreaInfos[stageID]) {
        return -1;
    }
    const ShineAreaInfo &info = *sShineAreaInfos[stageID];
    if (isExStage) {
        const TGlobalVector<s32> &exScenarioIDs = info.getExScenarioIDs();
        if (scenarioID >= exScenarioIDs.size()) {
            return -1;
        }
        return exScenarioIDs[scenarioID];
    } else {
        const TGlobalVector<s32> &scenarioIDs = info.getScenarioIDs();
        if (scenarioID >= scenarioIDs.size()) {
            return -1;
        }
        return scenarioIDs[scenarioID];
    }
}
SMS_PATCH_B(SMS_PORT_REGION(0x8016FAC0, 0, 0, 0), SMS_getShineID);
SMS_PATCH_B(SMS_PORT_REGION(0x80175AF8, 0, 0, 0), SMS_getShineID);
SMS_PATCH_B(SMS_PORT_REGION(0x8017CC6C, 0, 0, 0), SMS_getShineID);

static s32 SMS_getShineIDofExStage(u32 exStageID) {
    if (sExAreaInfos[exStageID].mShineStageID == -1)
        return -1;
    return sExAreaInfos[exStageID].mShineID;
}
SMS_PATCH_B(SMS_PORT_REGION(0x802a8a98, 0, 0, 0), SMS_getShineIDofExStage);

static s32 SMS_getShineStage(u32 stageID) { return sNormalAreaInfos[stageID].mShineStageID; }
SMS_PATCH_B(SMS_PORT_REGION(0x802A8AC8, 0, 0, 0), SMS_getShineStage);

static TExPane *constructExPaneForSelectScreen(TExPane *pane, J2DScreen *screen) {
    TSelectMenu *menu;
    SMS_FROM_GPR(31, menu);

    // This check is only necessary once
    SMS_ASSERT(sShineAreaInfos[SMS_getShineStage(menu->mAreaID)]->getShineSelectPaneID() != 0,
               "Tried to open shine select screen for an area that has no pane ID!");

    return (TExPane *)__ct__7TExPaneFP9J2DScreenUl(
        pane, screen, sShineAreaInfos[SMS_getShineStage(menu->mAreaID)]->getShineSelectPaneID());
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80174D40, 0, 0, 0), constructExPaneForSelectScreen);

static TBoundPane *constructBoundPaneForSelectScreenA(TBoundPane *pane, J2DScreen *screen) {
    TSelectMenu *menu;
    SMS_FROM_GPR(31, menu);

    u32 paneID =
        (sShineAreaInfos[SMS_getShineStage(menu->mAreaID)]->getShineSelectPaneID() & 0xFFFFFF00) |
        'a';

    return (TBoundPane *)__ct__10TBoundPaneFP9J2DScreenUl(pane, screen, paneID);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80174D88, 0, 0, 0), constructBoundPaneForSelectScreenA);

static TBoundPane *constructBoundPaneForSelectScreenB(TBoundPane *pane, J2DScreen *screen) {
    TSelectMenu *menu;
    SMS_FROM_GPR(31, menu);

    u32 paneID =
        (sShineAreaInfos[SMS_getShineStage(menu->mAreaID)]->getShineSelectPaneID() & 0xFFFFFF00) |
        'b';

    return (TBoundPane *)__ct__10TBoundPaneFP9J2DScreenUl(pane, screen, paneID);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80174DD0, 0, 0, 0), constructBoundPaneForSelectScreenB);

static bool getShineFlagForSelectScreen1() {
    TSelectMenu *menu;
    SMS_FROM_GPR(31, menu);

    int shineID = SMS_getShineID(SMS_getShineStage(menu->mAreaID), 0, true);
    if (shineID == -1) {
        return false;
    }

    return TFlagManager::smInstance->getShineFlag(shineID);
}
SMS_WRITE_32(SMS_PORT_REGION(0x80174B40, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174B44, 0, 0, 0), 0x60000000);
SMS_PATCH_BL(SMS_PORT_REGION(0x80174B48, 0, 0, 0), getShineFlagForSelectScreen1);

static bool getShineFlagForSelectScreen2() {
    TSelectMenu *menu;
    SMS_FROM_GPR(31, menu);

    int index;
    SMS_FROM_GPR(26, index);

    int shineID = SMS_getShineID(SMS_getShineStage(menu->mAreaID), index, true);
    if (shineID == -1) {
        return false;
    }

    return TFlagManager::smInstance->getShineFlag(shineID);
}
SMS_WRITE_32(SMS_PORT_REGION(0x80174B8C, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174B90, 0, 0, 0), 0x60000000);
SMS_PATCH_BL(SMS_PORT_REGION(0x80174B94, 0, 0, 0), getShineFlagForSelectScreen2);

static u8 sScenarioCountForSelectArea = 0;
static u8 sScenarioMaxForAnyArea      = 0;

static float doScenarioCountPatches(void *throwaway, u8 area) {
    TSelectMenu *menu;
    SMS_FROM_GPR(31, menu);
    menu->mAreaID = area;

    ShineAreaInfo *info         = sShineAreaInfos[SMS_getShineStage(menu->mAreaID)];
    sScenarioCountForSelectArea = info ? info->getScenarioIDs().size() : 8;

    if (sScenarioMaxForAnyArea == 0) {
        for (u32 i = 0; i < 0xFF; ++i) {
            ShineAreaInfo *info = sShineAreaInfos[SMS_getShineStage(i)];
            if (info && info->getShineSelectPaneID() != 0) {
                sScenarioMaxForAnyArea = Max(sScenarioMaxForAnyArea, info->getScenarioIDs().size());
            }
        }
    }

    // initData
    PowerPC::writeU32((u32 *)0x80174E84, 0x2C000000 | sScenarioCountForSelectArea);
    PowerPC::writeU32((u32 *)0x80174E90, 0x28000000 | sScenarioCountForSelectArea);
    PowerPC::writeU32((u32 *)0x80174E98, 0x38000000 | sScenarioCountForSelectArea);
    PowerPC::writeU32((u32 *)0x80174ED0, 0x2C050000 | sScenarioCountForSelectArea);
    PowerPC::writeU32((u32 *)0x80174ED4, 0x20650000 | sScenarioCountForSelectArea);
    PowerPC::writeU32((u32 *)0x80174ED8, 0x48000074);

    PowerPC::writeU32((u32 *)0x8017505C, 0x20030000 | (sScenarioMaxForAnyArea - 1));
    PowerPC::writeU32((u32 *)0x80175114, 0x20030000 | sScenarioMaxForAnyArea);

    // --- 0x150

    PowerPC::writeU32((u32 *)0x801750A4, 0x819F0150);  // lwz r12, 0x150 (r31)
    PowerPC::writeU32((u32 *)0x801750B0, 0x7C0CD8AE);  // lbzx r0, r12, r27

    PowerPC::writeU32((u32 *)0x8017515C, 0x819F0150);  // lwz r12, 0x150 (r31)
    PowerPC::writeU32((u32 *)0x80175168, 0x7C0CD8AE);  // lbzx r0, r12, r27

    // --- 0xDC

    // Preallocate
    *(u32 **)((u8 *)menu + 0xDC) = new u32[sScenarioCountForSelectArea];
    PowerPC::writeU32((u32 *)0x80175020, 0x60000000);  // nop the initializer ^^

    PowerPC::writeU32((u32 *)0x80175094, 0x835F00DC);  // lwz r26, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x80175098, 0x7F5AEA14);  // add r26, r26, r29

    PowerPC::writeU32((u32 *)0x8017514C, 0x835F00DC);  // lwz r26, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x80175150, 0x7F5AE214);  // add r26, r26, r28

    PowerPC::writeU32((u32 *)0x8017529C, 0x819F00DC);  // lwz r12, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x801752A0, 0x7C6C182E);  // lwzx r3, r12, r3

    PowerPC::writeU32((u32 *)0x801752B4, 0x807F00DC);  // lwz r3, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x801752B8, 0x7C63002E);  // lwzx r3, r3, r0

    //

    // perform
    PowerPC::writeU32((u32 *)0x801736A8, 0x899F013B);  // lbz r12, 0x13B (r31)
    PowerPC::writeU32((u32 *)0x801736B0, 0x807F0150);  // lwz r3, 0x150 (r31)
    PowerPC::writeU32((u32 *)0x801736B4, 0x7C0C18AE);  // lbzx r0, r12, r3

    // --- 0xDC
    PowerPC::writeU32((u32 *)0x80173D58, 0x819F00DC);  // lwz r12, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x80173D5C, 0x7C6C182E);  // lwzx r3, r12, r3
    PowerPC::writeU32((u32 *)0x80173D70, 0x807F00DC);  // lwz r3, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x80173D74, 0x7C63002E);  // lwzx r3, r3, r0

    PowerPC::writeU32((u32 *)0x80173DF0, 0x819F00DC);  // lwz r12, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x80173DF4, 0x7C6C182E);  // lwzx r3, r12, r3
    PowerPC::writeU32((u32 *)0x80173E08, 0x807F00DC);  // lwz r3, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x80173E0C, 0x7C63002E);  // lwzx r3, r3, r0

    PowerPC::writeU32((u32 *)0x801739B4, 0x819F00DC);  // lwz r12, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x801739B8, 0x7C6C182E);  // lwzx r3, r12, r3
    PowerPC::writeU32((u32 *)0x801739CC, 0x807F00DC);  // lwz r3, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x801739D0, 0x7C63002E);  // lwzx r3, r3, r0

    PowerPC::writeU32((u32 *)0x80173A4C, 0x819F00DC);  // lwz r12, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x80173A50, 0x7C6C182E);  // lwzx r3, r12, r3
    PowerPC::writeU32((u32 *)0x80173A64, 0x807F00DC);  // lwz r3, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x80173A68, 0x7C63002E);  // lwzx r3, r3, r0

    PowerPC::writeU32((u32 *)0x801740B4, 0x819F00DC);  // lwz r12, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x801740B8, 0x7C6C182E);  // lwzx r3, r12, r3

    PowerPC::writeU32((u32 *)0x80174108, 0x807F00DC);  // lwz r3, 0xDC (r31)
    PowerPC::writeU32((u32 *)0x8017410C, 0x7C63002E);  // lwzx r3, r3, r0

    // -- Stuff from TSelectShineManager
    PowerPC::writeU32((u32 *)0x801739E4, 0x80630010);  // lwz r3, 0x10 (r3)
    PowerPC::writeU32((u32 *)0x801739E8, 0x7C63002E);  // lwzx r3, r3, r0

    PowerPC::writeU32((u32 *)0x80173A7C, 0x80630010);  // lwz r3, 0x10 (r3)
    PowerPC::writeU32((u32 *)0x80173A80, 0x7C63002E);  // lwzx r3, r3, r0

    PowerPC::writeU32((u32 *)0x80173D40, 0x80630010);  // lwz r3, 0x10 (r3)
    PowerPC::writeU32((u32 *)0x80173D44, 0x7C63002E);  // lwzx r3, r3, r0

    PowerPC::writeU32((u32 *)0x80173DD8, 0x80630010);  // lwz r3, 0x10 (r3)
    PowerPC::writeU32((u32 *)0x80173DDC, 0x7C63002E);  // lwzx r3, r3, r0
    //

    // getNextIndex
    PowerPC::writeU32((u32 *)0x80172C3C, 0x28040000 | sScenarioCountForSelectArea);
    PowerPC::writeU32((u32 *)0x80172C50, 0x20040000 | sScenarioCountForSelectArea);
    PowerPC::writeU32((u32 *)0x80172C54, 0x2C040000 | sScenarioCountForSelectArea);

    PowerPC::writeU32((u32 *)0x80172C60, 0x81830150);  // lwz r12, 0x150 (r3)
    PowerPC::writeU32((u32 *)0x80172C64, 0x7C0C20AE);  // lbzx r0, r12, r4

    PowerPC::writeU32((u32 *)0x80172C04, 0x81830150);  // lwz r12, 0x150 (r3)
    PowerPC::writeU32((u32 *)0x80172C08, 0x7C0C20AE);  // lbzx r0, r12, r4

    PowerPC::writeU32((u32 *)0x80174450, 0x809F0150);  // lwz r4, 0x150 (r31)

    // TSelectShineManager::perform
    PowerPC::writeU32((u32 *)0x801781BC, 0x81990010);  // lwz r12, 0x10 (r25)
    PowerPC::writeU32((u32 *)0x801781C0, 0x7C6CB82E);  // lwzx r3, r12, r23
    PowerPC::writeU32((u32 *)0x80178330, 0x83990010);  // lwz r28, 0x10 (r25)
    PowerPC::writeU32((u32 *)0x80178338, 0x7F9CC214);  // add r28, r28, r24
    PowerPC::writeU32((u32 *)0x801784EC, 0x82B90010);  // lwz r21, 0x10 (r25)
    PowerPC::writeU32((u32 *)0x801784F0, 0x7EB5E214);  // add r21, r21, r28

    // TSelectShineManager::startDecrease
    PowerPC::writeU32((u32 *)0x80178634, 0x80A30010);  // lwz r5, 0x10 (r3)
    PowerPC::writeU32((u32 *)0x80178638, 0x7CC5002E);  // lwzx r6, r5, r0
    PowerPC::writeU32((u32 *)0x801786D8, 0x80630010);  // lwz r3, 0x10 (r3)
    PowerPC::writeU32((u32 *)0x801786DC, 0x7C83002E);  // lwzx r4, r3, r0

    // TSelectShineManager::startIncrease
    PowerPC::writeU32((u32 *)0x80178738, 0x80A30010);  // lwz r5, 0x10 (r3)
    PowerPC::writeU32((u32 *)0x8017873C, 0x7CC5002E);  // lwzx r6, r5, r0
    PowerPC::writeU32((u32 *)0x801787DC, 0x80630010);  // lwz r3, 0x10 (r3)
    PowerPC::writeU32((u32 *)0x801787E0, 0x7C83002E);  // lwzx r4, r3, r0

    // TSelectShineManager::initData  -> reference next patch for where buffer is alloc'd from
    PowerPC::writeU32((u32 *)0x80178D58, 0x819F0010);  // lwz r12, 0x10 (r31)
    PowerPC::writeU32((u32 *)0x80178D5C, 0x7E6CC92E);  // stwx r19, r12, r25
    PowerPC::writeU32((u32 *)0x80178DF0, 0x819F0010);  // lwz r12, 0x10 (r31)
    PowerPC::writeU32((u32 *)0x80178DF4, 0x7E6CC92E);  // stwx r19, r12, r25
    PowerPC::writeU32((u32 *)0x80178DFC, 0x819F0010);  // lwz r12, 0x10 (r31)
    PowerPC::writeU32((u32 *)0x80178E04, 0x7C6CC92E);  // stwx r3, r12, r25
    PowerPC::writeU32((u32 *)0x80178E0C, 0x2C140000 | sScenarioCountForSelectArea);
    PowerPC::writeU32((u32 *)0x80178E28, 0x807F0010);  // lwz r3, 0x10 (r31)
    PowerPC::writeU32((u32 *)0x80178E2C, 0x7C83002E);  // lwzx r4, r3, r0

    // TSelectMenu::startMove -> TSelectShineManager had buffer updated
    PowerPC::writeU32((u32 *)0x8017447C, 0x80630010);  // lwz r3, 0x10 (r3)
    PowerPC::writeU32((u32 *)0x80174480, 0x7C63002E);  // lwzx r3, r3, r0

    *(u8 **)((u8 *)menu + 0x150) = new u8[sScenarioCountForSelectArea];

    // Emulate functionality at 0x80174DFC
    for (u32 i = 0; i < sScenarioCountForSelectArea; ++i) {
        (*(u8 **)((u8 *)menu + 0x150))[i] = 2;
    }

    // Emulate functionality at 0x80174E84
    u8 unlocked_scenarios = 0;
    for (u32 i = 0; i < sScenarioCountForSelectArea; ++i) {
        const u8 shine_stage = SMS_getShineStage(area);
        const s32 shine_id   = SMS_getShineID(shine_stage, i, false);
        if (shine_id == -1) {
            continue;
        }

        if (!TFlagManager::smInstance->getShineFlag(shine_id)) {
            continue;
        }

        (*(u8 **)((u8 *)menu + 0x150))[i] = 3;
        unlocked_scenarios += 1;
    }

    menu->mEpisodeCount = unlocked_scenarios;

    // Emulate functionality at 0x80174EE8
    for (u32 i = unlocked_scenarios; i < sScenarioCountForSelectArea; ++i) {
        (*(u8 **)((u8 *)menu + 0x150))[i] = 0;
    }

    return SMSGetAnmFrameRate();
}
SMS_PATCH_BL(SMS_PORT_REGION(0x801744D0, 0, 0, 0), doScenarioCountPatches);
SMS_WRITE_32(SMS_PORT_REGION(0x80174DFC, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174E00, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174E04, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174E08, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174E0C, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174E10, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174E14, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174E18, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80174E1C, 0, 0, 0), 0x48000070);

static void allocBufferForSelectShineManager(J3DModelData *data, J3DAnmColor *color) {
    u32 *manager;
    SMS_FROM_GPR(31, manager);

    data->entryMatColorAnimator(color);

    *(u32 **)((u8 *)manager + 0x10) = new u32[sScenarioCountForSelectArea];
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80178B50, 0, 0, 0), allocBufferForSelectShineManager);

static void SelectShineManager_startCloseOverride(TSelectShineManager *manager) {
    int current = *(u32 *)((u8 *)manager + 0x8C);
    *(((u8 *)((*(u32 ***)((u8 *)manager + 0x10))[current])) + 0x24) = 0;

    for (int i = 0; i < 8; i++) {
        u8 *shine = (u8 *)((*(u32 ***)((u8 *)manager + 0x10))[i]);
        if (shine != nullptr && i != current && *(shine + 0x49) == 0) {
            *(shine + 0x49) = 1;
            *(shine + 0x48) = 0;
        }
    }

    *((u8 *)manager + 0xA7) = 1;
}
SMS_PATCH_B(SMS_PORT_REGION(0x80178830, 0, 0, 0), SelectShineManager_startCloseOverride);

static J2DPicture *createExtraDigitAsChildOf(u32 paneID, JUTTexture *defaultTex, J2DPicture *parent,
                                             bool isBack) {
    J2DPicture *digitPicture = new J2DPicture(paneID, {0, 0, 0, 0});

    JUTTexture *texture      = new JUTTexture();
    texture->mTexObj2.val[2] = 0;
    texture->storeTIMG(*(const ResTIMG **)((u8 *)defaultTex + 0x20));
    texture->_50 = false;

    digitPicture->insert(texture, 0, 1.0f);

    digitPicture->mRect = {15, 0, 40, 40};

    digitPicture->mAlpha     = isBack ? 80 : 255;
    digitPicture->mAlphaCopy = true;

    digitPicture->mColorMask    = isBack ? JUtility::TColor{0, 0, 0, 255}
                                         : JUtility::TColor{0, 255, 160, 255};
    digitPicture->mColorOverlay = {0, 0, 0, 0};

    digitPicture->mVertexColors[0] = {255, 255, 255, 255};
    digitPicture->mVertexColors[1] = {255, 255, 255, 255};
    digitPicture->mVertexColors[2] = {255, 255, 255, 255};
    digitPicture->mVertexColors[3] = {255, 255, 255, 255};

    parent->mChildrenList.append(&digitPicture->mPtrLink);
    return digitPicture;
}

static void initMultiDigitEpisodeIndexToUI() {
    TSelectMenu *menu;
    SMS_FROM_GPR(31, menu);

    J2DPicture *picPrevFront = *(J2DPicture **)((u8 *)menu + 0x48);
    J2DPicture *picPrevBack  = *(J2DPicture **)((u8 *)menu + 0x4C);
    J2DPicture *picCurFront = *(J2DPicture **)((u8 *)menu + 0x70);
    J2DPicture *picCurBack   = *(J2DPicture **)((u8 *)menu + 0x74);

    picPrevBack->mIsVisible = false;
    picCurBack->mIsVisible = false;

    picPrevFront->mColorMask = JUtility::TColor{0, 255, 160, 255};
    picPrevBack->mColorMask  = JUtility::TColor{0, 0, 0, 255};
    picCurFront->mColorMask  = JUtility::TColor{0, 255, 160, 255};
    picCurBack->mColorMask   = JUtility::TColor{0, 0, 0, 255};

    picPrevFront->mColorOverlay = JUtility::TColor{0, 0, 0, 0};
    picPrevBack->mColorOverlay  = JUtility::TColor{0, 0, 0, 0};
    picCurFront->mColorOverlay  = JUtility::TColor{0, 0, 0, 0};
    picCurBack->mColorOverlay   = JUtility::TColor{0, 0, 0, 0};

    picPrevFront->move(40, -7);
    picCurFront->move(40, -7);

    JUTTexture *defaultTex = menu->mCoinCountNumberTex[0];

    // Because of other modifications, these pointers are free use
    J2DPicture *picPrevFront2 = picPrevFront2 =
        createExtraDigitAsChildOf('xnf2', defaultTex, picPrevFront, false);
    *(J2DPicture **)((u8 *)menu + 0xE0) = picPrevFront2;

    J2DPicture *picPrevBack2 = picPrevBack2 =
        createExtraDigitAsChildOf('xnb2', defaultTex, picPrevBack, true);
    *(J2DPicture **)((u8 *)menu + 0xE4) = picPrevBack2;

    J2DPicture *picCurFront2 = createExtraDigitAsChildOf('xcf2', defaultTex, picCurFront, false);
    *(J2DPicture **)((u8 *)menu + 0xE8) = picCurFront2;

    J2DPicture *picCurBack2 = createExtraDigitAsChildOf('xcb2', defaultTex, picCurBack, true);
    *(J2DPicture **)((u8 *)menu + 0xEC) = picCurBack2;

    const u8 uiCurIndex = menu->mEpisodeID + 1;
    if (uiCurIndex >= 10) {
        picPrevFront->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex / 10] + 0x20), 0);
        picPrevBack->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex / 10] + 0x20), 0);
        picPrevFront2->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex % 10] + 0x20), 0);
        picPrevBack2->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex % 10] + 0x20), 0);
        picPrevFront2->mIsVisible = true;
        picPrevBack2->mIsVisible  = true;

        picPrevFront->resize(25, 40);
        picPrevBack->resize(25, 40);
    } else {
        picPrevFront->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex] + 0x20), 0);
        picPrevBack->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex] + 0x20), 0);
        picPrevFront2->mIsVisible = false;
        picPrevBack2->mIsVisible  = false;
    }
}
SMS_PATCH_BL(SMS_PORT_REGION(0x801752D8, 0, 0, 0), initMultiDigitEpisodeIndexToUI);
SMS_WRITE_32(SMS_PORT_REGION(0x801752DC, 0, 0, 0), 0x48000020);

static void applyMultiDigitEpisodeIndexToUI() {
    TSelectMenu *menu;
    SMS_FROM_GPR(31, menu);

    u32 toIndex;
    SMS_FROM_GPR(23, toIndex);

    J2DPicture *picPrevFront = *(J2DPicture **)((u8 *)menu + 0x48);
    J2DPicture *picPrevBack  = *(J2DPicture **)((u8 *)menu + 0x4C);
    J2DPicture *picCurFront = *(J2DPicture **)((u8 *)menu + 0x70);
    J2DPicture *picCurBack  = *(J2DPicture **)((u8 *)menu + 0x74);

    // Because of other modifications, these pointers are free use
    J2DPicture *picPrevFront2 = *(J2DPicture **)((u8 *)menu + 0xE0);
    J2DPicture *picPrevBack2  = *(J2DPicture **)((u8 *)menu + 0xE4);
    J2DPicture *picCurFront2  = *(J2DPicture **)((u8 *)menu + 0xE8);
    J2DPicture *picCurBack2   = *(J2DPicture **)((u8 *)menu + 0xEC);

    const u8 uiToIndex  = toIndex + 1;
    const u8 uiCurIndex = menu->mEpisodeID + 1;

    if (uiCurIndex >= 10) {
        picPrevFront->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex / 10] + 0x20), 0);
        picPrevBack->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex / 10] + 0x20), 0);
        picPrevFront2->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex % 10] + 0x20), 0);
        picPrevBack2->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex % 10] + 0x20), 0);
        picPrevFront2->mIsVisible = true;
        picPrevBack2->mIsVisible  = true;

        picPrevFront->resize(25, 40);
        picPrevBack->resize(25, 40);
    } else {
        picPrevFront->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex] + 0x20), 0);
        picPrevBack->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiCurIndex] + 0x20), 0);
        picPrevFront2->mIsVisible = false;
        picPrevBack2->mIsVisible  = false;

        picPrevFront->resize(40, 40);
        picPrevBack->resize(40, 40);
    }

    if (uiToIndex >= 10) {
        picCurFront->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiToIndex / 10] + 0x20), 0);
        picCurBack->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiToIndex / 10] + 0x20), 0);
        picCurFront2->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiToIndex % 10] + 0x20), 0);
        picCurBack2->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiToIndex % 10] + 0x20), 0);
        picCurFront2->mIsVisible = true;
        picCurBack2->mIsVisible  = true;

        picCurFront->resize(25, 40);
        picCurBack->resize(25, 40);
    } else {
        picCurFront->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiToIndex] + 0x20), 0);
        picCurBack->changeTexture(
            *(const ResTIMG **)((u8 *)menu->mCoinCountNumberTex[uiToIndex] + 0x20), 0);
        picCurFront2->mIsVisible = false;
        picCurBack2->mIsVisible  = false;

        picCurFront->resize(40, 40);
        picCurBack->resize(40, 40);
    }
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80173C90, 0, 0, 0), applyMultiDigitEpisodeIndexToUI);
SMS_WRITE_32(SMS_PORT_REGION(0x80173C94, 0, 0, 0), 0x48000054);
SMS_PATCH_BL(SMS_PORT_REGION(0x80173904, 0, 0, 0), applyMultiDigitEpisodeIndexToUI);
SMS_WRITE_32(SMS_PORT_REGION(0x80173908, 0, 0, 0), 0x48000054);

static const char *getScenarioNameForSelectScreen() {
    TSelectMenu *menu;
    SMS_FROM_GPR(31, menu);

    ShineAreaInfo *info = sShineAreaInfos[SMS_getShineStage(menu->mAreaID)];
    if (!info) {
        return MESSAGE_NO_DATA;
    }

    const TGlobalVector<s32> &scenarioNameIDs = info->getScenarioNameIDs();
    if (menu->mEpisodeID >= scenarioNameIDs.size()) {
        return MESSAGE_NO_DATA;
    }

    s32 message_idx = scenarioNameIDs[menu->mEpisodeID];
    if (message_idx == -1) {
        return MESSAGE_NO_DATA;
    }

    return (const char *)SMSGetMessageData__FPvUl(menu->mScenarioBMGData, message_idx);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8017539C, 0, 0, 0), getScenarioNameForSelectScreen);
SMS_PATCH_BL(SMS_PORT_REGION(0x8017398C, 0, 0, 0), getScenarioNameForSelectScreen);
SMS_PATCH_BL(SMS_PORT_REGION(0x80173A24, 0, 0, 0), getScenarioNameForSelectScreen);
SMS_PATCH_BL(SMS_PORT_REGION(0x80173D18, 0, 0, 0), getScenarioNameForSelectScreen);
SMS_PATCH_BL(SMS_PORT_REGION(0x80173DB0, 0, 0, 0), getScenarioNameForSelectScreen);

const char *loadStageNameFromBMG(void *global_bmg) {
    const char *message;

    const char *errMessage = BetterSMS::isDebugMode() ? MESSAGE_NO_DATA : nullptr;

    if (gpMarDirector->mAreaID >= 60) {
        TFlagManager::smInstance->setFlag(0x40003, gpMarDirector->mEpisodeID);
    }

    s32 area_id = SMS_getShineStage(gpMarDirector->mAreaID);
    message     = (const char *)SMSGetMessageData__FPvUl(global_bmg, area_id);
    return message ? message : errMessage;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80172704, 0x802A0C00, 0, 0), loadStageNameFromBMG);
SMS_PATCH_BL(SMS_PORT_REGION(0x80156D2C, 0x802A0C00, 0, 0), loadStageNameFromBMG);

static const char *loadScenarioNameFromBMG(void *global_bmg) {
    const char *message;

    const char *errMessage = BetterSMS::isDebugMode() ? MESSAGE_NO_DATA : nullptr;

    ShineAreaInfo *info = sShineAreaInfos[SMS_getShineStage(gpMarDirector->mAreaID)];
    if (!info) {
        return errMessage;
    }

    const TGlobalVector<s32> &scenarioNameIDs = info->getScenarioNameIDs();

    s32 episode_id = TFlagManager::smInstance->getFlag(0x40003);
    if (episode_id >= scenarioNameIDs.size()) {
        return errMessage;
    }

    s32 message_idx = scenarioNameIDs[episode_id];
    if (message_idx == -1) {
        return MESSAGE_NO_DATA;
    }

    message = (const char *)SMSGetMessageData__FPvUl(global_bmg, message_idx);
    return message ? message : errMessage;
}
SMS_WRITE_32(SMS_PORT_REGION(0x80172734, 0, 0, 0), 0x4800006C);
SMS_PATCH_BL(SMS_PORT_REGION(0x801727A0, 0x802A0C00, 0, 0), loadScenarioNameFromBMG);

static const char *loadScenarioNameFromBMGAfter(void *global_bmg) {
    const char *message;

    const char *errMessage = BetterSMS::isDebugMode() ? MESSAGE_NO_DATA : nullptr;

    ShineAreaInfo *info = sShineAreaInfos[SMS_getShineStage(gpMarDirector->mAreaID)];
    if (!info) {
        return errMessage;
    }

    const TGlobalVector<s32> &scenarioNameIDs = info->getScenarioNameIDs();

    s32 episode_id = TFlagManager::smInstance->getFlag(0x40003);
    if (episode_id >= scenarioNameIDs.size()) {
        return errMessage;
    }

    s32 message_idx = scenarioNameIDs[episode_id];
    if (message_idx == -1) {
        return MESSAGE_NO_DATA;
    }

    message = (const char *)SMSGetMessageData__FPvUl(global_bmg, message_idx);
    return message ? message : errMessage;
}

SMS_NO_INLINE static const char *loadScenarioNameFromBMGAfterStub(u8 *pause_menu,
                                                                  void *global_bmg) {
    const char *name = loadScenarioNameFromBMGAfter(global_bmg);
    if (!name || strcmp(name, "") == 0) {
        (*(J2DPane **)(pause_menu + 0x1C))->add(0, 30);
        (*(J2DPane **)(pause_menu + 0xD4))->add(0, 15);
    }

    return name;
}

// Stupid register bullshit
static const char *loadScenarioNameFromBMGAfterStubStub(void *global_bmg) {
    u8 *pause_menu;
    SMS_FROM_GPR(29, pause_menu);

    return loadScenarioNameFromBMGAfterStub(pause_menu, global_bmg);
}
SMS_WRITE_32(SMS_PORT_REGION(0x80156D5C, 0, 0, 0), 0x480000A8);
SMS_PATCH_BL(SMS_PORT_REGION(0x80156E04, 0x802A0C00, 0, 0), loadScenarioNameFromBMGAfterStubStub);

// Default stage override

static void moveStage_override(TMarDirector *director) {
    if (gpApplication.mNextScene.mAreaID <= 60 || gpApplication.mNextScene.mEpisodeID != 0xFF) {
        director->moveStage();
        return;
    }

    if (sNormalAreaInfos[gpApplication.mNextScene.mAreaID].mShineStageID == -1) {
        director->moveStage();
        return;
    }

    s32 shineStageID = SMS_getShineStage(gpApplication.mNextScene.mAreaID);

    bool isSameShineStage = SMS_getShineStage(gpApplication.mCurrentScene.mAreaID) == shineStageID;
    bool isSameNormalStage =
        gpApplication.mCurrentScene.mAreaID == gpApplication.mNextScene.mAreaID;

    if (isSameShineStage && !isSameNormalStage && !TFlagManager::smInstance->getBool(0x50010)) {
        if (gpApplication.mNextScene.mEpisodeID == 0xFF) {
            gpApplication.mNextScene.mEpisodeID = gpApplication.mCurrentScene.mEpisodeID;
            director->moveStage();
            return;
        }
    }

    gpApplication.mFader->setColor({0, 0, 0, 255});

    if (SMS_getShineStage(gpApplication.mNextScene.mAreaID) !=
        SMS_getShineStage(gpApplication.mCurrentScene.mAreaID)) {
        TFlagManager::smInstance->setFlag(0x40002, 0);
    }

    *(u32 *)((u8 *)director + 0xE4) = 8;
    director->mNextState            = 8;
}

JDrama::TNameRef *LevelNameRefGen::getNameRef(const char *name) const {
    if (strcmp("CustomScene", name) == 0) {
        CustomScene *stage = new CustomScene(name);
        return stage;
    }
    return JDrama::TNameRefGen::getNameRef(name);
}

#define READ_ATTR(in, var) ((in).readData(&(var), sizeof(decltype((var)))))

// See: https://github.com/JoshuaMKW/JuniorsToolbox/tree/master/Templates/CustomScene.json
struct ScenarioData {
    s16 m_bmg_name_index  = -1;
    bool m_is_ex_scenario = false;
    s16 m_shine_id        = -1;

    void deserialize(JSUMemoryInputStream &in) {
        READ_ATTR(in, m_bmg_name_index);
        READ_ATTR(in, m_is_ex_scenario);
        READ_ATTR(in, m_shine_id);
    }
};

struct AreaData {
    u8 m_area_id      = 0xFF;
    bool m_is_ex_area = false;
    s16 m_ex_shine_id = -1;

    void deserialize(JSUMemoryInputStream &in) {
        READ_ATTR(in, m_area_id);
        READ_ATTR(in, m_is_ex_area);
        READ_ATTR(in, m_ex_shine_id);
    }
};

CustomScene::CustomScene(const char *name) : JDrama::TNameRef(name) {}

void CustomScene::load(JSUMemoryInputStream &in) {
    JDrama::TNameRef::load(in);

    u8 logical_scene_id;
    u32 shine_select_pane_id;
    READ_ATTR(in, logical_scene_id);
    READ_ATTR(in, shine_select_pane_id);

    u32 scenario_count;
    READ_ATTR(in, scenario_count);

    ScenarioData scenario_datas[256] = {};
    for (u32 i = 0; i < scenario_count; ++i) {
        scenario_datas[i].deserialize(in);
    }

    u32 connected_area_count;
    READ_ATTR(in, connected_area_count);

    AreaData connected_area_datas[256] = {};
    for (u32 i = 0; i < connected_area_count; ++i) {
        connected_area_datas[i].deserialize(in);
    }

    Stage::ShineAreaInfo *scene_info =
        new Stage::ShineAreaInfo(logical_scene_id, shine_select_pane_id);

    for (u32 i = 0; i < scenario_count; ++i) {
        const ScenarioData &scenario = scenario_datas[i];
        if (scenario.m_is_ex_scenario) {
            // EX Scenarios are scenarios that are secret. (100 coin shine, red coin missions for
            // secret courses)
            scene_info->addExScenario(scenario.m_shine_id, scenario.m_bmg_name_index);
        } else {
            scene_info->addScenario(scenario.m_shine_id, scenario.m_bmg_name_index);
        }
    }

    Stage::registerShineStage(scene_info);

    for (u32 i = 0; i < connected_area_count; ++i) {
        const AreaData &area_data = connected_area_datas[i];
        if (area_data.m_is_ex_area) {
            // EX Areas are usually the secret courses themselves, and have just 1 episode entry
            // that connects to a shine
            Stage::registerExStage(area_data.m_area_id, logical_scene_id, area_data.m_ex_shine_id);
        } else {
            Stage::registerNormalStage(area_data.m_area_id, logical_scene_id);
        }
    }
};

#undef READ_ATTR
