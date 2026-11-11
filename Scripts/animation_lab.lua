-- 자동 속도 변화와 수동 조절로 Blend Space를 비교하고 단발 Punch 포즈를 혼합하는 실습이다.
local ActionNode <const> = "Punch (One Shot)"
local CycleDuration <const> = 12.0
local ActionCue <const> = 8.8
local Sweep <const> = {
    { 0.0, 0.0 },
    { 1.2, 0.0 },
    { 3.0, 2.0 },
    { 4.0, 2.0 },
    { 6.0, 5.0 },
    { 7.0, 5.0 },
    { 8.6, 2.0 },
    { 10.1, 2.0 },
    { 12.0, 0.0 }
}

local Mesh = nil
local Hud = nil
local Readouts = {}
local IsLive = false
local IsAutomatic = true
local Elapsed = 0.0
local Speed = 0.0
local ActionAlpha = 0.0
local ActionState = "READY"

local function GetSweepSpeed(Time)
    -- 구간의 양 끝에서 기울기가 0인 보간으로 속도 프리셋 사이를 왕복한다.
    for Index = 2, #Sweep do
        local Next = Sweep[Index]
        if Time <= Next[1] then
            local Previous = Sweep[Index - 1]
            local Phase = (Time - Previous[1]) / (Next[1] - Previous[1])
            local Weight = Phase * Phase * (3.0 - 2.0 * Phase)
            return Previous[2] + (Next[2] - Previous[2]) * Weight
        end
    end

    return 0.0
end

local function SetReadout(Name, Value)
    local Readout = Readouts[Name]
    if Readout.Text ~= Value then
        Readout.Widget:SetText(Value)
        Readout.Text = Value
    end
end

local function UpdateHud()
    local Idle = math.max(0.0, 1.0 - Speed / 2.0)
    local Run = math.max(0.0, (Speed - 2.0) / 3.0)
    local Walk = 1.0 - Idle - Run

    SetReadout("Mode", IsAutomatic and "AUTO SWEEP" or "MANUAL SCRUB")
    SetReadout("Speed", string.format("%.2f", Speed))
    SetReadout("Idle", string.format("%3.0f%%", Idle * 100.0))
    SetReadout("Walk", string.format("%3.0f%%", Walk * 100.0))
    SetReadout("Run", string.format("%3.0f%%", Run * 100.0))
    SetReadout("Action", string.format("%3.0f%%", ActionAlpha * 100.0))
    SetReadout("ActionState", ActionState)
end

local function StartAction()
    if ActionState ~= "PLAYING" and Mesh:RestartAnimGraphPlayer(ActionNode) then
        ActionState = "PLAYING"
    end
end

local function UpdateAction(DeltaTime)
    -- 클립 종료 여부는 C++ 플레이어에서 읽고 Lua는 진입 및 복귀의 블렌딩 비율을 조절한다.
    if ActionState == "PLAYING" then
        if Mesh:IsAnimGraphPlayerFinished(ActionNode) then
            ActionState = "FADE OUT"
        else
            ActionAlpha = math.min(1.0, ActionAlpha + DeltaTime / 0.18)
        end
    end

    if ActionState == "FADE OUT" then
        ActionAlpha = math.max(0.0, ActionAlpha - DeltaTime / 0.36)
        if ActionAlpha == 0.0 then
            ActionState = "READY"
        end
    end
    Mesh:SetAnimGraphFloat("ActionAlpha", ActionAlpha)
end

function BeginPlay()
    Mesh = assert(GetComponentByName("USkeletalMeshComponent"), "Animation Lab requires a Skeletal Mesh Component")
    local InitialSpeed = obj.Name == "Run Reference" and 5.0 or 0.0
    assert(Mesh:SetAnimGraphFloat("Speed", InitialSpeed), "Animation Lab graph is not assigned")
    -- 기준 캐릭터는 고정 속도로 재생하고 Live Graph 한 캐릭터만 입력과 HUD를 담당한다.
    if obj.Name ~= "Live Graph" then
        return
    end

    Hud = assert(CreateWidget("Assets/AnimationLab/Telemetry.widget"), "Animation Lab HUD is missing")
    for _, Name in ipairs({ "Mode", "Speed", "Idle", "Walk", "Run", "Action", "ActionState" }) do
        Readouts[Name] = { Widget = assert(Hud:Find(Name), Name), Text = "" }
    end
    Hud:AddToViewport(10)
    IsLive = true
    UpdateHud()
end

function Tick(DeltaTime)
    if not IsLive then
        return
    end

    if IsKeyPressed("r") then
        IsAutomatic = true
        Elapsed = 0.0
    end

    for Index, Preset in ipairs({ 0.0, 2.0, 5.0 }) do
        if IsKeyPressed(tostring(Index)) then
            IsAutomatic = false
            Speed = Preset
        end
    end

    local Direction = (IsKeyDown("right") and 1 or 0) - (IsKeyDown("left") and 1 or 0)
    if Direction ~= 0 then
        IsAutomatic = false
        Speed = math.max(0.0, math.min(5.0, Speed + Direction * 1.65 * DeltaTime))
    end

    if IsAutomatic then
        local Previous = Elapsed
        Elapsed = Elapsed + DeltaTime
        Speed = GetSweepSpeed(Elapsed % CycleDuration)
        if math.floor((Elapsed - ActionCue) / CycleDuration) > math.floor((Previous - ActionCue) / CycleDuration) then
            StartAction()
        end
    end

    if IsKeyPressed("space") then
        StartAction()
    end

    Mesh:SetAnimGraphFloat("Speed", Speed)
    UpdateAction(DeltaTime)
    UpdateHud()
end

function EndPlay()
    if Hud and Hud:IsValid() then
        Hud:Destroy()
    end
end
