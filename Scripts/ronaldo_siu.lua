-- Ronaldo(ValveBiped) Skeleton으로 리타깃한 SIU 애니메이션
local siu = "Imported/Ronaldo_60Hz/AS_Ronaldo_60Hz_Scene_d3e6d13682940572.fbxbin"

function BeginPlay()
    local mesh = assert(GetComponentByName("USkeletalMeshComponent"), "Ronaldo needs a SkeletalMeshComponent")
    mesh:PlayAnimation(siu, true, 1.0)
end
