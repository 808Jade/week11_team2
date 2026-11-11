-- 이동 및 입력 상태를 그래프 변수로 전달한다. 상태 선택, 조건 검사와 전환 블렌딩은 C++이 수행한다.
local Clips <const> = dofile("Scripts/state_machine_clips.lua")
local Mesh = nil
local Camera = nil
local Hud = nil
local Readouts = {}
local Spawn = nil
local Time = 0.0
local HudAge = 0.0
local Speed = 0.0
local VelocityX = 0.0
local VelocityY = 0.0
local VerticalSpeed = 0.0
local Yaw = 0.0
local Pitch = -17.0
local Facing = 0.0
local Grounded = true
local Crouch = false
local Aim = false
local Swim = false
local Dance = false
local Dead = false
local Browser = false
local Action = false
local Hit = false
local BusyAge = 0.0
local Busy = ""
local Automatic = false
local AutoTime = 0.0
local AutoStage = -1
local BrowserIndex = 9
local DemoStage = -1
local IsPlayer = false
local IsGallery = false

local function Clamp(Value, Minimum, Maximum)
    return math.max(Minimum, math.min(Maximum, Value))
end

local function ClipIndex(Name)
    -- Lua 배열은 1 기반이고 클립 선택용 Blend Space 좌표는 0 기반이다.
    for Index, Clip in ipairs(Clips) do
        if Clip == Name then
            return Index - 1
        end
    end
    error("Unknown animation clip: " .. Name)
end

local function SetReadout(Name, Text)
    local Readout = Readouts[Name]
    if Readout and Readout.Text ~= Text then
        Readout.Widget:SetText(Text)
        Readout.Text = Text
    end
end

local function UpdateAnimation()
    -- 포즈 입력용 Float와 상태 전환용 Bool을 같은 애니메이션 인스턴스에 갱신한다.
    Mesh:SetAnimGraphFloat("Speed", Speed)
    Mesh:SetAnimGraphFloat("VerticalSpeed", VerticalSpeed)
    Mesh:SetAnimGraphFloat("AimPitch", Clamp((Pitch + 17.0) / 35.0, -1.0, 1.0))
    Mesh:SetAnimGraphFloat("AimWeight", Clamp(1.0 - Speed / 5.0, 0.45, 1.0))
    Mesh:SetAnimGraphFloat("BrowserClip", BrowserIndex)
    Mesh:SetAnimGraphBool("IsInAir", not Grounded)
    Mesh:SetAnimGraphBool("IsCrouching", Crouch)
    Mesh:SetAnimGraphBool("IsSwimming", Swim)
    Mesh:SetAnimGraphBool("IsAiming", Aim)
    Mesh:SetAnimGraphBool("WantsAction", Action)
    Mesh:SetAnimGraphBool("WantsHit", Hit)
    Mesh:SetAnimGraphBool("IsDancing", Dance)
    Mesh:SetAnimGraphBool("IsDead", Dead)
    Mesh:SetAnimGraphBool("IsBrowser", Browser)
end

local function ResetPlayer()
    obj.Location = FVector(Spawn.x, Spawn.y, Spawn.z)
    VelocityX, VelocityY, VerticalSpeed, Speed = 0.0, 0.0, 0.0, 0.0
    Grounded, Crouch, Aim, Swim, Dance, Dead, Browser, Action, Hit = true, false, false, false, false, false, false, false, false
    Yaw = IsGallery and 90.0 or 0.0
    Pitch, Facing = -17.0, Yaw
    Busy, BusyAge = "", 0.0
    Automatic, AutoTime, AutoStage = false, 0.0, -1
    UpdateAnimation()
end

local function StartAction(Name)
    -- 재생할 클립 좌표와 요청 플래그를 지정하면 상태 머신의 진입 정책이 재생을 시작한다.
    Mesh:SetAnimGraphFloat("ActionClip", ClipIndex(Name))
    Action, Hit, Dance, Busy, BusyAge = true, false, false, "Action", 0.0
end

local function StartHit()
    Mesh:SetAnimGraphFloat("HitClip", ClipIndex("Hit_Chest"))
    Hit, Action, Dance, Busy, BusyAge = true, false, false, "Hit", 0.0
end

local function Jump()
    if Grounded and not Crouch and not Swim and not Dead and Busy == "" then
        Grounded, VerticalSpeed = false, 6.0
        Dance = false
    end
end

local function GroundHeight(X, Y)
    if not IsGallery and Y >= 4.5 and Y <= 7.5 and X >= -2.25 and X <= 2.25 then
        return 0.03 + (math.min(2, math.floor((X + 2.25) / 1.5)) + 1) * 0.3
    end
    return 0.03
end

local function UpdateControls()
    if IsKeyPressed("backspace") then
        ResetPlayer()
    end
    if IsKeyPressed("g") then
        OpenLevel(IsGallery and "Scenes/Demo_StateMachineLab.scene" or "Scenes/Demo_StateMachineClips.scene")
        return
    end
    if IsKeyPressed("b") then
        Automatic, AutoTime, AutoStage = not Automatic, 0.0, -1
    end
    if Automatic then
        return
    end
    if IsKeyPressed("tab") and Grounded and not Dead then
        Browser, Dance, Action, Hit, Busy = not Browser, false, false, false, ""
    end
    if Browser then
        local Change = (IsKeyPressed("2") and 1 or 0) - (IsKeyPressed("1") and 1 or 0)
        BrowserIndex = (BrowserIndex + Change + #Clips) % #Clips
        return
    end
    if IsKeyPressed("y") then
        Dead, Dance, Action, Hit, Busy = true, false, false, false, ""
    end
    if Dead then
        return
    end
    if IsKeyPressed("c") and Grounded then
        Crouch, Swim = not Crouch, false
    end
    if IsKeyPressed("v") and Grounded then
        Swim, Crouch = not Swim, false
    end
    if IsKeyPressed("t") or IsRightMouseButtonPressed() then
        Aim = not Aim
    end
    if IsKeyPressed("space") then
        Jump()
    end
    if IsKeyPressed("h") then
        StartHit()
    elseif Grounded and Busy == "" and not Swim then
        if IsKeyPressed("f") then
            Dance = not Dance
        elseif IsKeyPressed("q") then
            StartAction("Roll")
        elseif IsKeyPressed("e") then
            StartAction("Interact")
        elseif IsKeyPressed("r") then
            StartAction("Pistol_Reload")
        elseif IsKeyPressed("x") then
            StartAction("Sword_Attack")
        elseif IsKeyPressed("k") then
            StartAction("Spell_Simple_Shoot")
        elseif IsLeftMouseButtonPressed() then
            StartAction(Aim and "Pistol_Shoot" or "Punch_Jab")
        end
    end
end

local function UpdateGuided(DeltaTime)
    -- 3초 단위의 열 단계에서 이동, 자세와 단발 동작을 순서대로 시연한다.
    AutoTime = AutoTime + DeltaTime
    local Stage = math.floor(AutoTime / 3.0) % 10
    if Stage ~= AutoStage then
        AutoStage = Stage
        Crouch, Aim, Swim, Dance, Browser, Dead = Stage == 2, Stage == 4, Stage == 9, Stage == 8, false, false
        if Stage == 0 then
            obj.Location = FVector(Spawn.x, Spawn.y, Spawn.z)
        elseif Stage == 3 then
            Jump()
        elseif Stage == 5 then
            StartAction("Roll")
        elseif Stage == 6 then
            StartAction("Sword_Attack")
        elseif Stage == 7 then
            StartHit()
        end
    end
    if Stage == 4 then
        Pitch = -17.0 + math.sin(AutoTime * 1.7) * 30.0
    end
end

local function MovePlayer(DeltaTime)
    -- 카메라 방향 기준 이동과 샘플용 중력 및 계단 높이 계산으로 그래프에 넣을 속도를 구한다.
    Yaw = Yaw + ((IsKeyDown("right") and 1 or 0) - (IsKeyDown("left") and 1 or 0)) * 85.0 * DeltaTime
    Pitch = Clamp(Pitch + ((IsKeyDown("up") and 1 or 0) - (IsKeyDown("down") and 1 or 0)) * 45.0 * DeltaTime, -45.0, 15.0)
    local Forward = (IsKeyDown("w") and 1 or 0) - (IsKeyDown("s") and 1 or 0)
    local Right = (IsKeyDown("d") and 1 or 0) - (IsKeyDown("a") and 1 or 0)
    local Radians = math.rad(Yaw)
    local X = math.cos(Radians) * Forward - math.sin(Radians) * Right
    local Y = math.sin(Radians) * Forward + math.cos(Radians) * Right
    local TargetSpeed = Swim and 2.2 or (Crouch or Aim) and 2.1 or IsKeyDown("shift") and 5.0 or IsKeyDown("z") and 1.6 or 3.2
    if Automatic then
        X, Y = AutoStage <= 2 and (AutoStage == 2 and -1 or 1) or 0, 0
        TargetSpeed = AutoStage == 1 and 5.0 or AutoStage == 2 and 2.1 or 1.6
    end
    local Length = math.sqrt(X * X + Y * Y)
    if Length > 1.0 then
        X, Y = X / Length, Y / Length
    end
    if Dead or Dance or Browser or Busy ~= "" then
        X, Y = 0.0, 0.0
    end
    local Response = math.min(1.0, DeltaTime * 12.0)
    VelocityX = VelocityX + (X * TargetSpeed - VelocityX) * Response
    VelocityY = VelocityY + (Y * TargetSpeed - VelocityY) * Response
    local Previous = obj.Location
    local NextX = Clamp(Previous.x + VelocityX * DeltaTime, -14.0, 14.0)
    local NextY = Clamp(Previous.y + VelocityY * DeltaTime, -10.0, IsGallery and 34.0 or 10.0)
    local Height = GroundHeight(NextX, NextY)
    if Grounded and Height > Previous.z + 0.44 then
        NextX, NextY, Height = Previous.x, Previous.y, GroundHeight(Previous.x, Previous.y)
    end
    VerticalSpeed = VerticalSpeed - 15.0 * DeltaTime
    local NextZ = Previous.z + VerticalSpeed * DeltaTime
    local Landed = VerticalSpeed <= 0.0 and (NextZ <= Height or Grounded and NextZ - Height < 0.1)
    if Landed then
        NextZ, VerticalSpeed = Height, 0.0
    end
    Grounded = Landed
    Speed = DeltaTime > 0.0 and math.sqrt((NextX - Previous.x)^2 + (NextY - Previous.y)^2) / DeltaTime or 0.0
    if Speed > 0.1 then
        Facing = math.deg(math.atan(VelocityY, VelocityX))
    end
    obj.Location = FVector(NextX, NextY, NextZ)
    obj.Rotation = FRotator(0.0, Aim and Yaw or Facing, 0.0)
    if Camera then
        Camera.Location = FVector(NextX, NextY, NextZ)
        SetCameraRelativeTransform(Camera, FVector(-math.cos(Radians) * 7.0, -math.sin(Radians) * 7.0, 3.3), FRotator(Pitch, Yaw, 0.0))
    end
end

local function UpdateDemo(DeltaTime)
    -- 주변 시연 캐릭터는 각자의 변수와 재생 인스턴스로 블렌딩, 액션, 조준과 점프를 반복한다.
    if obj.Name == "Demo Blend" then
        Speed = 2.5 + math.sin(Time * 0.6) * 2.5
        Crouch = math.floor(Time / 6.0) % 3 == 1
        Swim = math.floor(Time / 6.0) % 3 == 2
    elseif obj.Name == "Demo Action" then
        local Stage = math.floor(Time / 3.0)
        if Stage ~= DemoStage and Busy == "" then
            DemoStage = Stage
            local Actions = { "Punch_Jab", "Roll", "Interact", "Sword_Attack", "Spell_Simple_Shoot", "Pistol_Reload" }
            StartAction(Actions[Stage % #Actions + 1])
        end
    elseif obj.Name == "Demo Aim" then
        Aim = true
        Pitch = -17.0 + math.sin(Time) * 35.0
    elseif obj.Name == "Demo Jump" then
        local Stage = math.floor(Time / 4.0)
        if Stage ~= DemoStage then
            DemoStage = Stage
            Jump()
        end
        VerticalSpeed = VerticalSpeed - 15.0 * DeltaTime
        local Height = obj.Location.z + VerticalSpeed * DeltaTime
        Grounded = Height <= Spawn.z and VerticalSpeed <= 0.0
        if Grounded then
            Height, VerticalSpeed = Spawn.z, 0.0
        end
        obj.Location = FVector(Spawn.x, Spawn.y, Height)
    end
end

local function UpdateHud()
    SetReadout("Mode", Automatic and "AUTO / 30 SECOND GUIDED DEMO" or "MANUAL / B: GUIDED DEMO")
    SetReadout("Status", string.format("INPUTS | InAir %s / Crouch %s / Aim %s / Swim %s / Dance %s", tostring(not Grounded), tostring(Crouch), tostring(Aim), tostring(Swim), tostring(Dance)))
    SetReadout("Detail", string.format("Speed %.2f / VerticalSpeed %.2f / Action %s / Hit %s / Dead %s", Speed, VerticalSpeed, tostring(Action), tostring(Hit), tostring(Dead)))
    SetReadout("Browser", Browser and string.format("CLIP %02d / 43 : %s   [1 / 2 previous / next, TAB close]", BrowserIndex + 1, Clips[BrowserIndex + 1]) or "TAB: 43-CLIP BROWSER / G: " .. (IsGallery and "RETURN TO MOTION YARD" or "OPEN CLIP GALLERY"))
end

function BeginPlay()
    Mesh = assert(GetComponentByName("USkeletalMeshComponent"), "State Machine Lab requires a Skeletal Mesh Component")
    Spawn = obj.Location
    Spawn = FVector(Spawn.x, Spawn.y, Spawn.z)
    IsGallery = obj.Name == "Gallery Player"
    IsPlayer = IsGallery or obj.Name == "State Machine Player"
    assert(Mesh:SetAnimGraphBool("IsInAir", false), "Assign StateMachineLab/ThirdPerson.animgraph")
    if IsPlayer then
        Camera = GetActorByName("State Machine Camera")
        Hud = assert(CreateWidget("Assets/StateMachineLab/Telemetry.widget"), "State Machine Lab HUD is missing")
        for _, Name in ipairs({ "Mode", "Status", "Detail", "Browser" }) do
            Readouts[Name] = { Widget = assert(Hud:Find(Name), Name), Text = "" }
        end
        Hud:AddToViewport(10)
        ResetPlayer()
        UpdateHud()
    end
    UpdateAnimation()
end

function Tick(DeltaTime)
    local Step = Clamp(DeltaTime, 0.0, 0.05)
    Time, BusyAge, HudAge = Time + Step, BusyAge + Step, HudAge + Step
    -- 요청 직후 이전 클립의 종료 값에 반응하지 않도록 진입 여유를 두고 완료 후 요청을 해제한다.
    if BusyAge > 0.12 and Busy ~= "" and Mesh:IsAnimGraphPlayerFinished(Busy == "Action" and "Action Player" or "Hit Player") then
        Action, Hit, Busy = false, false, ""
    end
    if IsPlayer then
        UpdateControls()
        if Automatic then
            UpdateGuided(Step)
        end
        MovePlayer(Step)
        if HudAge >= 0.1 then
            HudAge = 0.0
            UpdateHud()
        end
    else
        UpdateDemo(Step)
    end
    UpdateAnimation()
end

function EndPlay()
    if Hud and Hud:IsValid() then
        Hud:Destroy()
    end
end
