#pragma once

#include <JDrama/JDRCamera.hxx>
#include <JDrama/JDRDStageGroup.hxx>
#include <JDrama/JDRDirector.hxx>
#include <JDrama/JDRDisplay.hxx>
#include <JDrama/JDRScreen.hxx>
#include <JGadget/Vector.hxx>
#include <SMS/MarioUtil/DrawUtil.hxx>
#include <SMS/MarioUtil/gd-reinit-gx.hxx>
#include <SMS/System/Application.hxx>
#include <SMS/System/Resolution.hxx>

#include <J2D/J2DOrthoGraph.hxx>
#include <J2D/J2DPicture.hxx>

#include "module.hxx"
#include "p_area.hxx"
#include <GC2D/SelectGrad.hxx>

using namespace BetterSMS;

struct ScenarioMenuInfo {
    J2DTextBox *mScenarioTextBox;
    s32 mSceneID;
    s32 mScenarioID;
};

struct SceneMenuInfo {
    s32 mSceneID;
    s32 mPrimaryAreaID;  // Important for triggering shine select
    bool mHasShineSelect;
    J2DTextBox *mNameTextBox;
    J2DPane *mScenarioListPane;
    JGadget::TVector<ScenarioMenuInfo *> mScenarioMenuInfos;
};

struct EpisodeMenuInfo {
    J2DTextBox *mEpisodeTextBox;
    J2DTextBox *mFilenameTextBox;
    s32 mNormalStageID;
    s32 mEpisodeID;
};

struct AreaMenuInfo {
    s32 mAreaID;
    J2DTextBox *mNameTextBox;
    J2DPane *mEpisodeListPane;
    JGadget::TVector<EpisodeMenuInfo *> mEpisodeMenuInfos;
};

class LevelSelectScreen : public JDrama::TViewObj {
    enum ELevelSelectView {
        SCENE_VIEW,
        AREA_VIEW,
    };

public:
    friend class LevelSelectDirector;

    LevelSelectScreen(TMarioGamePad *controller)
        : TViewObj("<LevelSelectScreen>"), mController(controller), mShouldExit(false),
          mSceneColumnCount(1), mAreaColumnCount(1), mScrollGroupID(0), mScrollEntryID(0),
          mSelectedGroupID(-1), mSelectedEntryID(-1), mSceneScrollOffset(0), mAreaScrollOffset(0),
          mEntryScrollOffset(0), mShowFilenames(false),
          mEnterShineSelect(false), mScreen(nullptr), mSceneViewPane(nullptr),
          mAreaViewPane(nullptr), mSelectLabel(nullptr), mScrollUpArrow(nullptr),
          mScrollDownArrow(nullptr), mSceneMenuInfos(), mAreaMenuInfos(), mViewToggle(SCENE_VIEW) {}

    ~LevelSelectScreen() override {}

    void perform(u32, JDrama::TGraphics *) override;

    SceneMenuInfo *getSceneInfo(u32 index);
    AreaMenuInfo *getAreaInfo(u32 index);
    EpisodeMenuInfo *getEpisodeInfo(u32 areaIndex, u32 index);

protected:
    void processSceneInput();
    void processAreaInput();

    void drawSceneList();
    void drawAreaList();

    void setView(ELevelSelectView view);

    void layoutGroupEntry(J2DTextBox *nameTextBox, s32 index, s32 rowOffset, s32 columnCount);
    void layoutPopupEntry(J2DTextBox *entryTextBox, s32 slot);
    void stepGroupSelection(s32 delta, s32 count, s32 columnCount, s32 *rowOffset);
    void updateGroupScrollOffset(s32 selected, s32 count, s32 columnCount, s32 *rowOffset);
    void updateScrollOffset(s32 selected, s32 count, s32 pageSize, s32 *offset);
    void updateScrollArrows(s32 count, s32 offset, s32 pageSize, s32 listTop, s32 listBottom,
                            bool listVisible);

    void genSceneList(void *sceneNameData, void *scenarioNameData);
    bool genSceneText(s32 flatRow, u8 shineStageID, void *sceneNameData, void *scenarioNameData);
    void genScenarioText(SceneMenuInfo &, void *scenarioNameData);

    void genAreaList();
    bool genAreaText(s32 flatRow, u8 areaID);
    void genEpisodeText(AreaMenuInfo &, u8 areaID);

private:
    TMarioGamePad *mController;
    bool mShouldExit;

    s32 mSceneColumnCount;
    s32 mAreaColumnCount;

    s32 mScrollGroupID;
    s32 mScrollEntryID;
    s32 mSelectedGroupID;
    s32 mSelectedEntryID;

    // Group views scroll by whole rows, so these count rows, not entries. That
    // is what keeps an entry in the column its index puts it in.
    s32 mSceneScrollOffset;
    s32 mAreaScrollOffset;

    // The popup episode list is one column, so this one counts entries.
    s32 mEntryScrollOffset;

    bool mShowFilenames;
    bool mEnterShineSelect;

    J2DScreen *mScreen;
    J2DPane *mSceneViewPane;
    J2DPane *mAreaViewPane;
    J2DTextBox *mSelectLabel;

    J2DPicture *mScrollUpArrow;
    J2DPicture *mScrollDownArrow;

    JGadget::TVector<SceneMenuInfo *> mSceneMenuInfos;
    JGadget::TVector<AreaMenuInfo *> mAreaMenuInfos;

    ELevelSelectView mViewToggle;
};

class LevelSelectDirector : public JDrama::TDirector {
    enum class State { INIT, CONTROL, EXIT };

public:
    LevelSelectDirector()           = default;
    ~LevelSelectDirector() override = default;

    void setup(JDrama::TDisplay *, TMarioGamePad *);

    s32 direct() override;

protected:
    static void *setupThreadFunc(void *);

    s32 exit();
    void initialize();
    void initializeDramaHierarchy();
    void initializeLevelsLayout();

private:
    State mState;
    JDrama::TDisplay *mDisplay;
    TMarioGamePad *mController;
    LevelSelectScreen *mSelectScreen;
};
