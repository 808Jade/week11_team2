local capoeira = "Imported/X Bot/AS_Capoeira_mixamo.com.fbxbin"

function BeginPlay()
    local mesh = assert(GetComponentByName("USkeletalMeshComponent"), "X Bot needs a SkeletalMeshComponent")
    mesh:PlayAnimation(capoeira, true, 1.0)
end
