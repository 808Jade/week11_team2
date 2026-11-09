-- Ronaldo(ValveBiped) Skeleton으로 리타깃한 Hype Boy 애니메이션
local hypeboy = "Imported/Hype_boy/AS_Hype_boy_Take 001_9fa03ecade33bc35.fbxbin"

function BeginPlay()
    local mesh = assert(GetComponentByName("USkeletalMeshComponent"), "Ronaldo needs a SkeletalMeshComponent")
    mesh:PlayAnimation(hypeboy, true, 1.0)
end
