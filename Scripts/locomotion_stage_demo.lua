local Library <const> = dofile("Scripts/locomotion_stage_library.lua")
local Mesh = nil
local Spawn = nil
local Time, Age = 0.0, 0.0
local PreviousStage = -1
local Busy = false

local function IndexOf(Name)
    for Index, Clip in ipairs(Library.Clips) do
        if Clip == Name then return Index - 1 end
    end
    error("Unknown demo clip: " .. Name)
end

function BeginPlay()
    Mesh = assert(GetComponentByClass("USkeletalMeshComponent"))
    local Position = obj.Location
    Spawn = FVector(Position.x, Position.y, Position.z)
end

function Tick(DeltaTime)
    local Step = math.min(DeltaTime, 0.05)
    Time, Age = Time + Step, Age + Step
    if obj.Name == "Flow Demonstrator" then
        obj.Location = FVector(Spawn.x + math.sin(Time * 0.8) * 2.0, Spawn.y, Spawn.z)
        obj.Rotation = FRotator(0.0, math.cos(Time * 0.8) >= 0.0 and 0.0 or 180.0, 0.0)
        Mesh:SetAnimGraphBool("IsCrouching", math.floor(Time / 6.0) % 2 == 1)
    elseif obj.Name == "Aim Demonstrator" then
        Mesh:SetAnimGraphBool("IsAiming", true)
        Mesh:SetAnimGraphFloat("AimPitch", math.sin(Time * 0.8))
        Mesh:SetAnimGraphFloat("AimWeight", 0.85)
    elseif obj.Name == "Action Demonstrator" then
        if Busy and Age > 0.15 and Mesh:IsAnimGraphPlayerFinished("Action Player") then Busy = false; Mesh:SetAnimGraphBool("WantsAction", false) end
        local Stage = math.floor(Time / 4.0)
        if Stage ~= PreviousStage and not Busy then
            PreviousStage, Age, Busy = Stage, 0.0, true
            local Actions = { "Punch_Jab", "Punch_Cross", "Sword_Attack", "Pistol_Reload", "Spell_Simple_Shoot", "Interact", "PickUp_Table" }
            Mesh:SetAnimGraphFloat("ActionClip", IndexOf(Actions[Stage % #Actions + 1]))
            Mesh:SetAnimGraphBool("WantsAction", true)
        end
    elseif obj.Name == "Root Demonstrator" then
        Mesh:SetAnimGraphFloat("Speed", 1.6)
        local Position = obj.Location
        if math.abs(Position.x - Spawn.x) > 6.0 then obj.Location = FVector(Spawn.x, Spawn.y, Spawn.z) end
    end
end
