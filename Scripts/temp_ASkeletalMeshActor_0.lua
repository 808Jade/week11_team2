-- 액터마다 별도 environment에서 실행된다. obj는 이 스크립트가 붙은 액터다.
local hypeboy = "Imported/Hype_boy/AS_Hype_boy_Take 001.fbxbin"

function BeginPlay()
    local mesh = assert(GetComponentByName("USkeletalMeshComponent"), "Ronaldo needs a SkeletalMeshComponent")
    mesh:PlayAnimation(hypeboy, true, 1.0)
end