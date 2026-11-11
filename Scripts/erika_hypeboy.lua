local hypeboy = "Imported/Hype_boy/AS_Hype_boy_Take 001.fbxbin"

function BeginPlay()
    local mesh = assert(GetComponentByName("USkeletalMeshComponent"), "Erika needs a SkeletalMeshComponent")
    mesh:PlayAnimation(hypeboy, true, 1.0)
end
