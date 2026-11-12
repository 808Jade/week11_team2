#include "EnginePCH.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Animation/Skeleton.h"
#include "Editor/Asset/FbxImporter/FbxImporter.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace {
    void MatchLoopHeight(FFbxAssetData& Asset) {
        for (auto& Track : Asset.Animation.BoneAnimationTracks) {
            auto& Keys{Track.InternalTrackData.PosKeys};
            if (Track.Name != "Root hips" || Keys.Num() < 2) {
                continue;
            }
            const float Difference{Keys[0].Y - Keys[Keys.Num() - 1].Y};
            for (int32 Key{}; Key < Keys.Num(); ++Key) {
                Keys[Key].Y += Difference * static_cast<float>(Key) / static_cast<float>(Keys.Num() - 1);
            }
        }
    }

    nlohmann::json Measure(const FFbxAssetData& Asset, const FReferenceSkeleton& Skeleton) {
        const auto& Animation{Asset.Animation};
        std::unordered_map<FString, const FBoneAnimationTrack*> Tracks{};
        for (const auto& Track : Animation.BoneAnimationTracks) {
            Tracks.emplace(Track.Name, &Track);
        }
        const int32 Count{Skeleton.GetNum()};
        TArray<FMatrix> World{};
        World.SetNum(Count);
        nlohmann::json Frames{nlohmann::json::array()};
        FVector FirstRoot{};
        FVector LastRoot{};
        float Travel{};
        for (int32 Key{}; Key < Animation.NumberOfKeys; ++Key) {
            nlohmann::json Frame{};
            for (int32 Bone{}; Bone < Count; ++Bone) {
                const auto& Info{Skeleton.RefBoneInfo[Bone]};
                FTransform Pose{Skeleton.RefBonePose[Bone]};
                if (const auto Found{Tracks.find(Info.Name)}; Found != Tracks.end()) {
                    const auto& Raw{Found->second->InternalTrackData};
                    if (!Raw.PosKeys.IsEmpty()) {
                        Pose.Location = Raw.PosKeys[std::min(Key, Raw.PosKeys.Num() - 1)];
                    }
                    if (!Raw.RotKeys.IsEmpty()) {
                        Pose.Rotation = Raw.RotKeys[std::min(Key, Raw.RotKeys.Num() - 1)].ToFRotator();
                    }
                    if (!Raw.ScaleKeys.IsEmpty()) {
                        Pose.Scale = Raw.ScaleKeys[std::min(Key, Raw.ScaleKeys.Num() - 1)];
                    }
                }
                World[Bone] = Info.ParentIndex >= 0 ? Pose.GetLocalMatrix() * World[Info.ParentIndex] : Pose.GetLocalMatrix();
                const FVector Position{World[Bone].TransformPosition(FVector{})};
                if (Bone == 0) {
                    if (Key == 0) {
                        FirstRoot = Position;
                    } else {
                        Travel += (Position - LastRoot).Length();
                    }
                    LastRoot = Position;
                    Frame["Root"] = {Position.X, Position.Y, Position.Z};
                }
                if (Info.Name == "Hand base left" || Info.Name == "Hand base right" || Info.Name == "Root hips" || Info.Name == "Leg left ankle" || Info.Name == "Leg right ankle" || Info.Name == "Leg left knee" || Info.Name == "Leg right knee") {
                    Frame[Info.Name] = {Position.X, Position.Y, Position.Z};
                }
            }
            Frames.push_back(std::move(Frame));
        }
        return {{"Length", (Animation.NumberOfKeys - 1) / Animation.FrameRate}, {"Rate", Animation.FrameRate}, {"RootDistance", (LastRoot - FirstRoot).Length()}, {"RootTravel", Travel}, {"Frames", std::move(Frames)}};
    }
}

int main() {
    const FString SkeletonKey{"Imported/Devil Jin/SKEL_Devil Jin_Root ground 2.fbxbin"};
    FString Error{};
    FFbxAssetData SkeletonAsset{};
    if (!FFbxAssetBake::ReadBaked("Assets/" + SkeletonKey, SkeletonAsset, Error)) {
        std::fprintf(stderr, "%s\n", Error.c_str());
        return 1;
    }
    FFbxImportOptions Options{};
    Options.bImportOnlyAnimations = true;
    Options.bImportAnimations = true;
    Options.bRetargetHumanoid = true;
    Options.bRetargetRootMotion = true;
    Options.AnimationSkeletonKey = SkeletonKey;
    const FString Output{"Assets/DevilDuel/Motion"};
    std::filesystem::create_directories(Output);
    std::vector<std::filesystem::path> Sources{};
    for (const auto& Entry : std::filesystem::directory_iterator{"Assets/Models/TekkenAnim"}) {
        if (Entry.path().extension() == ".fbx") {
            Sources.push_back(Entry.path());
        }
    }
    Sources.emplace_back("Assets/Models/Bouncing Fight Idle.fbx");
    std::sort(Sources.begin(), Sources.end());
    nlohmann::json Metadata{};
    for (const auto& Source : Sources) {
        TArray<FFbxAssetData> Assets{};
        TArray<FString> Warnings{};
        if (!FFbxImporter::Cook(FbxPath::ToString(Source), Options, Output, Assets, Warnings, Error, Output)) {
            std::fprintf(stderr, "%s: %s\n", Source.string().c_str(), Error.c_str());
            return 1;
        }
        for (auto& Asset : Assets) {
            if (Asset.Type != EFbxAssetType::AnimSequence) {
                continue;
            }
            const FString Name{FbxPath::ToString(Source.stem())};
            if (Name == "Long Step Forward") {
                MatchLoopHeight(Asset);
            }
            Asset.Key = "DevilDuel/Motion/" + Name + ".fbxbin";
            if (!FFbxAssetBake::WriteBaked("Assets/" + Asset.Key, Asset, Error)) {
                std::fprintf(stderr, "%s\n", Error.c_str());
                return 1;
            }
            Metadata[Name] = Measure(Asset, SkeletonAsset.RefSkeleton);
            Metadata[Name]["Path"] = Asset.Key;
            std::printf("Cooked %s, root travel %.3f m\n", Name.c_str(), Metadata[Name]["RootTravel"].get<float>());
        }
    }
    std::ofstream File{"Build/DevilDuelMotionMetrics.json"};
    File << Metadata.dump(2);
    return 0;
}
