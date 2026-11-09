local siu = "Imported/Ronaldo_60Hz/AS_Ronaldo_60Hz_Scene.fbxbin"

function BeginPlay()
    local mesh = assert(GetComponentByName("USkeletalMeshComponent"), "Erika needs a SkeletalMeshComponent")
    mesh:PlayAnimation(siu, true, 1.0)
end
