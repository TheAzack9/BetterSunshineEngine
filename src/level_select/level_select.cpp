#include <JDrama/JDRCamera.hxx>
#include <JDrama/JDRDStageGroup.hxx>
#include <JDrama/JDRDirector.hxx>
#include <JDrama/JDRDisplay.hxx>
#include <JDrama/JDRScreen.hxx>
#include <JGadget/Vector.hxx>
#include <SMS/MSound/MSBGM.hxx>
#include <SMS/MarioUtil/DrawUtil.hxx>
#include <SMS/MarioUtil/gd-reinit-gx.hxx>
#include <SMS/System/Application.hxx>
#include <SMS/System/Resolution.hxx>

#include <J2D/J2DOrthoGraph.hxx>

#include "game.hxx"
#include "loading.hxx"
#include "module.hxx"
#include "p_area.hxx"
#include "p_level_select.hxx"
#include "stage.hxx"
#include <DVD.h>
#include <raw_fn.hxx>

#define TEXT_COLOR_DEFAULT_SCENARIO {255, 255, 255, 255}
#define TEXT_COLOR_DEFAULT_FILENAME {200, 255, 200, 255}
#define TEXT_COLOR_TOP_SELECTED     {180, 230, 10, 255}
#define TEXT_COLOR_BOTTOM_SELECTED  {240, 170, 10, 255}
#define TEXT_COLOR_INVALID_SCENARIO {255, 60, 50, 255}

extern bool gForceOpenShineSelect;

constexpr int TitleFontSize = 21;
constexpr int EntryFontSize = 21;

constexpr int RowsPerColumn = 14;
constexpr int MaxColumns    = 2;

constexpr int PopupListTop     = 110;
constexpr int PopupRowPitch    = EntryFontSize + 2;
constexpr int PopupVisibleRows = 10;

constexpr int PopupListTextTop = PopupListTop + 24 - (EntryFontSize / 2);
constexpr int PopupListTextBottom =
    PopupListTop + ((PopupVisibleRows - 1) * PopupRowPitch) + 24 + (EntryFontSize / 2);

static char sSceneSelectLabel[] = "Scene Select";
static char sAreaSelectLabel[]  = "Area Select";

// array size is 160
static const u8 SMS_ALIGN(32) sTinyArrowResTIMG[] = {
    0x02, 0x01, 0x00, 0x10, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
    0x00, 0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0x00,
    0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0x00, 0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0xfe,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe,
    0xfe, 0xfe, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static J2DSetScreen *sShineSelectScreen = nullptr;

inline const ResTIMG *GetResourceTextureHeader(const u8 *data) {
    return reinterpret_cast<const ResTIMG *>(data);
}

static const ResTIMG *GetArrowResTIMG() { return GetResourceTextureHeader(sTinyArrowResTIMG); }

static JUTResFont *s_text_font = nullptr;
static bool SceneInfoHasValidShineSelect(const Stage::ShineAreaInfo *info) {
    if (!info) {
        return false;
    }

    const u32 paneID = info->getShineSelectPaneID();

    if (sShineSelectScreen) {
        J2DPane *infoGroupPane = sShineSelectScreen->search(paneID);
        if (!infoGroupPane) {
            return false;
        }

        J2DPane *infoAPane = infoGroupPane->search((paneID & ~0xFF) | 'a');
        J2DPane *infoBPane = infoGroupPane->search((paneID & ~0xFF) | 'b');
        return infoAPane && infoBPane;
    }

    return paneID != 0;
}

constexpr int CalcAdjustedFontSizeForColumns(int baseSize, int columns) {
    return Max(baseSize - (4 * (columns - 1)), 4);
}

static s32 CalcColumnCountForEntries(s32 count) {
    s32 columns = (count + RowsPerColumn - 1) / RowsPerColumn;
    return Clamp(columns, 1, MaxColumns);
}

static s32 GroupRowOf(s32 index, s32 columnCount) { return index / columnCount; }
static s32 GroupColumnOf(s32 index, s32 columnCount) { return index % columnCount; }
static s32 GroupRowCount(s32 count, s32 columnCount) {
    return (count + columnCount - 1) / columnCount;
}

static s32 LastIndexInColumn(s32 count, s32 columnCount, s32 column) {
    for (s32 index = count - 1; index >= 0; --index) {
        if (GroupColumnOf(index, columnCount) == column) {
            return index;
        }
    }
    return 0;
}

// Text bounds of a group column's first and last row, for arrow placement.
static void CalcGroupListTextBounds(s32 columnCount, s32 *top, s32 *bottom) {
    const s32 fontSize = CalcAdjustedFontSizeForColumns(TitleFontSize, columnCount);
    *top               = 70 + 24 - (fontSize / 2);
    *bottom            = 70 + ((RowsPerColumn - 1) * (fontSize + 2)) + 24 + (fontSize / 2);
}

static s32 CalcCondensedFontWidth(const char *name, s32 fontSize) {
    s32 nameLen = (s32)strlen(name);
    if (nameLen <= 16) {
        return fontSize;
    }
    return Max(fontSize - (nameLen - 16), 4);
}

static TNameRefAryT<TScenarioArchiveName> *getAreaArchive(u32 areaID) {
    if (areaID >= gpApplication.mStageArchiveAry->mChildren.size()) {
        return nullptr;
    }
    return reinterpret_cast<TNameRefAryT<TScenarioArchiveName> *>(
        gpApplication.mStageArchiveAry->mChildren[areaID]);
}

static bool sceneExists(u32 areaID, u32 episodeID) {
    auto *areaInfo = getAreaArchive(areaID);
    if (!areaInfo) {
        OSReport("Area ID %d NOT FOUND\n", areaID);
        return false;
    }

    if (episodeID >= areaInfo->mChildren.size()) {
        OSReport("Area ID %d, Episode ID %d NOT FOUND\n", areaID, episodeID);
        return false;
    }

    TScenarioArchiveName episodeInfo = areaInfo->mChildren[episodeID];
    char stageName[128];
    snprintf(stageName, 128, "/data/scene/%s", episodeInfo.mArchiveName);
    char *loc = strstr(stageName, ".arc");
    if (loc) {
        strncpy(loc, ".szs", 4);
    }

    if (DVDConvertPathToEntrynum(stageName) >= 0) {
        return true;
    } else {
        OSReport("Area ID %d, Episode ID %d, Name %s NOT FOUND\n", areaID, episodeID, stageName);
        return false;
    }
}

static bool sceneFilename(char *out, size_t buf_size, u32 areaID, u32 episodeID) {
    auto *areaInfo = getAreaArchive(areaID);
    if (!areaInfo) {
        OSReport("Area ID %d NOT FOUND\n", areaID);
        return false;
    }

    if (episodeID >= areaInfo->mChildren.size()) {
        OSReport("Area ID %d, Episode ID %d NOT FOUND\n", areaID, episodeID);
        return false;
    }

    TScenarioArchiveName episodeInfo = areaInfo->mChildren[episodeID];
    char stageName[128];
    snprintf(stageName, 128, "/data/scene/%s", episodeInfo.mArchiveName);
    char *loc = strstr(stageName, ".arc");
    if (loc) {
        strncpy(loc, ".szs", 4);
    }

    if (DVDConvertPathToEntrynum(stageName) >= 0) {
        snprintf(out, buf_size, "%s", episodeInfo.mArchiveName);
        char *loc = strstr(out, ".arc");
        if (loc) {
            strncpy(loc, ".szs", 4);
        }
        return true;
    } else {
        OSReport("Area ID %d, Episode ID %d, Name %s NOT FOUND\n", areaID, episodeID, stageName);
        return false;
    }
}

// The shine select is hosted by a real area, so a scene is only enterable
// through the first non-EX area that maps to it.
static s32 findPrimaryAreaForScene(u8 shineStageID) {
    const Stage::NormalAreaInfo *normalAreaInfos = Stage::getNormalAreaInfos();
    const Stage::ExAreaInfo *exAreaInfos         = Stage::getExAreaInfos();

    for (s32 i = 0; i < BETTER_SMS_AREA_MAX; ++i) {
        if (normalAreaInfos[i].mShineStageID != shineStageID) {
            continue;
        }
        if (exAreaInfos[i].mShineStageID != -1) {
            continue;
        }
        return i;
    }

    return -1;
}

void LevelSelectScreen::perform(u32 flags, JDrama::TGraphics *graphics) {
    if ((flags & 0x3)) {
        switch (mViewToggle) {
        case SCENE_VIEW:
            drawSceneList();
            break;
        case AREA_VIEW:
            drawAreaList();
            break;
        }
    }

    if ((flags & 0x8)) {
        ReInitializeGX();
        SMS_DrawInit();

        J2DOrthoGraph ortho(0, 0, BetterSMS::getScreenOrthoWidth(), SMSGetTitleRenderHeight());
        ortho.setup2D();

        mScreen->draw(0, 0, &ortho);
    }

    if ((flags & 0x1)) {
        switch (mViewToggle) {
        case SCENE_VIEW:
            processSceneInput();
            break;
        case AREA_VIEW:
            processAreaInput();
            break;
        }
    }
}

void LevelSelectScreen::setView(ELevelSelectView view) {
    mViewToggle        = view;
    mScrollGroupID     = 0;
    mScrollEntryID     = 0;
    mSelectedGroupID   = -1;
    mSelectedEntryID   = -1;
    mSceneScrollOffset = 0;
    mAreaScrollOffset  = 0;
    mEntryScrollOffset = 0;
    mShowFilenames     = false;

    mSceneViewPane->mIsVisible = view == SCENE_VIEW;
    mAreaViewPane->mIsVisible  = view == AREA_VIEW;
    mSelectLabel->mStrPtr      = view == SCENE_VIEW ? sSceneSelectLabel : sAreaSelectLabel;
}

void LevelSelectScreen::processSceneInput() {
    const s32 sceneCount = mSceneMenuInfos.size();

    // Scroll item
    {
        if ((mController->mButtons.mRapidInput &
             (TMarioGamePad::DPAD_DOWN | TMarioGamePad::MAINSTICK_DOWN))) {
            stepGroupSelection(1, sceneCount, mSceneColumnCount, &mSceneScrollOffset);
        }

        if ((mController->mButtons.mRapidInput &
             (TMarioGamePad::DPAD_UP | TMarioGamePad::MAINSTICK_UP))) {
            stepGroupSelection(-1, sceneCount, mSceneColumnCount, &mSceneScrollOffset);
        }

        // Horizontal movement crosses columns within the current row.
        if ((mController->mButtons.mRapidInput &
             (TMarioGamePad::DPAD_RIGHT | TMarioGamePad::MAINSTICK_RIGHT))) {
            if (GroupColumnOf(mScrollGroupID, mSceneColumnCount) < mSceneColumnCount - 1 &&
                mScrollGroupID + 1 < sceneCount) {
                mScrollGroupID += 1;
            }
        }

        if ((mController->mButtons.mRapidInput &
             (TMarioGamePad::DPAD_LEFT | TMarioGamePad::MAINSTICK_LEFT))) {
            if (GroupColumnOf(mScrollGroupID, mSceneColumnCount) > 0) {
                mScrollGroupID -= 1;
            }
        }

        if (sceneCount <= 0) {
            mScrollGroupID = 0;
        }

        if ((mController->mButtons.mFrameInput & TMarioGamePad::R)) {
            setView(AREA_VIEW);
            return;
        }
    }

    // Select item
    {
        if ((mController->mButtons.mFrameInput & TMarioGamePad::A) && sceneCount > 0) {
            SceneMenuInfo *sceneInfo = mSceneMenuInfos[mScrollGroupID];

            if (sceneInfo->mHasShineSelect) {
                mSelectedGroupID  = mScrollGroupID;
                mSelectedEntryID  = -1;
                mEnterShineSelect = true;
                mShouldExit       = true;
            }
        }

        if ((mController->mButtons.mFrameInput & TMarioGamePad::B)) {
            mSelectedGroupID  = -1;
            mSelectedEntryID  = -1;
            mEnterShineSelect = false;
            mShouldExit       = true;
        }
    }
}

void LevelSelectScreen::processAreaInput() {
    const bool selectingEntry = mSelectedGroupID != -1;
    const s32 areaCount       = mAreaMenuInfos.size();

    // Scroll item
    {
        if ((mController->mButtons.mRapidInput &
             (TMarioGamePad::DPAD_DOWN | TMarioGamePad::MAINSTICK_DOWN))) {
            if (selectingEntry) {
                mScrollEntryID += 1;
            } else {
                stepGroupSelection(1, areaCount, mAreaColumnCount, &mAreaScrollOffset);
            }
        }

        if ((mController->mButtons.mRapidInput &
             (TMarioGamePad::DPAD_UP | TMarioGamePad::MAINSTICK_UP))) {
            if (selectingEntry) {
                mScrollEntryID -= 1;
            } else {
                stepGroupSelection(-1, areaCount, mAreaColumnCount, &mAreaScrollOffset);
            }
        }

        // Horizontal movement crosses columns within the current row.
        if ((mController->mButtons.mRapidInput &
             (TMarioGamePad::DPAD_RIGHT | TMarioGamePad::MAINSTICK_RIGHT))) {
            if (!selectingEntry &&
                GroupColumnOf(mScrollGroupID, mAreaColumnCount) < mAreaColumnCount - 1 &&
                mScrollGroupID + 1 < areaCount) {
                mScrollGroupID += 1;
            }
        }

        if ((mController->mButtons.mRapidInput &
             (TMarioGamePad::DPAD_LEFT | TMarioGamePad::MAINSTICK_LEFT))) {
            if (!selectingEntry && GroupColumnOf(mScrollGroupID, mAreaColumnCount) > 0) {
                mScrollGroupID -= 1;
            }
        }

        if (areaCount <= 0) {
            mScrollGroupID = 0;
        }

        if (selectingEntry) {
            const s32 episodeCount = mAreaMenuInfos[mSelectedGroupID]->mEpisodeMenuInfos.size();
            if (episodeCount > 0) {
                if (mScrollEntryID < 0) {
                    mScrollEntryID += episodeCount;
                } else if (mScrollEntryID >= episodeCount) {
                    mScrollEntryID -= episodeCount;
                }
            } else {
                mScrollEntryID = 0;
            }
        }

        if ((mController->mButtons.mFrameInput & TMarioGamePad::R)) {
            setView(SCENE_VIEW);
            return;
        }
    }

    mShowFilenames = (mController->mButtons.mInput & TMarioGamePad::Z) != 0;

    // Select item
    {
        if ((mController->mButtons.mFrameInput & TMarioGamePad::A)) {
            if (selectingEntry) {
                if (mAreaMenuInfos[mSelectedGroupID]->mEpisodeMenuInfos.size() > 0) {
                    // Tell director to load the scene
                    mSelectedEntryID = mScrollEntryID;
                    mShouldExit      = true;
                }
            } else if (areaCount > 0) {
                mSelectedGroupID   = mScrollGroupID;
                mScrollEntryID     = 0;
                mEntryScrollOffset = 0;
            }
        }

        if ((mController->mButtons.mFrameInput & TMarioGamePad::B)) {
            if (selectingEntry) {
                mSelectedGroupID   = -1;
                mSelectedEntryID   = -1;
                mEntryScrollOffset = 0;
            } else {
                mShouldExit = true;
            }
        }
    }
}

// Vertical movement steps a whole row at a time, so the cursor stays in its
// column. Stepping past the end of the column scrolls the window instead of
// crossing over, and once there is nothing left to scroll the cursor wraps
// around to the other end of that same column.
void LevelSelectScreen::stepGroupSelection(s32 delta, s32 count, s32 columnCount, s32 *rowOffset) {
    if (count <= 0) {
        mScrollGroupID = 0;
        *rowOffset     = 0;
        return;
    }

    const s32 maxRowOffset = Max(GroupRowCount(count, columnCount) - RowsPerColumn, 0);
    const s32 column       = GroupColumnOf(mScrollGroupID, columnCount);
    const s32 visibleRow   = GroupRowOf(mScrollGroupID, columnCount) - *rowOffset;
    const s32 next         = mScrollGroupID + (delta * columnCount);

    if (delta > 0) {
        // A short final row runs out of entries before the column bottoms out.
        if (next < count) {
            if (visibleRow < RowsPerColumn - 1) {
                mScrollGroupID = next;
                return;
            }
            if (*rowOffset < maxRowOffset) {
                *rowOffset += 1;
                mScrollGroupID = next;
                return;
            }
        }

        // Bottom of the scroll, so wrap to the top of the same column.
        mScrollGroupID = column;
        *rowOffset     = 0;
        return;
    }

    if (next >= 0) {
        if (visibleRow > 0) {
            mScrollGroupID = next;
            return;
        }
        if (*rowOffset > 0) {
            *rowOffset -= 1;
            mScrollGroupID = next;
            return;
        }
    }

    // Top of the scroll, so wrap to the bottom of the same column.
    mScrollGroupID = LastIndexInColumn(count, columnCount, column);
    *rowOffset     = maxRowOffset;
}

void LevelSelectScreen::updateGroupScrollOffset(s32 selected, s32 count, s32 columnCount,
                                                s32 *rowOffset) {
    const s32 rowCount = GroupRowCount(count, columnCount);
    if (rowCount <= RowsPerColumn) {
        *rowOffset = 0;
        return;
    }

    const s32 row = GroupRowOf(selected, columnCount);
    if (row < *rowOffset) {
        *rowOffset = row;
    } else if (row >= *rowOffset + RowsPerColumn) {
        *rowOffset = row - (RowsPerColumn - 1);
    }

    *rowOffset = Clamp(*rowOffset, 0, rowCount - RowsPerColumn);
}

void LevelSelectScreen::updateScrollOffset(s32 selected, s32 count, s32 pageSize, s32 *offset) {
    if (count <= pageSize) {
        *offset = 0;
        return;
    }

    if (selected < *offset) {
        *offset = selected;
    } else if (selected >= *offset + pageSize) {
        *offset = selected - (pageSize - 1);
    }

    *offset = Clamp(*offset, 0, count - pageSize);
}

void LevelSelectScreen::updateScrollArrows(s32 count, s32 offset, s32 pageSize, s32 listTop,
                                           s32 listBottom, bool listVisible) {
    if (!mScrollUpArrow || !mScrollDownArrow) {
        return;
    }

    const s32 maxOffset = Max(count - pageSize, 0);

    mScrollUpArrow->mIsVisible   = listVisible && offset > 0;
    mScrollDownArrow->mIsVisible = listVisible && offset < maxOffset;

    if (!mScrollUpArrow->mIsVisible && !mScrollDownArrow->mIsVisible) {
        return;
    }

    const int arrowWidth  = mScrollUpArrow->mRect.mX2 - mScrollUpArrow->mRect.mX1;
    const int arrowHeight = mScrollUpArrow->mRect.mY2 - mScrollUpArrow->mRect.mY1;
    const int arrowX      = 300 - (arrowWidth / 2);

    mScrollUpArrow->mRect.move(arrowX, listTop - 6 - arrowHeight);
    mScrollDownArrow->mRect.move(arrowX, listBottom + 6);
}

void LevelSelectScreen::layoutPopupEntry(J2DTextBox *entryTextBox, s32 slot) {
    const int textY = PopupListTop + (slot * PopupRowPitch);
    entryTextBox->mRect.set(0, textY, 600, textY + 48);
}

void LevelSelectScreen::layoutGroupEntry(J2DTextBox *nameTextBox, s32 index, s32 rowOffset,
                                         s32 columnCount) {
    const int fontSize  = CalcAdjustedFontSizeForColumns(TitleFontSize, columnCount);
    const int textWidth = 500 / columnCount;
    const int textX     = 50 + (GroupColumnOf(index, columnCount) * (textWidth + 4));
    const int textY     = 70 + ((GroupRowOf(index, columnCount) - rowOffset) * (fontSize + 2));

    nameTextBox->mRect.set(textX, textY, textX + textWidth, textY + 48);
}

void LevelSelectScreen::drawSceneList() {
    const s32 sceneCount = mSceneMenuInfos.size();
    if (sceneCount == 0) {
        return;
    }

    updateGroupScrollOffset(mScrollGroupID, sceneCount, mSceneColumnCount, &mSceneScrollOffset);

    s32 listTop, listBottom;
    CalcGroupListTextBounds(mSceneColumnCount, &listTop, &listBottom);
    updateScrollArrows(GroupRowCount(sceneCount, mSceneColumnCount), mSceneScrollOffset,
                       RowsPerColumn, listTop, listBottom, true);

    for (s32 i = 0; i < sceneCount; ++i) {
        SceneMenuInfo *sceneInfo = mSceneMenuInfos[i];
        J2DTextBox *nameTextBox  = sceneInfo->mNameTextBox;

        const s32 visibleRow    = GroupRowOf(i, mSceneColumnCount) - mSceneScrollOffset;
        nameTextBox->mIsVisible = visibleRow >= 0 && visibleRow < RowsPerColumn;
        if (!nameTextBox->mIsVisible) {
            continue;
        }

        layoutGroupEntry(nameTextBox, i, mSceneScrollOffset, mSceneColumnCount);

        if (i == mScrollGroupID) {
            nameTextBox->mGradientTop    = TEXT_COLOR_TOP_SELECTED;
            nameTextBox->mGradientBottom = TEXT_COLOR_BOTTOM_SELECTED;
        } else {
            nameTextBox->mGradientTop    = sceneInfo->mHasShineSelect
                                               ? JUtility::TColor(TEXT_COLOR_DEFAULT_SCENARIO)
                                               : JUtility::TColor(TEXT_COLOR_INVALID_SCENARIO);
            nameTextBox->mGradientBottom = sceneInfo->mHasShineSelect
                                               ? JUtility::TColor(TEXT_COLOR_DEFAULT_SCENARIO)
                                               : JUtility::TColor(TEXT_COLOR_INVALID_SCENARIO);
        }
    }
}

void LevelSelectScreen::drawAreaList() {
    const s32 areaCount = mAreaMenuInfos.size();
    if (areaCount == 0) {
        return;
    }

    const u8 areaAlpha = mSelectedGroupID == -1 ? 255 : 0;

    updateGroupScrollOffset(mScrollGroupID, areaCount, mAreaColumnCount, &mAreaScrollOffset);

    s32 listTop, listBottom;
    CalcGroupListTextBounds(mAreaColumnCount, &listTop, &listBottom);
    updateScrollArrows(GroupRowCount(areaCount, mAreaColumnCount), mAreaScrollOffset, RowsPerColumn,
                       listTop, listBottom, mSelectedGroupID == -1);

    for (s32 i = 0; i < areaCount; ++i) {
        AreaMenuInfo *areaInfo  = mAreaMenuInfos[i];
        J2DTextBox *nameTextBox = areaInfo->mNameTextBox;

        areaInfo->mEpisodeListPane->mIsVisible = false;

        const s32 visibleRow    = GroupRowOf(i, mAreaColumnCount) - mAreaScrollOffset;
        nameTextBox->mIsVisible = visibleRow >= 0 && visibleRow < RowsPerColumn;
        if (!nameTextBox->mIsVisible) {
            continue;
        }

        layoutGroupEntry(nameTextBox, i, mAreaScrollOffset, mAreaColumnCount);

        // Tint the current selection
        if (i == mScrollGroupID) {
            nameTextBox->mGradientTop    = TEXT_COLOR_TOP_SELECTED;
            nameTextBox->mGradientBottom = TEXT_COLOR_BOTTOM_SELECTED;
        } else {
            nameTextBox->mGradientTop    = TEXT_COLOR_DEFAULT_SCENARIO;
            nameTextBox->mGradientBottom = TEXT_COLOR_DEFAULT_SCENARIO;
        }

        nameTextBox->mGradientTop.a    = areaAlpha;
        nameTextBox->mGradientBottom.a = areaAlpha;
    }

    if (mSelectedGroupID == -1) {
        return;
    }

    AreaMenuInfo *curAreaMenuInfo = mAreaMenuInfos[mSelectedGroupID];
    const s32 episodeCount        = curAreaMenuInfo->mEpisodeMenuInfos.size();

    updateScrollOffset(mScrollEntryID, episodeCount, PopupVisibleRows, &mEntryScrollOffset);
    updateScrollArrows(episodeCount, mEntryScrollOffset, PopupVisibleRows, PopupListTextTop,
                       PopupListTextBottom, true);

    for (s32 i = 0; i < episodeCount; ++i) {
        EpisodeMenuInfo *episodeInfo = curAreaMenuInfo->mEpisodeMenuInfos[i];

        const s32 slot      = i - mEntryScrollOffset;
        const bool inWindow = slot >= 0 && slot < PopupVisibleRows;

        episodeInfo->mEpisodeTextBox->mIsVisible  = inWindow && !mShowFilenames;
        episodeInfo->mFilenameTextBox->mIsVisible = inWindow && mShowFilenames;

        if (!inWindow) {
            continue;
        }

        layoutPopupEntry(episodeInfo->mEpisodeTextBox, slot);
        layoutPopupEntry(episodeInfo->mFilenameTextBox, slot);

        // Tint the current selection
        if (i == mScrollEntryID) {
            episodeInfo->mEpisodeTextBox->mGradientTop     = TEXT_COLOR_TOP_SELECTED;
            episodeInfo->mEpisodeTextBox->mGradientBottom  = TEXT_COLOR_BOTTOM_SELECTED;
            episodeInfo->mFilenameTextBox->mGradientTop    = TEXT_COLOR_TOP_SELECTED;
            episodeInfo->mFilenameTextBox->mGradientBottom = TEXT_COLOR_BOTTOM_SELECTED;
        } else {
            episodeInfo->mEpisodeTextBox->mGradientTop     = TEXT_COLOR_DEFAULT_SCENARIO;
            episodeInfo->mEpisodeTextBox->mGradientBottom  = TEXT_COLOR_DEFAULT_SCENARIO;
            episodeInfo->mFilenameTextBox->mGradientTop    = TEXT_COLOR_DEFAULT_FILENAME;
            episodeInfo->mFilenameTextBox->mGradientBottom = TEXT_COLOR_DEFAULT_FILENAME;
        }
    }

    curAreaMenuInfo->mEpisodeListPane->mIsVisible = true;
}

void LevelSelectScreen::genSceneList(void *sceneNameData, void *scenarioNameData) {
    Stage::ShineAreaInfo **shineAreaInfos = Stage::getShineAreaInfos();

    s32 sceneCount = 0;
    for (s32 i = 0; i < BETTER_SMS_AREA_MAX; ++i) {
        if (shineAreaInfos[i]) {
            sceneCount += 1;
        }
    }

    mSceneColumnCount = CalcColumnCountForEntries(sceneCount);

    s32 flatRow = 0;
    for (s32 i = 0; i < BETTER_SMS_AREA_MAX; ++i) {
        if (!shineAreaInfos[i]) {
            continue;
        }
        if (genSceneText(flatRow, i, sceneNameData, scenarioNameData)) {
            flatRow += 1;
        }
    }
}

bool LevelSelectScreen::genSceneText(s32 flatRow, u8 shineStageID, void *sceneNameData,
                                     void *scenarioNameData) {
    const int screenRenderWidth  = BetterSMS::getScreenRenderWidth();
    const int screenRenderHeight = 480;

    const s32 sceneFontSize = CalcAdjustedFontSizeForColumns(TitleFontSize, mSceneColumnCount);

    const Stage::ShineAreaInfo *info = Stage::getShineAreaInfos()[shineStageID];

    const char *sceneName = (const char *)SMSGetMessageData__FPvUl(sceneNameData, shineStageID);

    char *sceneNameBuf = new char[64];
    memset(sceneNameBuf, 0, 64);
    if (sceneName) {
        snprintf(sceneNameBuf, 64, "%s", sceneName);
    } else {
        snprintf(sceneNameBuf, 64, "Scene %d", shineStageID);
    }

    J2DPane *scenePane =
        new J2DPane(19, ('S' << 24) | shineStageID, {0, 0, screenRenderWidth, screenRenderHeight});
    scenePane->mIsVisible = false;
    {
        char *groupTextBuf = new char[64];
        memset(groupTextBuf, 0, 64);

        snprintf(groupTextBuf, 64, "%s", sceneNameBuf);

        J2DTextBox *label =
            new J2DTextBox(('l' << 24) | shineStageID, {0, 30, 600, 120}, s_text_font->mFont,
                           groupTextBuf, J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
        label->mCharSizeX      = 26;
        label->mCharSizeY      = 26;
        label->mNewlineSize    = 26;
        label->mGradientTop    = {240, 10, 170, 255};
        label->mGradientBottom = {180, 10, 230, 255};
        scenePane->mChildrenList.append(&label->mPtrLink);
    }

    // Scene listing
    J2DTextBox *sceneText =
        new J2DTextBox(('s' << 24) | shineStageID, {0, 0, 0, 0}, s_text_font->mFont, "",
                       J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);
    {
        sceneText->mStrPtr         = sceneNameBuf;
        sceneText->mCharSizeX      = CalcCondensedFontWidth(sceneNameBuf, sceneFontSize);
        sceneText->mCharSizeY      = sceneFontSize;
        sceneText->mNewlineSize    = sceneFontSize;
        sceneText->mGradientBottom = TEXT_COLOR_DEFAULT_SCENARIO;
        sceneText->mGradientTop    = TEXT_COLOR_DEFAULT_SCENARIO;
    }
    layoutGroupEntry(sceneText, flatRow, 0, mSceneColumnCount);
    mSceneViewPane->mChildrenList.append(&sceneText->mPtrLink);

    SceneMenuInfo *sceneMenuInfo  = new SceneMenuInfo();
    sceneMenuInfo->mSceneID       = shineStageID;
    sceneMenuInfo->mPrimaryAreaID = findPrimaryAreaForScene(shineStageID);
    sceneMenuInfo->mHasShineSelect =
        sceneMenuInfo->mPrimaryAreaID && SceneInfoHasValidShineSelect(info);
    sceneMenuInfo->mNameTextBox      = sceneText;
    sceneMenuInfo->mScenarioListPane = scenePane;
    mSceneMenuInfos.insert(mSceneMenuInfos.end(), sceneMenuInfo);

    genScenarioText(*sceneMenuInfo, scenarioNameData);

    mSceneViewPane->mChildrenList.append(&scenePane->mPtrLink);
    return true;
}

void LevelSelectScreen::genScenarioText(SceneMenuInfo &menu, void *scenarioNameData) {
    const Stage::ShineAreaInfo *info = Stage::getShineAreaInfos()[menu.mSceneID];
    if (!info) {
        return;
    }

    const TGlobalVector<s32> &scenarioIDs     = info->getScenarioIDs();
    const TGlobalVector<s32> &scenarioNameIDs = info->getScenarioNameIDs();

    if (scenarioIDs.size() > scenarioNameIDs.size()) {
        OSReport("[WARNING] Scenario count mismatches name count! %lu / %lu\n", scenarioIDs.size(),
                 scenarioNameIDs.size());
    }

    for (s32 j = 0; j < (s32)scenarioIDs.size(); ++j) {
        const s32 scenarioNameID = j >= (s32)scenarioNameIDs.size() ? -1 : scenarioNameIDs[j];

        J2DTextBox *scenarioText = new J2DTextBox(
            ('c' << 24) | (menu.mSceneID << 8) | j,
            {0, PopupListTop + (PopupRowPitch * j), 600, PopupListTop + 48 + (PopupRowPitch * j)},
            s_text_font->mFont, "", J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
        {
            char *scenarioTextBuf = new char[100];
            memset(scenarioTextBuf, 0, 100);

            const char *scenarioName =
                scenarioNameID == -1
                    ? nullptr
                    : (const char *)SMSGetMessageData__FPvUl(scenarioNameData, scenarioNameID);

            if (scenarioName) {
                snprintf(scenarioTextBuf, 100, "%s", scenarioName);
            } else {
                snprintf(scenarioTextBuf, 100, "Scenario %ld", j);
            }

            scenarioText->mStrPtr         = scenarioTextBuf;
            scenarioText->mCharSizeX      = EntryFontSize;
            scenarioText->mCharSizeY      = EntryFontSize;
            scenarioText->mNewlineSize    = EntryFontSize;
            scenarioText->mGradientBottom = TEXT_COLOR_DEFAULT_SCENARIO;
            scenarioText->mGradientTop    = TEXT_COLOR_DEFAULT_SCENARIO;
        }
        menu.mScenarioListPane->mChildrenList.append(&scenarioText->mPtrLink);

        ScenarioMenuInfo *scenarioInfo = new ScenarioMenuInfo();
        scenarioInfo->mScenarioTextBox = scenarioText;
        scenarioInfo->mSceneID         = menu.mSceneID;
        scenarioInfo->mScenarioID      = j;
        menu.mScenarioMenuInfos.insert(menu.mScenarioMenuInfos.end(), scenarioInfo);
    }
}

void LevelSelectScreen::genAreaList() {
    const s32 archiveCount = gpApplication.mStageArchiveAry->mChildren.size();

    s32 areaCount = 0;
    for (s32 i = 0; i < archiveCount; ++i) {
        if (getAreaArchive(i)) {
            areaCount += 1;
        }
    }

    mAreaColumnCount = CalcColumnCountForEntries(areaCount);

    s32 flatRow = 0;
    for (s32 i = 0; i < archiveCount; ++i) {
        if (genAreaText(flatRow, i)) {
            flatRow += 1;
        }
    }
}

bool LevelSelectScreen::genAreaText(s32 flatRow, u8 areaID) {
    auto *areaInfoAry = getAreaArchive(areaID);
    if (!areaInfoAry) {
        return false;
    }

    const int screenRenderWidth  = BetterSMS::getScreenRenderWidth();
    const int screenRenderHeight = 480;

    const s32 areaFontSize = CalcAdjustedFontSizeForColumns(TitleFontSize, mAreaColumnCount);

    char *areaNameBuf = new char[64];
    memset(areaNameBuf, 0, 64);
    if (areaInfoAry->mKeyName) {
        snprintf(areaNameBuf, 64, "%s", areaInfoAry->mKeyName);
    } else {
        snprintf(areaNameBuf, 64, "Area %d", areaID);
    }

    J2DPane *areaPane =
        new J2DPane(19, ('A' << 24) | areaID, {0, 0, screenRenderWidth, screenRenderHeight});
    areaPane->mIsVisible = false;
    {
        char *groupTextBuf = new char[64];
        memset(groupTextBuf, 0, 64);

        snprintf(groupTextBuf, 64, "%s", areaNameBuf);

        J2DTextBox *label   = new J2DTextBox(('l' << 24) | ('a' << 16) | areaID, {0, 30, 600, 120},
                                             s_text_font->mFont, groupTextBuf,
                                             J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
        label->mCharSizeX   = 26;
        label->mCharSizeY   = 26;
        label->mNewlineSize = 26;
        label->mGradientTop = {240, 10, 170, 255};
        label->mGradientBottom = {180, 10, 230, 255};
        areaPane->mChildrenList.append(&label->mPtrLink);
    }

    // Area listing
    J2DTextBox *areaText = new J2DTextBox(('a' << 24) | areaID, {0, 0, 0, 0}, s_text_font->mFont,
                                          "", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);
    {
        areaText->mStrPtr         = areaNameBuf;
        areaText->mCharSizeX      = CalcCondensedFontWidth(areaNameBuf, areaFontSize);
        areaText->mCharSizeY      = areaFontSize;
        areaText->mNewlineSize    = areaFontSize;
        areaText->mGradientBottom = TEXT_COLOR_DEFAULT_SCENARIO;
        areaText->mGradientTop    = TEXT_COLOR_DEFAULT_SCENARIO;
    }
    layoutGroupEntry(areaText, flatRow, 0, mAreaColumnCount);
    mAreaViewPane->mChildrenList.append(&areaText->mPtrLink);

    AreaMenuInfo *areaMenuInfo     = new AreaMenuInfo();
    areaMenuInfo->mAreaID          = areaID;
    areaMenuInfo->mNameTextBox     = areaText;
    areaMenuInfo->mEpisodeListPane = areaPane;
    mAreaMenuInfos.insert(mAreaMenuInfos.end(), areaMenuInfo);

    genEpisodeText(*areaMenuInfo, areaID);

    mAreaViewPane->mChildrenList.append(&areaPane->mPtrLink);
    return true;
}

void LevelSelectScreen::genEpisodeText(AreaMenuInfo &menu, u8 areaID) {
    auto *areaInfoAry = getAreaArchive(areaID);
    if (!areaInfoAry) {
        return;
    }

    const s32 episodeCount = areaInfoAry->mChildren.size();

    for (s32 i = 0; i < episodeCount; ++i) {
        char filename[128];
        if (!sceneFilename(filename, 128, areaID, i)) {
            continue;
        }

        const TScenarioArchiveName &episode = areaInfoAry->mChildren[i];

        J2DTextBox *episodeText =
            new J2DTextBox(('e' << 24) | (areaID << 8) | i, {0, 0, 0, 0}, s_text_font->mFont, "",
                           J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
        {
            char *episodeTextBuf = new char[100];
            memset(episodeTextBuf, 0, 100);

            if (episode.mKeyName) {
                snprintf(episodeTextBuf, 100, "%s", episode.mKeyName);
            } else {
                snprintf(episodeTextBuf, 100, "Episode %ld", i);
            }

            episodeText->mStrPtr         = episodeTextBuf;
            episodeText->mCharSizeX      = EntryFontSize;
            episodeText->mCharSizeY      = EntryFontSize;
            episodeText->mNewlineSize    = EntryFontSize;
            episodeText->mGradientBottom = TEXT_COLOR_DEFAULT_SCENARIO;
            episodeText->mGradientTop    = TEXT_COLOR_DEFAULT_SCENARIO;
            episodeText->mIsVisible      = false;
        }
        menu.mEpisodeListPane->mChildrenList.append(&episodeText->mPtrLink);

        J2DTextBox *episodeFileNameText =
            new J2DTextBox(('f' << 24) | (areaID << 8) | i, {0, 0, 0, 0}, s_text_font->mFont, "",
                           J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
        {
            char *episodeFileNameTextBuf = new char[100];
            memset(episodeFileNameTextBuf, 0, 100);
            snprintf(episodeFileNameTextBuf, 100, "%s", filename);

            episodeFileNameText->mStrPtr         = episodeFileNameTextBuf;
            episodeFileNameText->mCharSizeX      = EntryFontSize;
            episodeFileNameText->mCharSizeY      = EntryFontSize;
            episodeFileNameText->mNewlineSize    = EntryFontSize;
            episodeFileNameText->mGradientBottom = TEXT_COLOR_DEFAULT_FILENAME;
            episodeFileNameText->mGradientTop    = TEXT_COLOR_DEFAULT_FILENAME;
            episodeFileNameText->mIsVisible      = false;
        }
        menu.mEpisodeListPane->mChildrenList.append(&episodeFileNameText->mPtrLink);

        EpisodeMenuInfo *episodeInfo  = new EpisodeMenuInfo();
        episodeInfo->mEpisodeTextBox  = episodeText;
        episodeInfo->mFilenameTextBox = episodeFileNameText;
        episodeInfo->mNormalStageID   = areaID;
        episodeInfo->mEpisodeID       = i;
        menu.mEpisodeMenuInfos.insert(menu.mEpisodeMenuInfos.end(), episodeInfo);
    }
}

SceneMenuInfo *LevelSelectScreen::getSceneInfo(u32 index) {
    if (index >= mSceneMenuInfos.size())
        return nullptr;

    return mSceneMenuInfos.at(index);
}

AreaMenuInfo *LevelSelectScreen::getAreaInfo(u32 index) {
    if (index >= mAreaMenuInfos.size())
        return nullptr;

    return mAreaMenuInfos.at(index);
}

EpisodeMenuInfo *LevelSelectScreen::getEpisodeInfo(u32 areaIndex, u32 index) {
    const AreaMenuInfo *info = getAreaInfo(areaIndex);
    if (!info)
        return nullptr;

    if (index >= info->mEpisodeMenuInfos.size())
        return nullptr;

    return info->mEpisodeMenuInfos.at(index);
}

void *LevelSelectDirector::setupThreadFunc(void *param) {
    auto *director = reinterpret_cast<LevelSelectDirector *>(param);
    director->initialize();
    return nullptr;
}

void LevelSelectDirector::setup(JDrama::TDisplay *display, TMarioGamePad *controller) {
    mViewObjStageGroup             = new JDrama::TDStageGroup(display);
    mDisplay                       = display;
    mController                    = controller;
    mController->mState.mReadInput = false;
    OSCreateThread(&gSetupThread, setupThreadFunc, this, gpSetupThreadStack + 0x10000, 0x10000, 17,
                   0);
    OSResumeThread(&gSetupThread);
}

static JKRMemArchive *s_title_archive  = nullptr;
static JKRMemArchive *s_select_archive = nullptr;

void LevelSelectDirector::initialize() {
    s_text_font = nullptr;
    s_title_archive = nullptr;
    s_select_archive = nullptr;
    sShineSelectScreen = nullptr;

    void *title_archive = SMSLoadArchive("/data/title.arc", nullptr, 0, nullptr);
    s_title_archive     = new JKRMemArchive();
    s_title_archive->mountFixed(title_archive, JKRMemBreakFlag::UNK_0);

    void *select_archive = SMSLoadArchive("/data/select.arc", nullptr, 0, nullptr);
    s_select_archive     = new JKRMemArchive();
    s_select_archive->mountFixed(select_archive, JKRMemBreakFlag::UNK_0);

    sShineSelectScreen = new J2DSetScreen("scenario_select_1.blo", s_select_archive);

    ResFONT *font_res = (ResFONT *)JKRFileLoader::getGlbResource("/title/font/test_fontex.bfn");
    if (font_res) {
        s_text_font = new JUTResFont(font_res, s_title_archive);
    }

    if (!s_text_font) {
        font_res = (ResFONT *)JKRFileLoader::getGlbResource("test_fontex.bfn");
        if (font_res) {
            s_text_font = new JUTResFont(font_res, nullptr);
        }
    }

    if (!s_text_font) {
        OSReport("[WARN] Could not find test_fontex.bfn in common.arc or title.arc, "
                 "falling back to the system font.\n");
        s_text_font = gpSystemFont;
    }

    initializeDramaHierarchy();
    initializeLevelsLayout();
}

void LevelSelectDirector::initializeDramaHierarchy() {
    auto *stageObjGroup = reinterpret_cast<JDrama::TDStageGroup *>(mViewObjStageGroup);
    auto *rootObjGroup  = new JDrama::TViewObjPtrListT<JDrama::TViewObj>("Root View Objs");
    mViewObjRoot        = rootObjGroup;

    JDrama::TRect screenRect{0, 0, SMSGetTitleRenderWidth(), SMSGetTitleRenderHeight()};

    auto *group2D = new JDrama::TViewObjPtrListT<JDrama::TViewObj>("Group 2D");
    {
        mSelectScreen = new LevelSelectScreen(mController);
        group2D->mViewObjList.insert(group2D->mViewObjList.end(), mSelectScreen);

        rootObjGroup->mViewObjList.insert(rootObjGroup->mViewObjList.end(), group2D);
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

void LevelSelectDirector::initializeLevelsLayout() {
    const int screenOrthoWidth   = BetterSMS::getScreenOrthoWidth();
    const int screenRenderHeight = 480;

    void *stageNameData = JKRFileLoader::getGlbResource("/common/2d/stagename.bmg");
    SMS_ASSERT(stageNameData, "Missing /common/2d/stagename.bmg!");

    void *scenarioNameData = JKRFileLoader::getGlbResource("/common/2d/scenarioname.bmg");
    SMS_ASSERT(scenarioNameData, "Missing /common/2d/scenarioname.bmg!");

    mSelectScreen->mScreen = new J2DScreen(8, 'ROOT', {0, 0, screenOrthoWidth, screenRenderHeight});

    // _EC is mbClipToParent, and sms_interface's inline J2DScreen ctors never
    // write it -- the game's own ctor does, but these bypass it, so it starts as
    // whatever the heap last held there. Non-zero makes J2DScreen::draw pass
    // isOrthoGraf=true, and every pane then GXSetScissor's itself to its own
    // bounds, cropping text to its box. Worse in widescreen, because the scissor
    // is computed in ortho units but applied in EFB pixels.
    mSelectScreen->mScreen->_EC = 0;
    {
        const ResTIMG *bg_timg = reinterpret_cast<const ResTIMG *>(
            JKRFileLoader::getGlbResource("/title/timg/title_test.bti"));
        if (bg_timg) {
            JUTTexture *bg_texture      = new JUTTexture();
            bg_texture->mTexObj2.val[2] = 0;
            bg_texture->storeTIMG(bg_timg);
            bg_texture->_50 = false;

            J2DPicture *background =
                new J2DPicture('BG_0', {0, 0, screenOrthoWidth, screenRenderHeight});
            background->insert(bg_texture, 0, 1.0f);

            // Darken bg
            background->mColorOverlay    = {0, 0, 0, 128};
            background->mVertexColors[0] = {100, 100, 100, 128};
            background->mVertexColors[1] = {100, 100, 100, 128};
            background->mVertexColors[2] = {100, 100, 100, 128};
            background->mVertexColors[3] = {100, 100, 100, 128};

            mSelectScreen->mScreen->mChildrenList.append(&background->mPtrLink);
        }

        J2DTextBox *label = new J2DTextBox(
            'logo', {0, screenRenderHeight - 110, 600, screenRenderHeight}, s_text_font->mFont,
            sSceneSelectLabel, J2DTextBoxHBinding::Center, J2DTextBoxVBinding::Center);
        label->mCharSizeX           = 24;
        label->mCharSizeY           = 24;
        label->mNewlineSize         = 24;
        mSelectScreen->mSelectLabel = label;
        mSelectScreen->mScreen->mChildrenList.append(&label->mPtrLink);

        J2DTextBox *exitLabel = new J2DTextBox(
            'exit',
            {static_cast<int>(30 - BetterSMS::getScreenRatioAdjustX()), screenRenderHeight - 110,
             static_cast<int>(170 - BetterSMS::getScreenRatioAdjustX()), screenRenderHeight},
            gpSystemFont->mFont, "# Exit", J2DTextBoxHBinding::Left, J2DTextBoxVBinding::Center);
        mSelectScreen->mScreen->mChildrenList.append(&exitLabel->mPtrLink);

        J2DTextBox *toggleLabel = new J2DTextBox(
            'togl',
            {static_cast<int>(400 + BetterSMS::getScreenRatioAdjustX()), screenRenderHeight - 110,
             static_cast<int>(570 + BetterSMS::getScreenRatioAdjustX()), screenRenderHeight},
            gpSystemFont->mFont, "> Toggle", J2DTextBoxHBinding::Right, J2DTextBoxVBinding::Center);
        mSelectScreen->mScreen->mChildrenList.append(&toggleLabel->mPtrLink);

        const ResTIMG *arrow_timg = GetArrowResTIMG();

        const int arrowWidth  = arrow_timg->mWidth;
        const int arrowHeight = arrow_timg->mHeight;

        JUTTexture *up_texture      = new JUTTexture();
        up_texture->mTexObj2.val[2] = 0;
        up_texture->storeTIMG(arrow_timg);
        up_texture->_50 = false;

        J2DPicture *scrollUpArrow = new J2DPicture('scup', {0, 0, arrowWidth, arrowHeight});
        scrollUpArrow->insert(up_texture, 0, 1.0f);
        scrollUpArrow->mMirrorFlags   = MirrorX | MirrorY;
        scrollUpArrow->mIsVisible     = false;
        scrollUpArrow->mColorMask     = {0, 255, 0, 255};
        mSelectScreen->mScrollUpArrow = scrollUpArrow;
        mSelectScreen->mScreen->mChildrenList.append(&scrollUpArrow->mPtrLink);

        JUTTexture *down_texture      = new JUTTexture();
        down_texture->mTexObj2.val[2] = 0;
        down_texture->storeTIMG(arrow_timg);
        down_texture->_50 = false;

        J2DPicture *scrollDownArrow = new J2DPicture('scdn', {0, 0, arrowWidth, arrowHeight});
        scrollDownArrow->insert(down_texture, 0, 1.0f);
        scrollDownArrow->mIsVisible     = false;
        scrollDownArrow->mColorMask     = {0, 255, 0, 255};
        mSelectScreen->mScrollDownArrow = scrollDownArrow;
        mSelectScreen->mScreen->mChildrenList.append(&scrollDownArrow->mPtrLink);
    }

    mSelectScreen->mSceneViewPane =
        new J2DPane(19, 'scnv', {0, 0, screenOrthoWidth, screenRenderHeight});
    mSelectScreen->mScreen->mChildrenList.append(&mSelectScreen->mSceneViewPane->mPtrLink);

    mSelectScreen->mAreaViewPane =
        new J2DPane(19, 'arev', {0, 0, screenOrthoWidth, screenRenderHeight});
    mSelectScreen->mAreaViewPane->mIsVisible = false;
    mSelectScreen->mScreen->mChildrenList.append(&mSelectScreen->mAreaViewPane->mPtrLink);

    mSelectScreen->genSceneList(stageNameData, scenarioNameData);
    mSelectScreen->genAreaList();
}

s32 LevelSelectDirector::direct() {
    s32 ret = 1;

    int *joinBuf[2];

    TSMSFader *fader = gpApplication.mFader;
    if (fader->mFadeStatus == TSMSFader::FADE_OFF) {
        mSelectScreen->mController->mState.mReadInput = false;
        mSelectScreen->mController->mState._02        = true;
    }

    if (mState == State::INIT) {
        if (!OSIsThreadTerminated(&gSetupThread))
            return 0;
        OSJoinThread(&gSetupThread, (void **)joinBuf);

        fader->startFadeinT(0.3f);

        if (!TFlagManager::smInstance->getBool(0x30007)) {
            TFlagManager::smInstance->setBool(true, 0x30007);
            gpMSound->loadWave(MS_WAVE_DEFAULT);
        }

        gpMSound->initSound();
        gpMSound->enterStage((MS_SCENE_WAVE)517, 10, 0);
        MSBgm::startBGM(BGM_MARE_SEA);

        mState = State::CONTROL;
        return 0;
    }

    TDirector::direct();

    switch (mState) {
    case State::INIT:
        break;
    case State::CONTROL:
        mSelectScreen->mPerformFlags &= ~0b0001;  // Enable input by default;

        if (mSelectScreen->mShouldExit) {
            if ((mController->mButtons.mInput & TMarioGamePad::X)) {
                TFlagManager::smInstance->firstStart();
                for (u32 shine = 0; shine < BetterSMS::Game::getMaxShines(); ++shine) {
                    TFlagManager::smInstance->setShineFlag(shine);
                }
                for (u32 bluec = 0x10366; bluec < 0x103B4; ++bluec) {
                    TFlagManager::smInstance->setBool(true, bluec);
                }
                TFlagManager::smInstance->saveSuccess();
            }

            if (mSelectScreen->mEnterShineSelect) {
                SceneMenuInfo *info = mSelectScreen->getSceneInfo(mSelectScreen->mSelectedGroupID);
                if (info) {
                    gpApplication.mNextScene.set(info->mPrimaryAreaID, 0, 0);
                    gForceOpenShineSelect = (mController->mButtons.mInput & TMarioGamePad::X) == 0;
                }
            } else if (mSelectScreen->mSelectedGroupID != -1 &&
                       mSelectScreen->mSelectedEntryID != -1) {
                EpisodeMenuInfo *info = mSelectScreen->getEpisodeInfo(
                    mSelectScreen->mSelectedGroupID, mSelectScreen->mSelectedEntryID);
                if (info) {
                    gpApplication.mNextScene.mAreaID    = info->mNormalStageID;
                    gpApplication.mNextScene.mEpisodeID = info->mEpisodeID;
                    // Reset coins
                    TFlagManager::smInstance->setFlag(0x40002, 0);
                    TFlagManager::smInstance->setFlag(0x40003, info->mEpisodeID);
                }
            }

            mState = State::EXIT;
        }
        break;
    case State::EXIT: {
        ret = exit();
        break;
    }
    }
    return ret;
}

s32 LevelSelectDirector::exit() {
    TSMSFader *fader = gpApplication.mFader;

    if (!gpMSound->checkWaveOnAram((MS_SCENE_WAVE)517)) {
        return 1;
    }

    if (fader->mFadeStatus == TSMSFader::FADE_OFF) {
        gpApplication.mFader->startFadeoutT(0.3f);
        MSBgm::stopBGM(BGM_MARE_SEA, 10);
    }

    if (fader->mFadeStatus != TSMSFader::FADE_ON) {
        return 1;
    }

    return mSelectScreen->mEnterShineSelect ? TApplication::CONTEXT_DIRECT_SHINE_SELECT
                                            : TApplication::CONTEXT_DIRECT_STAGE;
}
