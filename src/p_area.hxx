#pragma once

#include <SMS/System/Application.hxx>

#include "area.hxx"

namespace BetterSMS {

    namespace Stage {

        struct NormalAreaInfo {
            s32 mShineStageID;
        };

        struct ExAreaInfo {
            s32 mShineStageID;
            s32 mShineID;
        };

        ShineAreaInfo **getShineAreaInfos();
        NormalAreaInfo *getNormalAreaInfos();
        ExAreaInfo *getExAreaInfos();

        class LevelNameRefGen : public JDrama::TNameRefGen {
	        virtual JDrama::TNameRef* getNameRef(const char*) const override;
        };

        class CustomScenario : public JDrama::TNameRef {
        public:
            CustomScenario(const char *name);
            void load(JSUMemoryInputStream &stream);
        };

        class CustomStage : public JDrama::TNameRef {
        public:
            CustomStage(const char *name);
            void load(JSUMemoryInputStream &stream);
        };
    }  // namespace Stage

}  // namespace BetterSMS