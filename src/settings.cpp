#include <Dolphin/CARD.h>
#include <Dolphin/VI.h>
#include <Dolphin/ctype.h>
#include <Dolphin/mem.h>
#include <Dolphin/string.h>
#include <Dolphin/types.h>

#include <JSystem/J2D/J2DPicture.hxx>
#include <JSystem/J2D/J2DTextBox.hxx>
#include <JSystem/JDrama/JDRCamera.hxx>
#include <JSystem/JDrama/JDRDStage.hxx>
#include <JSystem/JDrama/JDRDStageGroup.hxx>
#include <JSystem/JDrama/JDRScreen.hxx>
#include <JSystem/JDrama/JDRViewObjPtrListT.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <JSystem/JUtility/JUTRect.hxx>
#include <JSystem/JUtility/JUTTexture.hxx>

#include <SMS/Camera/CubeManagerBase.hxx>
#include <SMS/GC2D/SMSFader.hxx>
#include <SMS/MSound/MSBGM.hxx>
#include <SMS/MSound/MSound.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Manager/FlagManager.hxx>
#include <SMS/Manager/RumbleManager.hxx>
#include <SMS/MarioUtil/DrawUtil.hxx>
#include <SMS/MarioUtil/gd-reinit-gx.hxx>
#include <SMS/System/Application.hxx>
#include <SMS/System/CardManager.hxx>
#include <SMS/System/Resolution.hxx>

#include "libs/constmath.hxx"
#include "libs/container.hxx"
#include "libs/global_vector.hxx"
#include "libs/string.hxx"
#include "module.hxx"
#include "settings.hxx"

#include "p_icons.hxx"
#include "p_module.hxx"
#include "p_settings.hxx"
#include <libs/scoped_ptr.hxx>

#define BETTER_SMS_CARD_ERROR_VERSION_MISMATCH (s32)(-1)

struct SettingMetaInfo {
    const char *mID;
    bool mIsUnlocked;
};

static TGlobalVector<SettingMetaInfo> sNewUnlockMap;

#define CARD_MAX_BLOCKS 8

static SMS_ALIGN(32) char sCardSysArea[CARD_WORKAREA];
static SMS_ALIGN(32) char sCardBuffer[CARD_BLOCKS_TO_BYTES(CARD_MAX_BLOCKS)];

static bool sIsMounted = false;
static s32 sChannel    = 0;

static Settings::SettingsWidgetInitCallback sWidgetsInit[16];
static u8 sWidgetCount = 0;

static void detachCallback_(s32 channel, s32 res) { sIsMounted = false; }

BETTER_SMS_FOR_EXPORT const char *Settings::getGroupName(const Settings::SettingsGroup &group) {
    if (!group.mModule)
        return "Super Mario Sunshine";
    return group.mModule->mName;
}

BETTER_SMS_FOR_EXPORT s32 Settings::mountCard() {
    sIsMounted = true;

    s32 check;

    check = CARDCheck(CARD_SLOTA);
    if (check == CARD_ERROR_READY) {
        sChannel = CARD_SLOTA;
        return check;
    } else if (check == CARD_ERROR_UNLOCKED) {
        sChannel = CARD_SLOTA;
        goto wait_for_unlock;
    }

    check = CARDCheck(CARD_SLOTB);
    if (check == CARD_ERROR_READY) {
        sChannel = CARD_SLOTB;
        return check;
    } else if (check == CARD_ERROR_UNLOCKED) {
        sChannel = CARD_SLOTA;
        goto wait_for_unlock;
    }

    check = CARDMount(CARD_SLOTA, sCardSysArea, detachCallback_);
    if (check == CARD_ERROR_READY || check == CARD_ERROR_UNLOCKED) {
        sChannel = CARD_SLOTA;
        goto wait_for_unlock;
    }

    check = CARDMount(CARD_SLOTB, sCardSysArea, detachCallback_);
    if (check == CARD_ERROR_READY || check == CARD_ERROR_UNLOCKED) {
        sChannel = CARD_SLOTB;
        goto wait_for_unlock;
    }

    sIsMounted = false;
    return check;

wait_for_unlock:
    check = CARDCheck(sChannel);
    while (check == CARD_ERROR_UNLOCKED) {
        //__CARDSync(sChannel);
        check = CARDCheck(sChannel);
    }
    sIsMounted = check == CARD_ERROR_READY;
    return check;
}

BETTER_SMS_FOR_EXPORT s32 Settings::unmountCard() {
    sIsMounted = false;
    return CARDUnmount(sChannel);
}

BETTER_SMS_FOR_EXPORT s32 Settings::saveSettingsGroup(Settings::SettingsGroup &group) {
    if (!group.isIOValid()) {
        return CARD_ERROR_READY;
    }

    CARDFileInfo finfo;
    s32 ret = OpenSavedSettings(group, finfo, true);
    if (ret < CARD_ERROR_READY) {
        CloseSavedSettings(group, &finfo);
        return ret;
    }

    ret = UpdateSavedSettings(group, &finfo);

    if (ret == CARD_ERROR_READY)
        OSReport("Saved settings for module \"%s\"!\n", Settings::getGroupName(group));
    else {
        OSReport("Failed to save settings for module \"%s\"!\n", Settings::getGroupName(group));
        return ret;
    }

    return CloseSavedSettings(group, &finfo);
}

BETTER_SMS_FOR_EXPORT s32 Settings::loadSettingsGroup(Settings::SettingsGroup &group) {
    if (!group.isIOValid()) {
        return CARD_ERROR_READY;
    }

    const bool isEmulator = BetterSMS::isGameEmulated();

    CARDFileInfo finfo;

    int ret = OpenSavedSettings(group, finfo, false);
    if (ret >= CARD_ERROR_READY) {
        // If this returns BROKEN, the save file is desynced by version and should be reset
        ret = ReadSavedSettings(group, &finfo);
        if (ret == BETTER_SMS_CARD_ERROR_VERSION_MISMATCH) {
            if (isEmulator) {
                OSPanic(__FILE__, __LINE__,
                        "Failed to load settings for module \"%s\"! (VERSION MISMATCH)\n\n"
                        "Automatically resetting to defaults...",
                        Settings::getGroupName(group));
            }
            ret = UpdateSavedSettings(group, &finfo);
        } else if (ret < CARD_ERROR_READY) {
            if (isEmulator) {
                OSPanic(__FILE__, __LINE__,
                        "Card error occured on module settings load! Make sure your memory card is "
                        "correctly configured in your emulator settings...",
                        Settings::getGroupName(group), ret);
            }

            CloseSavedSettings(group, &finfo);
            return ret;
        }

        for (auto &setting : group.getSettings()) {
            setting->emit();
            sNewUnlockMap.push_back(
                {setting->getName(), setting->isUnlocked() && setting->isUserEditable()});
        }

        return CloseSavedSettings(group, &finfo);
    }

    return ret;
}

BETTER_SMS_FOR_EXPORT bool Settings::saveAllSettings() {
    TGlobalVector<Settings::SettingsGroup *> groups;
    getSettingsGroups(groups);

    for (auto &group : groups) {
        if (saveSettingsGroup(*group) < CARD_ERROR_READY)
            return false;
    }

    return true;
}

BETTER_SMS_FOR_EXPORT bool Settings::loadAllSettings() {
    TGlobalVector<Settings::SettingsGroup *> groups;
    getSettingsGroups(groups);

    for (auto &group : groups) {
        if (loadSettingsGroup(*group) < CARD_ERROR_READY)
            return false;
    }

    return true;
}

u8 BetterSMS::Settings::registerWidget(SettingsWidgetInitCallback cb) {
    u8 newId            = sWidgetCount;
    sWidgetsInit[newId] = cb;
    sWidgetCount++;
    return newId;
}

#define DISK_GAME_ID (void *)0x80000000

using namespace BetterSMS;

static Settings::SettingsGroup sSunshineSettingsGroup = {1, 0, Settings::Priority::CORE};
static RumbleSetting sRumbleSetting("Controller Rumble");
static SoundSetting sSoundSetting("Sound Mode");
static SubtitleSetting sSubtitleSetting("Movie Subtitles");

// PRIVATE

void getSettingsGroups(TGlobalVector<Settings::SettingsGroup *> &out) {
    TGlobalVector<Settings::SettingsGroup *> tempCore;
    TGlobalVector<Settings::SettingsGroup *> tempGame;
    TGlobalVector<Settings::SettingsGroup *> tempMode;

    for (auto &item : gModuleInfos) {
        Settings::SettingsGroup *group = item.mSettings;
        if (strcmp(item.mName, "Better Sunshine Engine") == 0) {
            tempCore.insert(tempCore.begin(), group);
            continue;
        }

        switch (group->getPriority()) {
        case Settings::Priority::CORE:
            tempCore.insert(tempCore.end(), group);
            break;
        case Settings::Priority::GAME:
            tempGame.insert(tempGame.end(), group);
            break;
        case Settings::Priority::MODE:
            tempMode.insert(tempMode.end(), group);
            break;
        }
    }

    for (auto &item : tempCore) {
        out.insert(out.end(), item);
    }

    for (auto &item : tempGame) {
        out.insert(out.end(), item);
    }

    for (auto &item : tempMode) {
        out.insert(out.end(), item);
    }
}

BETTER_SMS_FOR_CALLBACK void initAllSettings(TApplication *app) {
    sSunshineSettingsGroup.addSetting(&sRumbleSetting);
    sSunshineSettingsGroup.addSetting(&sSoundSetting);
    sSunshineSettingsGroup.addSetting(&sSubtitleSetting);

    const bool isEmulator = BetterSMS::isGameEmulated();

    InitCard();
    if (Settings::mountCard() < CARD_ERROR_READY) {
        if (isEmulator) {
            OSPanic(__FILE__, __LINE__,
                    "Failed to mount memory card for loading module settings!\n\n"
                    "Automatically resetting to defaults...");
        }
        // return;
    }

    for (BetterSMS::ModuleInfo &init : gModuleInfos) {
        Settings::SettingsGroup *settingsGroup = init.mSettings;
        if (!settingsGroup)  // No settings registered
            continue;

        Settings::loadSettingsGroup(*settingsGroup);
    }

    Settings::unmountCard();
}

void InitCard() { CARDInit(); }

s32 OpenSavedSettings(Settings::SettingsGroup &group, CARDFileInfo &infoOut, bool canCreate) {
    auto &info = group.getSaveInfo();

    // Create and open save file for this group
    char normalizedPath[32];
    for (int i = 0; i < 32; ++i) {
        if (info.mSaveName[i] == ' ')
            normalizedPath[i] = '_';
        else
            normalizedPath[i] = tolower(info.mSaveName[i]);
    }

    if (info.mSaveGlobal)
        __CARDSetDiskID(&info.mGameCode);

    s32 ret = CARDOpen(sChannel, normalizedPath, &infoOut);
    while (ret == CARD_ERROR_BUSY) {
        ret = CARDCheck(sChannel);
    }

    if (ret == CARD_ERROR_NOFILE && canCreate) {
        s32 cret =
            CARDCreate(sChannel, normalizedPath, CARD_BLOCKS_TO_BYTES(info.mBlocks), &infoOut);
        // OSReport("Result (CREATE): %d\n", cret);
        if (cret < CARD_ERROR_READY) {
            if (info.mSaveGlobal)
                __CARDSetDiskID(DISK_GAME_ID);
            return cret;
        }
        UpdateSavedSettings(group, &infoOut);
    } else if (ret < CARD_ERROR_READY) {
        if (info.mSaveGlobal)
            __CARDSetDiskID(DISK_GAME_ID);
        return ret;
    }

    // We now have an open handle to the settings file
    return CARD_ERROR_READY;
}

s32 UpdateSavedSettings(Settings::SettingsGroup &group, CARDFileInfo *finfo) {
    Settings::SettingsSaveInfo &info = group.getSaveInfo();
    if (info.mBlocks > CARD_MAX_BLOCKS) {
        OSReport("Failed to save settings for module \"%s\"! (TOO MANY BLOCKS: > " SMS_STRINGIZE(
                     CARD_MAX_BLOCKS) ")\n",
                 Settings::getGroupName(group));
        return CARD_ERROR_CANCELED;
    }

    {
        CARDStat fstatus;

        // Work out status
        int statusRet = CARDGetStatus(finfo->mChannel, finfo->mFileNo, &fstatus);
        if (statusRet < CARD_ERROR_READY)
            return statusRet;

        fstatus.mGameCode = info.mGameCode;
        fstatus.mCompany  = info.mCompany;
        CARDSetBannerFmt(&fstatus, info.mBannerFmt);
        CARDSetIconAddr(&fstatus, CARD_DIRENTRY_SIZE);
        CARDSetCommentAddr(&fstatus, 4);
        for (s32 i = 0; i < info.mIconCount; ++i) {
            CARDSetIconFmt(&fstatus, i, info.mIconFmt);
            CARDSetIconSpeed(&fstatus, i, info.mIconSpeed);
        }
        fstatus.mLastModified = OSTicksToSeconds(OSGetTime());

        CARDSetStatus(finfo->mChannel, finfo->mFileNo, &fstatus);
    }

    const size_t saveDataSize = CARD_BLOCKS_TO_BYTES(info.mBlocks);
    {

        // Reset data
        memset(sCardBuffer, 0, saveDataSize);

        // Write version info
        sCardBuffer[0] = group.getMajorVersion();
        sCardBuffer[1] = group.getMinorVersion();

        // Copy group name into save data
        snprintf(sCardBuffer + 4, 32, "%s", info.mSaveName);

        // Copy date saved into save data
        {
            OSCalendarTime calendar;
            OSTicksToCalendarTime(OSGetTime(), &calendar);
            snprintf(sCardBuffer + 36, 32, "Module Info (%lu/%lu/%lu)", calendar.mon + 1,
                     calendar.mday, calendar.year);
        }

        memcpy(sCardBuffer + CARD_DIRENTRY_SIZE,
               reinterpret_cast<const u8 *>(info.mBannerImage) + info.mBannerImage->mTextureOffset,
               0xE00);
        memcpy(sCardBuffer + CARD_DIRENTRY_SIZE + 0xE00,
               reinterpret_cast<const u8 *>(info.mIconTable) + info.mIconTable->mTextureOffset,
               0x500 * info.mIconCount);

        size_t dataPosOut = CARD_DIRENTRY_SIZE + 0xE00 + (0x500 * info.mIconCount);

        // Write contents to save file
        JSUMemoryOutputStream out(sCardBuffer + dataPosOut, saveDataSize - dataPosOut);
        for (auto &setting : group.getSettings()) {
            setting->save(out);
        }

        for (size_t i = 0; i < saveDataSize; i += CARD_BLOCKS_TO_BYTES(1)) {
            s32 result = CARDWrite(finfo, sCardBuffer, CARD_BLOCKS_TO_BYTES(1), i);
            while (result == CARD_ERROR_BUSY) {
                result = CARDCheck(finfo->mChannel);
            }
            // OSReport("Result (WRITE): %d\n", result);
            if (result < CARD_ERROR_READY) {
                return result;
            }
        }
    }

    return CARD_ERROR_READY;
}

s32 ReadSavedSettings(Settings::SettingsGroup &group, CARDFileInfo *finfo) {
    auto &info = group.getSaveInfo();
    if (info.mBlocks > CARD_MAX_BLOCKS) {
        OSReport("Failed to load settings for module \"%s\"! (TOO MANY BLOCKS: > " SMS_STRINGIZE(
                     CARD_MAX_BLOCKS) ")\n",
                 Settings::getGroupName(group));
        return CARD_ERROR_CANCELED;
    }

    const size_t saveDataSize = CARD_BLOCKS_TO_BYTES(info.mBlocks);
    {
        // Reset data
        memset(sCardBuffer, 0, saveDataSize);

        for (size_t i = 0; i < saveDataSize; i += CARD_BLOCKS_TO_BYTES(1)) {
            s32 result = CARDRead(finfo, sCardBuffer, CARD_BLOCKS_TO_BYTES(1), i);
            while (result == CARD_ERROR_BUSY) {
                result = CARDCheck(finfo->mChannel);
            }
            // OSReport("Result (READ): %d\n", result);
            if (result < CARD_ERROR_READY) {
                return result;
            }
        }

        if (sCardBuffer[0] != group.getMajorVersion()) {
            return BETTER_SMS_CARD_ERROR_VERSION_MISMATCH;
        }

        size_t dataPosOut = CARD_DIRENTRY_SIZE + 0xE00 + (0x500 * info.mIconCount);

        // Write contents to save file
        JSUMemoryInputStream in(sCardBuffer + dataPosOut, saveDataSize - dataPosOut);
        for (auto &setting : group.getSettings()) {
            setting->load(in);
        }
    }

    return CARD_ERROR_READY;
}

s32 CloseSavedSettings(const Settings::SettingsGroup &group, CARDFileInfo *finfo) {
    auto &info = group.getSaveInfo();
    if (info.mSaveGlobal)
        __CARDSetDiskID(&info.mGameCode);
    s32 ret = CARDClose(finfo);
    if (info.mSaveGlobal)
        __CARDSetDiskID(DISK_GAME_ID);
    return ret;
}

static TCardBookmarkInfo sBookMarkInfo;

s32 SaveAllSettings() {
    const bool isEmulator = BetterSMS::isGameEmulated();

    {
        gpCardManager->getBookmarkInfos(&sBookMarkInfo);
        while (gpCardManager->mCommand == TCardManager::GETBOOKMARKS) {
            // Wait for save to finish
            OSYieldThread();
        }

        if (s32 status = gpCardManager->getLastStatus()) {
            if (isEmulator) {
                OSPanic(__FILE__, __LINE__,
                        "Failed to get bookmark info for autosave! (Status: %d)\nMake sure "
                        "your memory "
                        "card is okay "
                        "and that you haven't loaded a savestate that was made before your "
                        "last save!",
                        status);
            }
            return status;
        }

        JSUMemoryOutputStream out(nullptr, 0);
        gpCardManager->getWriteStream(&out);
        TFlagManager::smInstance->save(out);
        gpCardManager->writeBlock(gpApplication.mCurrentSaveBlock);  // This is the block being used
        while (gpCardManager->mCommand == TCardManager::SAVEBLOCK) {
            // Wait for save to finish
            OSYieldThread();
        }

        if (s32 status = gpCardManager->getLastStatus()) {
            if (isEmulator) {
                OSPanic(__FILE__, __LINE__,
                        "Failed to save block for autosave! (Status: %d)\nMake sure your "
                        "memory card "
                        "is okay and that "
                        "you haven't loaded a savestate that was made before your last save!",
                        status);
            }
            return status;
        }

        gpCardManager->unmount();
        TFlagManager::smInstance->saveSuccess();
    }

    {
        s32 cardStatus = Settings::mountCard();
        {
            if (cardStatus < CARD_ERROR_READY) {
                if (isEmulator) {
                    OSPanic(__FILE__, __LINE__,
                            "Failed to mount memory card for autosave! (Status: %d)\nMake sure "
                            "your "
                            "memory card is okay and that you haven't loaded a savestate that "
                            "was made "
                            "before your last save!",
                            cardStatus);

                    gpCardManager->mount_(true);
                }
                return cardStatus;
            }
            Settings::saveAllSettings();
            Settings::unmountCard();
        }

        gpCardManager->mount_(true);
    }

    {
        JSUMemoryOutputStream out(nullptr, 0);
        gpCardManager->getOptionWriteStream(&out);
        TFlagManager::smInstance->saveOption(out);
        gpCardManager->writeOptionBlock();

        OSReport("Last Status (Option Save): %d\n", gpCardManager->getLastStatus());
    }

    return CARD_ERROR_READY;
}

SettingsDirector::~SettingsDirector() { gpMSound->exitStage(); }

s32 SettingsDirector::direct() {
    s32 ret = 1;

    int *joinBuf[2];

    TSMSFader *fader = gpApplication.mFader;
    if (fader->mFadeStatus == TSMSFader::FADE_OFF) {
        mSettingScreen->mController->mState._06        = true;  // Disable camera processing
        mSettingScreen->mController->mState.mReadInput = true;
    }

    if (mState == State::INIT) {
        if (!OSIsThreadTerminated(&gSetupThread))
            return 0;
        OSJoinThread(&gSetupThread, (void **)joinBuf);

        fader->startFadeinT(0.3f);

        gpMSound->initSound();
        gpMSound->enterStage(MS_WAVE_DOLPIC, 1, 2);
        MSBgm::startBGM(BGM_UNDERGROUND);

        gpCardManager->unmount();

        mState = State::CONTROL;
        return 0;
    }

    TDirector::direct();

    switch (mState) {
    case State::INIT:
        break;
    case State::CONTROL: {
        mSettingScreen->mPerformFlags &= ~0b0001;  // Enable input by default;
        mSaveErrorPanel->mPerformFlags |= 0b1011;  // Disable view and input by default

        // Disable view and input by default
        for (int i = 0; i < 16; ++i) {
            if (mSettingsWidgets[i] == nullptr)
                continue;
            mSettingsWidgets[i]->mPerformFlags |= 0b1011;
        }
        break;
    }
    case State::CONTROL_SETTING:
        mSettingScreen->mPerformFlags |= 0b0001;   // Disable input
        mSaveErrorPanel->mPerformFlags |= 0b1011;  // Disable view and input
        // Disable view and input by default
        for (int i = 0; i < 16; ++i) {
            if (mSettingsWidgets[i] == nullptr)
                continue;
            mSettingsWidgets[i]->mPerformFlags |= 0b1011;
        }
        if (mActiveWidget != nullptr) {
            mActiveWidget->mPerformFlags &= ~0b1011;  // Enable view and input
            if (mDisappearingWidget) {
                if (!mActiveWidget->isAnimating()) {
                    mState              = State::CONTROL;
                    mActiveWidget       = nullptr;
                    mDisappearingWidget = false;
                }
            }
        }
        break;
    case State::SAVE_START:
        mSaveErrorPanel->appear();
        saveSettings();
        break;
    case State::SAVE_BUSY:
        mSettingScreen->mPerformFlags |= 0b0001;    // Disable input
        mSaveErrorPanel->mPerformFlags &= ~0b1011;  // Enable view and input
        // Disable view and input by default
        for (int i = 0; i < 16; ++i) {
            if (mSettingsWidgets[i] == nullptr)
                continue;
            mSettingsWidgets[i]->mPerformFlags |= 0b1011;
        }
        break;
    case State::SAVE_FAIL:
        [[fallthrough]];
    case State::SAVE_SUCCESS:
        mSaveErrorPanel->disappear();
        mState = State::EXIT;
        [[fallthrough]];
    case State::EXIT: {
        ret = exit();
        break;
    }
    }
    return ret;
}

bool SettingsDirector::switchToWidget(u8 widgetId) {
    Settings::SettingsWidget *widget = mSettingsWidgets[widgetId];
    if (widget != nullptr) {
        if (mActiveWidget != nullptr && widget != mActiveWidget) {
            mActiveWidget->disappear();
        }

        if (widget->isAnimating())
            return false;

        if (mSettingScreen->mCurrentSettingInfo == nullptr ||
            !mSettingScreen->mCurrentSettingInfo->mSettingData->isUserEditable()) {
            return false;
        }

        widget->mSettingRef = mSettingScreen->mCurrentSettingInfo->mSettingData;

        if (!widget->shouldAppear())
            return false;

        widget->appear();

        mActiveWidget = widget;
        mState        = State::CONTROL_SETTING;

        return true;
    }

    return false;
}

bool SettingsDirector::unloadWidget() {
    if (mActiveWidget != nullptr) {
        mActiveWidget->disappear();
        mDisappearingWidget = true;
        return true;
    }
    return false;
}

void SettingsDirector::setup(JDrama::TDisplay *display, TMarioGamePad *controller) {
    mViewObjStageGroup             = new JDrama::TDStageGroup(display);
    mDisplay                       = display;
    mController                    = controller;
    mController->mState.mReadInput = false;
    mController->mState._02        = true;
    mActiveWidget                  = nullptr;
    SMSRumbleMgr->reset();
    OSCreateThread(&gSetupThread, setupThreadFunc, this, gpSetupThreadStack + 0x10000, 0x10000, 17,
                   0);
    OSResumeThread(&gSetupThread);
}

void *SettingsDirector::setupThreadFunc(void *param) {
    auto *director = reinterpret_cast<SettingsDirector *>(param);
    director->initialize();
    return nullptr;
}

s32 SettingsDirector::exit() {
    TSMSFader *fader = gpApplication.mFader;
    if (fader->mFadeStatus == TSMSFader::FADE_OFF) {
        gpApplication.mFader->startFadeoutT(0.3f);
        gpCardManager->mount_(true);
    }
    return fader->mFadeStatus == TSMSFader::FADE_ON ? 5 : 1;
}

extern BugsSetting gBugFixesSetting;
bool BetterSMS::areBugsPatched() { return gBugFixesSetting.getBool(); }

extern BugsSetting gExploitFixesSetting;
bool BetterSMS::areExploitsPatched() { return gExploitFixesSetting.getBool(); }

extern BugsSetting gCollisionFixesSetting;
bool BetterSMS::isCollisionRepaired() { return gCollisionFixesSetting.getBool(); }

extern Settings::SwitchSetting gCameraInvertXSetting;
bool BetterSMS::isCameraInvertedX() { return gCameraInvertXSetting.getBool(); }

extern Settings::SwitchSetting gCameraInvertYSetting;
bool BetterSMS::isCameraInvertedY() { return gCameraInvertYSetting.getBool(); }

void SettingsDirector::initialize() {
    sRumbleSetting.setBool(TFlagManager::smInstance->getBool(0x90000));
    sSoundSetting.setInt(TFlagManager::smInstance->getFlag(0xA0000));
    sSubtitleSetting.setBool(TFlagManager::smInstance->getBool(0x90001));

    initializeDramaHierarchy();
    initializeSettingsLayout();
    initializeErrorLayout();

    JDrama::TViewObjPtrListT<JDrama::TViewObj> *group2D =
        (JDrama::TViewObjPtrListT<JDrama::TViewObj> *)mViewObjRoot->search("Group 2D");
    initializeSettingsWidgetLayouts(group2D);
}

void SettingsDirector::initializeDramaHierarchy() {
    auto *stageObjGroup = reinterpret_cast<JDrama::TDStageGroup *>(mViewObjStageGroup);
    auto *rootObjGroup  = new JDrama::TViewObjPtrListT<JDrama::TViewObj>("Root View Objs");
    mViewObjRoot        = rootObjGroup;

    JDrama::TRect screenRect{0, 0, SMSGetTitleRenderWidth(), SMSGetTitleRenderHeight()};

    auto *group2D = new JDrama::TViewObjPtrListT<JDrama::TViewObj>("Group 2D");
    {
        mSettingScreen = new SettingsScreen(mController);
        group2D->mViewObjList.insert(group2D->mViewObjList.end(), mSettingScreen);

        mSaveErrorPanel = new SaveErrorPanel(this, mController);
        mSaveErrorPanel->mPerformFlags |= 0b1011;  // Disable view and input by default
        group2D->mViewObjList.insert(group2D->mViewObjList.end(), mSaveErrorPanel);

        rootObjGroup->mViewObjList.insert(rootObjGroup->mViewObjList.end(), group2D);
    }

    {
        auto *group3D = new JDrama::TViewObjPtrListT<JDrama::TViewObj>("Group 3D");

        rootObjGroup->mViewObjList.insert(rootObjGroup->mViewObjList.end(), group3D);
        stageObjGroup->mViewObjList.insert(stageObjGroup->mViewObjList.end(), group3D);
    }

    {
        auto *group2DParticle = new JDrama::TViewObjPtrListT<JDrama::TViewObj>("Group 2D Particle");

        rootObjGroup->mViewObjList.insert(rootObjGroup->mViewObjList.end(), group2DParticle);
        stageObjGroup->mViewObjList.insert(stageObjGroup->mViewObjList.end(), group2DParticle);
    }

    {
        auto *stageDisp = new JDrama::TDStageDisp("<DStageDisp>", {0});

        auto *efbCtrl = stageDisp->getEfbCtrlDisp();
        efbCtrl->setSrcRect(screenRect);

        rootObjGroup->mViewObjList.insert(rootObjGroup->mViewObjList.end(), stageDisp);
        stageObjGroup->mViewObjList.insert(stageObjGroup->mViewObjList.end(), stageDisp);
    }

    {
        auto *screen = new JDrama::TScreen(screenRect, "Screen 2D");

        auto *orthoProj                = new JDrama::TOrthoProj();
        orthoProj->mProjectionField[0] = -BetterSMS::getScreenRatioAdjustX();
        orthoProj->mProjectionField[2] = 600.0f + BetterSMS::getScreenRatioAdjustX();
        screen->assignCamera(orthoProj);

        screen->assignViewObj(group2D);

        rootObjGroup->mViewObjList.insert(rootObjGroup->mViewObjList.end(), screen);
        stageObjGroup->mViewObjList.insert(stageObjGroup->mViewObjList.end(), screen);
    }
}

static size_t newlines(const char *buf) {
    size_t newlineCount = 0;
    for (size_t i = 0; buf[i] != '\0'; ++i) {
        if (buf[i] == '\n')
            newlineCount += 1;
    }
    return newlineCount;
}

void SettingsDirector::initializeSettingsLayout() {
    const int screenOrthoWidth   = BetterSMS::getScreenOrthoWidth();
    const int screenRenderWidth  = BetterSMS::getScreenRenderWidth();
    const int screenRenderHeight = 480;
    const int screenAdjustX      = BetterSMS::getScreenRatioAdjustX();

    mSettingScreen->mDirector = this;

    mSettingScreen->mScreen =
        new J2DScreen(8, 'ROOT', {0, 0, screenOrthoWidth, screenRenderHeight});

    JUTTexture *bg_texture      = new JUTTexture();
    bg_texture->mTexObj2.val[2] = 0;
    bg_texture->storeTIMG(GetResourceTextureHeader(gBricks));
    bg_texture->_50 = false;

    *(u16 *)((u32)bg_texture + 0x3C) = 64;
    *(u16 *)((u32)bg_texture + 0x3E) = 64;

    J2DPicture *screenBackground =
        new J2DPicture('snbg', {-screenAdjustX, 0, screenOrthoWidth, screenRenderHeight});

    screenBackground->insert(bg_texture, 0, 1.0f);
    screenBackground->mIsVisible    = true;
    screenBackground->mBinding      = 10;
    screenBackground->_134          = WrapRepeat;
    screenBackground->_138          = WrapRepeat;
    screenBackground->mRect         = {-screenAdjustX, 0, screenOrthoWidth, screenRenderHeight};
    screenBackground->mAlpha        = 64;
    screenBackground->mColorMask    = {0, 124, 141, 255};
    screenBackground->mColorOverlay = {0, 0, 0, 255};
    mSettingScreen->mScreen->mChildrenList.append(&screenBackground->mPtrLink);

    // Game settings

    int i = 0;
    TGlobalVector<Settings::SettingsGroup *> settingsGroups;
    getSettingsGroups(settingsGroups);

    settingsGroups.insert(settingsGroups.begin(), &sSunshineSettingsGroup);

    for (auto &group : settingsGroups) {
        auto *groupName = Settings::getGroupName(*group);

        J2DPane *groupPane =
            new J2DPane(19, ('p' << 24) | i, {0, 0, screenRenderWidth, screenRenderHeight});

        groupPane->mIsVisible = false;

        auto *groupInfo          = new GroupInfo();
        groupInfo->mGroupPane    = groupPane;
        groupInfo->mSettingGroup = group;

        int n = 0, ny = 0;
        for (auto &setting : group->getSettings()) {
            if (!setting->isUnlocked())
                continue;

            J2DPane *settingPane =
                new J2DPane(19, ('q' << 24) | i, {0, 0, screenRenderWidth, screenRenderHeight});

            J2DTextBox *settingKeyText = new J2DTextBox(
                ('t' << 24) | n, {20, 100 + (21 * ny), 300, 148 + (21 * ny)}, gpSystemFont->mFont,
                "", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);

            J2DTextBox *settingKeyTextBehind = new J2DTextBox(
                ('c' << 24) | n, {22, 102 + (21 * ny), 302, 150 + (21 * ny)}, gpSystemFont->mFont,
                "", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);

            J2DTextBox *settingValueText = new J2DTextBox(
                ('s' << 24) | n, {320, 100 + (21 * ny), 600, 148 + (21 * ny)}, gpSystemFont->mFont,
                "", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);

            J2DTextBox *settingValueTextBehind = new J2DTextBox(
                ('b' << 24) | n, {322, 102 + (21 * ny), 602, 150 + (21 * ny)}, gpSystemFont->mFont,
                "", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);
            {
                char valueTextbuf[40];
                setting->getValueName(valueTextbuf);

                char *settingValueTextBuf = new char[100];
                memset(settingValueTextBuf, 0, 100);
                snprintf(settingValueTextBuf, 100, "[ %s ]", valueTextbuf);

                char *settingKeyTextBuf = new char[100];
                memset(settingKeyTextBuf, 0, 100);
                snprintf(settingKeyTextBuf, 100, "%s", setting->getName());

                const u8 alpha = setting->isUserEditable() ? 255 : 210;
                const u8 color = setting->isUserEditable() ? 255 : 140;

                settingKeyText->mStrPtr         = settingKeyTextBuf;
                settingKeyText->mCharSizeX      = 20;
                settingKeyText->mCharSizeY      = 18;
                settingKeyText->mNewlineSize    = 18;
                settingKeyText->mGradientBottom = {color, color, color, alpha};
                settingKeyText->mGradientTop    = {color, color, color, alpha};

                settingKeyTextBehind->mStrPtr         = settingKeyTextBuf;
                settingKeyTextBehind->mCharSizeX      = 20;
                settingKeyTextBehind->mCharSizeY      = 18;
                settingKeyTextBehind->mNewlineSize    = 18;
                settingKeyTextBehind->mGradientBottom = {0, 0, 0, alpha};
                settingKeyTextBehind->mGradientTop    = {0, 0, 0, alpha};

                settingPane->mChildrenList.append(&settingKeyTextBehind->mPtrLink);
                settingPane->mChildrenList.append(&settingKeyText->mPtrLink);

                settingValueText->mStrPtr         = settingValueTextBuf;
                settingValueText->mCharSizeX      = 20;
                settingValueText->mCharSizeY      = 18;
                settingValueText->mNewlineSize    = 18;
                settingValueText->mGradientBottom = {color, color, color, alpha};
                settingValueText->mGradientTop    = {color, color, color, alpha};

                settingValueTextBehind->mStrPtr         = settingValueTextBuf;
                settingValueTextBehind->mCharSizeX      = 20;
                settingValueTextBehind->mCharSizeY      = 18;
                settingValueTextBehind->mNewlineSize    = 18;
                settingValueTextBehind->mGradientBottom = {0, 0, 0, alpha};
                settingValueTextBehind->mGradientTop    = {0, 0, 0, alpha};

                settingPane->mChildrenList.append(&settingValueTextBehind->mPtrLink);
                settingPane->mChildrenList.append(&settingValueText->mPtrLink);

                ny += newlines(settingValueTextBuf) + 1;
            }
            groupPane->mChildrenList.append(&settingPane->mPtrLink);

            auto *settingInfo                = new SettingInfo();
            settingInfo->mSettingTextBox     = settingValueText;
            settingInfo->mSettingTextBoxBack = settingValueTextBehind;
            settingInfo->mSettingData        = setting;
            groupInfo->mSettingInfos.insert(groupInfo->mSettingInfos.end(), settingInfo);

            n += 1;
        }

        mSettingScreen->mScreen->mChildrenList.append(&groupPane->mPtrLink);
        mSettingScreen->mGroups.insert(mSettingScreen->mGroups.end(), groupInfo);

        if (i == 0) {
            mSettingScreen->mCurrentGroupInfo   = groupInfo;
            mSettingScreen->mCurrentSettingInfo = mSettingScreen->getSettingInfo(0);
            mSettingScreen->mCurrentGroupInfo->mGroupPane->mIsVisible = true;
        }

        ++i;
    }

    // Layout
    {
        JUTTexture *mask      = new JUTTexture();
        mask->mTexObj2.val[2] = 0;
        mask->storeTIMG(GetResourceTextureHeader(gMaskBlack));
        mask->_50 = false;

        J2DPicture *maskTop    = new J2DPicture('mskt', {0, 0, 0, 0});
        J2DPicture *maskBottom = new J2DPicture('mskb', {0, 0, 0, 0});

        maskTop->insert(mask, 0, 1.0f);
        maskBottom->insert(mask, 0, 1.0f);

        maskTop->mRect    = {-screenAdjustX, 0, screenOrthoWidth, 90};
        maskBottom->mRect = {-screenAdjustX, screenRenderHeight - 90, screenOrthoWidth,
                             screenRenderHeight};

        maskTop->mAlpha    = 160;
        maskBottom->mAlpha = 160;

        maskTop->mColorOverlay    = {0, 0, 0, 255};
        maskBottom->mColorOverlay = {0, 0, 0, 255};

        maskTop->mVertexColors[0] = {0, 0, 0, 255};
        maskTop->mVertexColors[1] = {0, 0, 0, 255};
        maskTop->mVertexColors[2] = {0, 0, 0, 255};
        maskTop->mVertexColors[3] = {0, 0, 0, 255};

        maskBottom->mVertexColors[0] = {0, 0, 0, 255};
        maskBottom->mVertexColors[1] = {0, 0, 0, 255};
        maskBottom->mVertexColors[2] = {0, 0, 0, 255};
        maskBottom->mVertexColors[3] = {0, 0, 0, 255};

        mSettingScreen->mScreen->mChildrenList.append(&maskTop->mPtrLink);
        mSettingScreen->mScreen->mChildrenList.append(&maskBottom->mPtrLink);

        char *settingTextBuf = new char[100];
        memset(settingTextBuf, 0, 100);
        snprintf(settingTextBuf, 100, "Game Settings (1 / %ld)", settingsGroups.size());
        mSettingScreen->mGameSettingsTitle =
            new J2DTextBox('logo', {0, -10, 600, 80}, gpSystemFont->mFont, settingTextBuf,
                           J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
        mSettingScreen->mGameSettingsTitle->mCharSizeX   = 24;
        mSettingScreen->mGameSettingsTitle->mCharSizeY   = 24;
        mSettingScreen->mGameSettingsTitle->mNewlineSize = 24;
        mSettingScreen->mScreen->mChildrenList.append(
            &mSettingScreen->mGameSettingsTitle->mPtrLink);

        char *groupTitleTextBuf = new char[100];
        memset(groupTitleTextBuf, 0, 100);
        snprintf(groupTitleTextBuf, 100, "Super Mario Sunshine");
        mSettingScreen->mGroupTitle =
            new J2DTextBox('logo', {0, 20, 600, 110}, gpSystemFont->mFont, groupTitleTextBuf,
                           J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
        mSettingScreen->mGroupTitle->mCharSizeX   = 20;
        mSettingScreen->mGroupTitle->mCharSizeY   = 20;
        mSettingScreen->mGroupTitle->mNewlineSize = 20;
        mSettingScreen->mScreen->mChildrenList.append(&mSettingScreen->mGroupTitle->mPtrLink);

        J2DTextBox *exitLabel = new J2DTextBox(
            'exit',
            {static_cast<int>(20 - getScreenRatioAdjustX()), screenRenderHeight - 90,
             static_cast<int>(getScreenOrthoWidth() - 20), screenRenderHeight},
            gpSystemFont->mFont, "# Exit", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);
        mSettingScreen->mScreen->mChildrenList.append(&exitLabel->mPtrLink);

        J2DTextBox *moreLabel = new J2DTextBox(
            'more',
            {static_cast<int>(470 + getScreenRatioAdjustX()), screenRenderHeight - 90,
             static_cast<int>(getScreenOrthoWidth() - 20), screenRenderHeight},
            gpSystemFont->mFont, "@ More...", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);
        mSettingScreen->mScreen->mChildrenList.append(&moreLabel->mPtrLink);

        mSettingScreen->mPrevHint = new J2DTextBox(
            'prev',
            {static_cast<int>(20 - getScreenRatioAdjustX()), 0,
             static_cast<int>(getScreenOrthoWidth() - 20), 90},
            gpSystemFont->mFont, "< Prev", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);
        mSettingScreen->mScreen->mChildrenList.append(&mSettingScreen->mPrevHint->mPtrLink);

        mSettingScreen->mNextHint = new J2DTextBox(
            'next',
            {static_cast<int>(470 + getScreenRatioAdjustX()), 0,
             static_cast<int>(getScreenOrthoWidth() - 20), 90},
            gpSystemFont->mFont, "Next >", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);
        mSettingScreen->mScreen->mChildrenList.append(&mSettingScreen->mNextHint->mPtrLink);

        char *descriptionTextBufShadow = new char[200];
        memset(descriptionTextBufShadow, 0, 200);
        snprintf(descriptionTextBufShadow, 200, " ");
        mSettingScreen->mDescriptionShadow =
            new J2DTextBox('dess',
                           {static_cast<int>(600.0f / 2.0f - 150.0f - getScreenRatioAdjustX() + 2),
                            screenRenderHeight - 120 + 2,
                            static_cast<int>(600.0f / 2.0f + 150.0f + getScreenRatioAdjustX() + 2),
                            screenRenderHeight + 2},
                           gpSystemFont->mFont, descriptionTextBufShadow,
                           J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
        mSettingScreen->mDescriptionShadow->mCharSizeX      = 18;
        mSettingScreen->mDescriptionShadow->mCharSizeY      = 18;
        mSettingScreen->mDescriptionShadow->mNewlineSize    = 18;
        mSettingScreen->mDescriptionShadow->mGradientBottom = {0, 0, 0, 255};
        mSettingScreen->mDescriptionShadow->mGradientTop    = {0, 0, 0, 255};
        mSettingScreen->mScreen->mChildrenList.append(
            &mSettingScreen->mDescriptionShadow->mPtrLink);

        char *descriptionTextBuf = new char[200];
        memset(descriptionTextBuf, 0, 200);
        snprintf(descriptionTextBuf, 200, " ");

        mSettingScreen->mDescription =
            new J2DTextBox('desc',
                           {static_cast<int>(600.0f / 2.0f - 150.0f - getScreenRatioAdjustX()),
                            screenRenderHeight - 120,
                            static_cast<int>(600.0f / 2.0f + 150.0f + getScreenRatioAdjustX()),
                            screenRenderHeight},
                           gpSystemFont->mFont, descriptionTextBuf, J2DTextBoxHBinding::Center,
                           J2DTextBoxVBinding::Center);
        mSettingScreen->mDescription->mCharSizeX   = 18;
        mSettingScreen->mDescription->mCharSizeY   = 18;
        mSettingScreen->mDescription->mNewlineSize = 18;

        mSettingScreen->mScreen->mChildrenList.append(&mSettingScreen->mDescription->mPtrLink);
    }
}

static char sErrorTag[64];

void SettingsDirector::initializeErrorLayout() {
    const int screenOrthoWidth   = BetterSMS::getScreenOrthoWidth();
    const int screenRenderWidth  = BetterSMS::getScreenRenderWidth();
    const int screenRenderHeight = 480;
    const int screenAdjustX      = BetterSMS::getScreenRatioAdjustX();

    mSaveErrorPanel->mDirector = this;

    mSaveErrorPanel->mScreen =
        new J2DScreen(8, 'ROOT', {0, 0, screenOrthoWidth, screenRenderHeight});
    {
        JUTTexture *mask      = new JUTTexture();
        mask->mTexObj2.val[2] = 0;
        mask->storeTIMG(GetResourceTextureHeader(gMaskBlack));
        mask->_50 = false;

        J2DPane *rootPane = new J2DPane(19, 'root', {0, 0, 400, 280});
        mSaveErrorPanel->mScreen->mChildrenList.append(&rootPane->mPtrLink);

        mSaveErrorPanel->mAnimatedPane        = new TBoundPane(rootPane, {0, 0, 400, 280});
        mSaveErrorPanel->mAnimatedPane->mPane = rootPane;

        J2DPicture *maskPanel = new J2DPicture('mask', {0, 0, 0, 0});
        {
            maskPanel->insert(mask, 0, 1.0f);
            maskPanel->mRect            = {0, 0, 400, 280};
            maskPanel->mAlpha           = 210;
            maskPanel->mColorOverlay    = {0, 0, 0, 255};
            maskPanel->mVertexColors[0] = {20, 0, 0, 255};
            maskPanel->mVertexColors[1] = {20, 0, 0, 255};
            maskPanel->mVertexColors[2] = {20, 0, 0, 255};
            maskPanel->mVertexColors[3] = {20, 0, 0, 255};
        }
        rootPane->mChildrenList.append(&maskPanel->mPtrLink);

        mSaveErrorPanel->mErrorHandlerPane             = new J2DPane(19, 'err_', {0, 0, 400, 280});
        mSaveErrorPanel->mErrorHandlerPane->mIsVisible = false;
        {
            mSaveErrorPanel->mErrorTextBox =
                new J2DTextBox('errl', {12, 16, 388, 40}, gpSystemFont->mFont, "",
                               J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Top);
            {
                mSaveErrorPanel->mErrorTextBox->mStrPtr         = sErrorTag;
                mSaveErrorPanel->mErrorTextBox->mCharSizeX      = 21;
                mSaveErrorPanel->mErrorTextBox->mCharSizeY      = 24;
                mSaveErrorPanel->mErrorTextBox->mGradientBottom = {160, 190, 20, 255};
                mSaveErrorPanel->mErrorTextBox->mGradientBottom = {160, 190, 20, 255};
            }
            mSaveErrorPanel->mErrorHandlerPane->mChildrenList.append(
                &mSaveErrorPanel->mErrorTextBox->mPtrLink);

            J2DTextBox *description = new J2DTextBox(
                'desc', {20, 50, 380, 230}, gpSystemFont->mFont,
                "Something went wrong when saving the settings.\nWould you like to try again?",
                J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
            {
                description->mCharSizeX = 21;
                description->mCharSizeY = 24;
            }
            mSaveErrorPanel->mErrorHandlerPane->mChildrenList.append(&description->mPtrLink);

            mSaveErrorPanel->mChoiceBoxes[0] =
                new J2DTextBox('exit', {80, 230, 202, 258}, gpSystemFont->mFont, "Exit",
                               J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Bottom);
            {
                mSaveErrorPanel->mChoiceBoxes[0]->mCharSizeX = 24;
                mSaveErrorPanel->mChoiceBoxes[0]->mCharSizeY = 24;
            }
            mSaveErrorPanel->mErrorHandlerPane->mChildrenList.append(
                &mSaveErrorPanel->mChoiceBoxes[0]->mPtrLink);

            mSaveErrorPanel->mChoiceBoxes[1] =
                new J2DTextBox('save', {250, 230, 392, 258}, gpSystemFont->mFont, "Retry",
                               J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Bottom);
            {
                mSaveErrorPanel->mChoiceBoxes[1]->mCharSizeX = 24;
                mSaveErrorPanel->mChoiceBoxes[1]->mCharSizeY = 24;
            }
            mSaveErrorPanel->mErrorHandlerPane->mChildrenList.append(
                &mSaveErrorPanel->mChoiceBoxes[1]->mPtrLink);
        }
        rootPane->mChildrenList.append(&mSaveErrorPanel->mErrorHandlerPane->mPtrLink);

        mSaveErrorPanel->mSaveTryingPane             = new J2DPane(19, 'save', {0, 0, 400, 280});
        mSaveErrorPanel->mSaveTryingPane->mIsVisible = true;
        {
            J2DTextBox *description = new J2DTextBox(
                'desc', {20, 50, 380, 230}, gpSystemFont->mFont, "Saving to the memory card...",
                J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
            {
                description->mCharSizeX = 21;
                description->mCharSizeY = 24;
            }
            mSaveErrorPanel->mSaveTryingPane->mChildrenList.append(&description->mPtrLink);
        }
        rootPane->mChildrenList.append(&mSaveErrorPanel->mSaveTryingPane->mPtrLink);
    }
}

void SettingsDirector::initializeSettingsWidgetLayouts(
    JDrama::TViewObjPtrListT<JDrama::TViewObj> *performList) {

    for (int i = 0; i < 16; ++i)
        mSettingsWidgets[i] = nullptr;

    TGlobalVector<Settings::SettingsGroup *> settingsGroups;
    getSettingsGroups(settingsGroups);

    settingsGroups.insert(settingsGroups.begin(), &sSunshineSettingsGroup);

    for (auto &group : settingsGroups) {
        for (auto &setting : group->getSettings()) {
            u8 widgetId = setting->getWidgetId();
            if (widgetId > sWidgetCount) {
                OSReport("Setting with invalid widget '%s', widgetId %d\n", setting->getName(),
                         widgetId);
                continue;
            }

            if (mSettingsWidgets[widgetId] != nullptr)
                continue;

            mSettingsWidgets[widgetId] = sWidgetsInit[widgetId]();
            mSettingsWidgets[widgetId]->initializeLayout(this, mController);
            mSettingsWidgets[widgetId]->mPerformFlags |=
                0b1011;  // Disable view and input by default
            performList->mViewObjList.insert(performList->mViewObjList.end(),
                                             mSettingsWidgets[widgetId]);
        }
    }
}

void *SettingsDirector::saveThreadFunc(void *data) {
    auto director = reinterpret_cast<SettingsDirector *>(data);
    director->saveSettings_();
    return nullptr;
}

void SettingsDirector::saveSettings() {
    OSCreateThread(&gSetupThread, saveThreadFunc, this, gpSetupThreadStack + 0x10000, 0x10000, 17,
                   0);
    OSResumeThread(&gSetupThread);
    mState = State::SAVE_BUSY;
}

void SettingsDirector::saveSettings_() {
    char statusBuf[40];
    char messageBuf[100];

    {
        gpCardManager->mount_(true);

        // Save base game settings (language, etc)
        sRumbleSetting.emit();
        sSubtitleSetting.emit();
        sSoundSetting.emit();

        {
            JSUMemoryOutputStream out(nullptr, 0);
            gpCardManager->getOptionWriteStream(&out);
            TFlagManager::smInstance->saveOption(out);
            gpCardManager->writeOptionBlock();
        }

        gpCardManager->unmount();
    }

    {
        s32 cardStatus = Settings::mountCard();

        if (cardStatus < CARD_ERROR_READY) {
            failSave(cardStatus);
            return;
        }

        CARDFileInfo finfo;

        TGlobalVector<Settings::SettingsGroup *> groups;
        getSettingsGroups(groups);

        Settings::saveAllSettings();

        Settings::unmountCard();
    }

    gpCardManager->mount_(true);

    mState     = State::SAVE_SUCCESS;
    mErrorCode = CARD_ERROR_READY;
    return;
}

void SettingsDirector::failSave(int errorcode) {
    Settings::unmountCard();
    mSaveErrorPanel->switchScreen();
    mErrorCode = errorcode;
    strncpy(sErrorTag, getErrorString(errorcode), 64);
    return;
}

const char *SettingsDirector::getErrorString(int errorcode) {
    switch (errorcode) {
    default:
    case CARD_ERROR_WRONGDEVICE:
        return "Unsupported Device";
    case CARD_ERROR_NOCARD:
        return "Memory Card Not Found";
    case CARD_ERROR_IOERROR:
        return "Memory Card Bad I/O";
    case CARD_ERROR_BROKEN:
        return "Memory Card Corrupted";
    case CARD_ERROR_FATAL_ERROR:
        return "Memory Card Fatal Error";
    }
}

static s32 checkForSettingsMenu(TMarDirector *director) {
    s32 ret = director->changeState();
    if (director->mAreaID == 15 && director->mEpisodeID == 0) {
        TSMSFader *fader = gpApplication.mFader;
        if (gpCubeCamera->getInCubeNo(*(Vec *)gpMarioPos) > 0) {
            if (fader->mFadeStatus == TSMSFader::FADE_OFF) {
                fader->startFadeoutT(0.4f);
            } else if (fader->mFadeStatus == TSMSFader::FADE_ON) {
                ret = 10;
            }
        }
    }
    return ret;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80299D0C, 0, 0, 0), checkForSettingsMenu);

namespace BetterSMS {
    namespace Settings {

        void SettingsWidget::initializeLayout(SettingsDirector *director,
                                              TMarioGamePad *controller) {
            mDirector                    = director;
            mController                  = controller;
            const int screenOrthoWidth   = BetterSMS::getScreenOrthoWidth();
            const int screenRenderWidth  = BetterSMS::getScreenRenderWidth();
            const int screenRenderHeight = 480;
            const int screenAdjustX      = BetterSMS::getScreenRatioAdjustX();

            mScreen = new J2DScreen(8, 'ROOT', {0, 0, screenOrthoWidth, screenRenderHeight});
            {
                JUTTexture *mask      = new JUTTexture();
                mask->mTexObj2.val[2] = 0;
                mask->storeTIMG(GetResourceTextureHeader(gMaskBlack));
                mask->_50 = false;

                J2DPane *rootPane = new J2DPane(19, 'root', {0, 0, 400, 280});
                mScreen->mChildrenList.append(&rootPane->mPtrLink);

                mAnimatedPane        = new TBoundPane(rootPane, {0, 0, 400, 280});
                mAnimatedPane->mPane = rootPane;

                J2DPicture *maskPanel = new J2DPicture('mask', {0, 0, 0, 0});
                {
                    maskPanel->insert(mask, 0, 1.0f);
                    maskPanel->mRect            = {0, 0, 400, 280};
                    maskPanel->mAlpha           = 210;
                    maskPanel->mColorOverlay    = {0, 0, 0, 255};
                    maskPanel->mVertexColors[0] = {20, 0, 0, 255};
                    maskPanel->mVertexColors[1] = {20, 0, 0, 255};
                    maskPanel->mVertexColors[2] = {20, 0, 0, 255};
                    maskPanel->mVertexColors[3] = {20, 0, 0, 255};
                }
                rootPane->mChildrenList.append(&maskPanel->mPtrLink);

                mSettingPane             = new J2DPane(19, 'sett', {0, 0, 400, 280});
                mSettingPane->mIsVisible = true;
                {
                    initializeContainer();

                    J2DTextBox *cancelText =
                        new J2DTextBox('cncl', {20, 250, 380, 270}, gpSystemFont->mFont, "# Cancel",
                                       J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);
                    {
                        cancelText->mCharSizeX = 21;
                        cancelText->mCharSizeY = 24;
                    }
                    mSettingPane->mChildrenList.append(&cancelText->mPtrLink);

                    J2DTextBox *applyText = new J2DTextBox(
                        'aply', {20, 250, 380, 270}, gpSystemFont->mFont, "@ Apply Changes",
                        J2DTextBoxHBinding::Right, J2DTextBoxVBinding::Center);
                    {
                        applyText->mCharSizeX = 21;
                        applyText->mCharSizeY = 24;
                    }
                    mSettingPane->mChildrenList.append(&applyText->mPtrLink);
                }
                rootPane->mChildrenList.append(&mSettingPane->mPtrLink);
            }
        }

        void SettingsWidget::perform(u32 flags, JDrama::TGraphics *graphics) {
            if ((flags & 0x1)) {
                processInput();
            }

            if ((flags & 0x8)) {
                ReInitializeGX();
                SMS_DrawInit();

                J2DOrthoGraph ortho(0, 0, BetterSMS::getScreenOrthoWidth(),
                                    SMSGetTitleRenderHeight());
                ortho.setup2D();

                mAnimatedPane->update();
                mScreen->draw(0, 0, &ortho);
            }
        };

        void SettingsWidget::appear() {
            const s32 midX = getScreenRenderWidth() / 2;
            mAnimatedPane->setPanePosition(5, {100, 480}, {100, 200}, {100, 98});
            mAnimatedPane->startAnimation();
        }

        void SettingsWidget::disappear() {
            const s32 midX = getScreenRenderWidth() / 2;
            mAnimatedPane->setPanePosition(5, {100, 98}, {100, 200}, {100, 480});
            mAnimatedPane->startAnimation();
        }

        int BetterSunshineEngineSettingsWidget::buildValue() const {
            int value = 0;
            for (size_t i = 0; i < 10; ++i) {
                value *= 10;
                value += mValue[i];
            }
            return value * (mIsNegative ? -1 : 1);
        }

        void BetterSunshineEngineSettingsWidget::applySetting() {
            if (!mSettingRef)
                return;

            mSettingRef->setInt(buildValue());
            mDirector->getSettingsScreen()->refreshCurrent();
        }

        void BetterSunshineEngineSettingsWidget::perform(u32 flags, JDrama::TGraphics *graphics) {
            SettingsWidget::perform(flags, graphics);
            if ((flags & 0x3)) {
                if (mSettingRef && buildValue() != mSettingRef->getInt()) {
                    mValueTextBox->mGradientTop    = {180, 230, 10, 255};
                    mValueTextBox->mGradientBottom = {240, 170, 10, 255};
                } else {
                    mValueTextBox->mGradientTop    = {255, 255, 255, 255};
                    mValueTextBox->mGradientBottom = {255, 255, 255, 255};
                }
            }
        }

        void BetterSunshineEngineSettingsWidget::processInput() {
            if (mDirector->getState() != SettingsDirector::State::CONTROL_SETTING) {
                return;
            }

            if (mSettingRef->getKind() != Settings::SingleSetting::ValueKind::INT) {
                mDirector->unloadWidget();
                return;
            }

            Settings::IntSetting *intSetting = static_cast<Settings::IntSetting *>(mSettingRef);

            if ((mController->mButtons.mFrameInput & TMarioGamePad::A)) {
                mDirector->unloadWidget();
                applySetting();
                return;
            } else if ((mController->mButtons.mFrameInput & TMarioGamePad::B)) {
                mDirector->unloadWidget();
                return;
            }

            // Calculate the bounds for each digit
            // based on value range of setting.
            const Settings::ValueRange<int> &range = intSetting->getValueRange();
            int minVal                             = range.mStart;
            int maxVal                             = range.mStop;

            int maxDigits = 0;
            int maxValCpy = maxVal;
            for (int i = 0; i < 10; ++i) {
                if (maxValCpy > 0) {
                    maxValCpy /= 10;
                    maxDigits++;
                }
            }

            int minDigits = 0;
            int minValCpy = minVal;
            for (int i = 0; i < 10; ++i) {
                if (minValCpy > 0) {
                    minValCpy /= 10;
                    minDigits++;
                }
            }

            int digits = Max(maxDigits, minDigits);

            // Process input
            {
                if ((mController->mButtons.mRapidInput &
                     (TMarioGamePad::DPAD_RIGHT | TMarioGamePad::MAINSTICK_RIGHT))) {
                    mDigitIndex = Min(mDigitIndex + 1, 9);
                }

                if ((mController->mButtons.mRapidInput &
                     (TMarioGamePad::DPAD_LEFT | TMarioGamePad::MAINSTICK_LEFT))) {
                    mDigitIndex = Max(mDigitIndex - 1, 10 - digits);
                }

                if ((mController->mButtons.mRapidInput &
                     (TMarioGamePad::DPAD_UP | TMarioGamePad::MAINSTICK_UP))) {
                    mValue[mDigitIndex] = (mValue[mDigitIndex] + 1) % 10;
                }

                if ((mController->mButtons.mRapidInput &
                     (TMarioGamePad::DPAD_DOWN | TMarioGamePad::MAINSTICK_DOWN))) {
                    mValue[mDigitIndex] = (mValue[mDigitIndex] + 9) % 10;
                }
            }

            char intMaxBounds[10] = {};
            char intMinBounds[10] = {};

            // Populate the max bounds
            for (int i = 9; i >= 0; --i) {
                intMaxBounds[i] = maxVal % 10;
                maxVal /= 10;
            }

            // Populate the min bounds
            for (int i = 9; i >= 0; --i) {
                intMinBounds[i] = minVal % 10;
                minVal /= 10;
            }

            // Clamp by max bounds
            {
                bool isClamping = true;
                for (int i = 10 - digits; i < 10 && isClamping; ++i) {
                    // >= to capture the case where the value is already at the max
                    if (mValue[i] >= intMaxBounds[i]) {
                        mValue[i] = intMaxBounds[i];
                    } else {
                        isClamping = false;
                    }
                }
            }

            // Clamp by min bounds
            {
                bool isClamping = true;
                for (int i = 10 - digits; i < 10 && isClamping; ++i) {
                    // <= to capture the case where the value is already at the min
                    if (mValue[i] <= intMinBounds[i]) {
                        mValue[i] = intMinBounds[i];
                    } else {
                        isClamping = false;
                    }
                }
            }

            char intWidths[10] = {14, 11, 13, 14, 13, 13, 13, 13, 13, 13};

            int width      = 0;
            int totalWidth = 0;

            for (int i = 10 - digits; i < mDigitIndex; ++i) {
                width += intWidths[mValue[i]];
                if ((i % 2) == 1) {
                    width += 1;
                }
            }

            for (int i = 10 - digits; i < 10; ++i) {
                totalWidth += intWidths[mValue[i]];
                if ((i % 2) == 1) {
                    totalWidth += 1;
                }
            }

            if (mIsNegative) {
                width += 14;
                totalWidth += 14;
            }

            int trueX = 200 - totalWidth / 2 + 2;
            int ofsX  = width;

            mDigitSelector->mRect.mX1 = trueX + ofsX;
            mDigitSelector->mRect.mX2 = mDigitSelector->mRect.mX1 + mDigitSelector->mCharSizeX + 10;

            char valueTextBuf[16] = {};
            for (int i = 0; i < digits; ++i) {
                valueTextBuf[i] = mValue[(10 - digits) + i] + '0';
            }

            mValueTextBox->setString(valueTextBuf);
        }

        void BetterSunshineEngineSettingsWidget::initializeContainer() {

            mSettingTextBox = new J2DTextBox('name', {12, 16, 388, 40}, gpSystemFont->mFont, "",
                                             J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Top);
            {
                mSettingTextBox->mStrPtr         = (char *)"UNKNOWN";
                mSettingTextBox->mCharSizeX      = 21;
                mSettingTextBox->mCharSizeY      = 24;
                mSettingTextBox->mGradientTop    = {190, 20, 160, 255};
                mSettingTextBox->mGradientBottom = {190, 20, 160, 255};
            }
            mSettingPane->mChildrenList.append(&mSettingTextBox->mPtrLink);

            mValueTextBox = new J2DTextBox('valu', {12, 40, 388, 240}, gpSystemFont->mFont, "",
                                           J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
            {
                mValueTextBox->mStrPtr         = (char *)"0000000000";
                mValueTextBox->mCharSizeX      = 18;
                mValueTextBox->mCharSizeY      = 21;
                mValueTextBox->mGradientTop    = {255, 255, 255, 255};
                mValueTextBox->mGradientBottom = {255, 255, 255, 255};
            }
            mSettingPane->mChildrenList.append(&mValueTextBox->mPtrLink);

            mDigitSelector = new J2DTextBox('slct', {134, 150, 200, 180}, gpSystemFont->mFont, "",
                                            J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Top);
            {
                mDigitSelector->mStrPtr         = (char *)"^";
                mDigitSelector->mCharSizeX      = 18;
                mDigitSelector->mCharSizeY      = 23;
                mDigitSelector->mGradientTop    = {20, 220, 20, 255};
                mDigitSelector->mGradientBottom = {20, 220, 20, 255};
            }
            mSettingPane->mChildrenList.append(&mDigitSelector->mPtrLink);
        }

        bool BetterSunshineEngineSettingsWidget::shouldAppear() {
            return mSettingRef->getKind() == Settings::SingleSetting::ValueKind::INT;
        }

        void BetterSunshineEngineSettingsWidget::appear() {
            SettingsWidget::appear();

            mSettingTextBox->setString(mSettingRef->getName());

            delete[] mValueTextBox->mStrPtr;
            mValueTextBox->mStrPtr = new char[50];
            mSettingRef->getValueName(mValueTextBox->mStrPtr);

            mValueTextBox->mGradientTop    = {255, 255, 255, 255};
            mValueTextBox->mGradientBottom = {255, 255, 255, 255};

            int value = mSettingRef->getInt();

            mIsNegative = value < 0;
            mDigitIndex = 9;

            int i = 9;

            while (value > 0) {
                mValue[i--] = value % 10;
                value /= 10;
            }

            for (; i >= 0; --i) {
                mValue[i] = 0;
            }
        }
    }  // namespace Settings
}  // namespace BetterSMS

/* UNLOCK NOTIFICATION */

static J2DScreen *sNotificationScreen;
static J2DTextBox *sNotificationBox;
static TGlobalVector<TGlobalString> sUnlockedSettings;

static OSTime sLastTime  = 0;
static int sVisualState  = 0;
static int sOnScreenTime = 0;

static char sNotifTextBuf[128];

// Settings template specialization for UnorderedMap
using setting_hash = JSystem::hash<BetterSMS::Settings::SingleSetting *>;

struct setting_equal_to : JSystem::binary_function<BetterSMS::Settings::SingleSetting *,
                                                   BetterSMS::Settings::SingleSetting *, bool> {
    inline constexpr auto operator()(BetterSMS::Settings::SingleSetting *a,
                                     BetterSMS::Settings::SingleSetting *b) -> decltype(auto) {
        return a == b;
    }
};

template <>
inline size_t setting_hash::operator()(BetterSMS::Settings::SingleSetting *v) const noexcept {
    auto uv = reinterpret_cast<u32>(v);
    uv      = ((uv >> 16) ^ uv) * 0x45d9f3b;
    uv      = ((uv >> 16) ^ uv) * 0x45d9f3b;
    uv      = (uv >> 16) ^ uv;
    return uv;
}
//

BETTER_SMS_FOR_CALLBACK void initUnlockedSettings(TApplication *app) {
    sLastTime = 0;

    sUnlockedSettings.clear();
    sNewUnlockMap.clear();

    memset(sNotifTextBuf, 0, 128);

    auto *oldHeap = JKRHeap::sRootHeap->becomeCurrentHeap();

    sNotificationScreen = new J2DScreen(8, 'ROOT', {0, 0, 600, 480});
    {
        sNotificationBox = new J2DTextBox(gpSystemFont->mFont, "");
        {
            sNotificationBox->mRect.set(100, 100, 500, 170);
            sNotificationBox->mAlpha       = 0;
            sNotificationBox->mCharSizeX   = 14;
            sNotificationBox->mCharSizeY   = 15;
            sNotificationBox->mNewlineSize = 15;
            sNotificationBox->mHBinding    = J2DTextBoxHBinding::Center;
            sNotificationBox->mVBinding    = J2DTextBoxVBinding::Center;
            sNotificationBox->mStrPtr      = sNotifTextBuf;
        }
        sNotificationScreen->mChildrenList.append(&sNotificationBox->mPtrLink);
    }

    oldHeap->becomeCurrentHeap();

    sVisualState = 0;
}

BETTER_SMS_FOR_CALLBACK void
checkForUnlockedSettings(const Settings::SettingsGroup &group,
                         TGlobalVector<Settings::SingleSetting *> &out) {
    SettingMetaInfo *info = nullptr;
    for (auto &setting : group.getSettings()) {
        for (size_t i = 0; i < sNewUnlockMap.size(); ++i) {
            if (strcmp(sNewUnlockMap[i].mID, setting->getName()) == 0) {
                info = &sNewUnlockMap[i];
                break;
            }
        }

        bool isSettingAccessible = setting->isUnlocked() && setting->isUserEditable();

        if (info == nullptr) {
            sNewUnlockMap.push_back({setting->getName(), isSettingAccessible});
            continue;
        }

        if (isSettingAccessible &&
            !info->mIsUnlocked) {  // This means the setting was just unlocked
            out.insert(out.begin(), setting);
            info->mIsUnlocked = true;
        }
    }
}

BETTER_SMS_FOR_CALLBACK void updateUnlockedSettings(TApplication *app) {
    TGlobalVector<Settings::SettingsGroup *> groups;
    getSettingsGroups(groups);

    for (auto &group : groups) {
        TGlobalVector<Settings::SingleSetting *> unlockedSettings;
        checkForUnlockedSettings(*group, unlockedSettings);

        for (auto &setting : unlockedSettings) {
            char notifbuf[128];
            snprintf(notifbuf, 100, "%s\n\nUnlocked the \"%s\" setting!",
                     Settings::getGroupName(*group), setting->getName());

            sUnlockedSettings.push_back(notifbuf);

            if (sUnlockedSettings.size() == 1) {
                strncpy(sNotificationBox->mStrPtr, notifbuf, 100);
            }
        }
    }
}

BETTER_SMS_FOR_CALLBACK void drawUnlockedSettings(TApplication *app, const J2DOrthoGraph *ortho) {
    if (sUnlockedSettings.size() == 0)
        return;

    ReInitializeGX();
    const_cast<J2DOrthoGraph *>(ortho)->setup2D();

    J2DFillBox(
        sNotificationBox->mRect,
        {30, 70, 230, lerp<u8>(0, 200, static_cast<f32>(sNotificationBox->mAlpha) / 255.0f)});

    sNotificationScreen->draw(0, 0, ortho);

    if (sVisualState == 0) {
        if (sNotificationBox->mAlpha == 0) {
            if (gpMSound->gateCheck(18497))
                MSoundSESystem::MSoundSE::startSoundSystemSE(18497, 0, nullptr, 0);
        }
        sNotificationBox->mAlpha = Min(sNotificationBox->mAlpha + 10, 255);
        if (sNotificationBox->mAlpha == 255)
            sVisualState = 1;
    } else if (sVisualState == 1) {
        sOnScreenTime += 1;
        if (sOnScreenTime > 240) {
            sVisualState  = 2;
            sOnScreenTime = 0;
        }
    } else {
        sNotificationBox->mAlpha = Max(sNotificationBox->mAlpha - 10, 0);
        if (sNotificationBox->mAlpha == 0) {
            sUnlockedSettings.erase(sUnlockedSettings.begin());
            if (sUnlockedSettings.size() > 0) {
                auto notif = sUnlockedSettings.begin();
                strncpy(sNotificationBox->mStrPtr, notif->c_str(), 100);
            }
            sVisualState = 0;
        }
    }
}

#undef DISK_GAME_ID