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
        : TViewObj("<LevelSelectScreen>"), mScreen(nullptr), mController(controller),
          mScrollGroupID(0), mScrollEntryID(0), mSelectedGroupID(-1), mSelectedEntryID(-1),
          mSceneMenuInfos(), mAreaMenuInfos(), mShouldExit(false), mViewToggle(SCENE_VIEW) {}

    ~LevelSelectScreen() override {}

    void perform(u32, JDrama::TGraphics *) override;

    SceneMenuInfo *getAreaInfo(u32 index);
    ScenarioMenuInfo *getEpisodeInfo(u32 index);

protected:
    void processSceneInput();
    void processAreaInput();

    void drawSceneList();
    void drawAreaList();

    bool genSceneText(s32 flatRow, u8 normalStageID, u8 shineStageID, void *stageNameData,
                      void *scenarioNameData);
    void genScenarioText(SceneMenuInfo &, u8 normalStageID, u8 shineStageID,
                         void *scenarioNameData);

    bool genAreaText(s32 flatRow, u8 normalStageID);
    void genEpisodeTextDelfinoPlaza(SceneMenuInfo &, u8 normalStageID, u8 shineStageID,
                                    void *scenarioNameData);
    void genEpisodeTextTest1(SceneMenuInfo &info);
    void genEpisodeTextTest2(SceneMenuInfo &info);
    void genEpisodeTextScale(SceneMenuInfo &info);

    J2DPane *findOrCreateScenePane(u8 shineStageID, int width, int height, bool *created);
    J2DPane *findOrCreateAreaPane(u8 normalStageID, int width, int height, bool *created);

private:
    TMarioGamePad *mController;
    bool mShouldExit;
    
    s32 mColumnSize;
    s32 mColumnCount;
    
    s32 mScrollGroupID;
    s32 mScrollEntryID;
    s32 mSelectedGroupID;
    s32 mSelectedEntryID;
    
    J2DScreen *mScreen;
    J2DPane *mSceneViewPane;
    J2DPane *mAreaViewPane;
    J2DTextBox *mSelectLabel;
    
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