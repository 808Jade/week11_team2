local Library <const> = dofile("Scripts/locomotion_stage_library.lua")
local Zones <const> = {
    { Name = "01 / FLOW", X = -9, Y = 0, Z = 0.03 },
    { Name = "02 / TRAVERSE", X = -1, Y = -12, Z = 0.03 },
    { Name = "03 / COMBAT", X = 13.5, Y = 5, Z = 0.03 },
    { Name = "04 / ROOT MOTION", X = -9, Y = -6, Z = 0.03 },
    { Name = "05 / CLIP LAB", X = -8, Y = 12, Z = 0.03 }
}
local Rates <const> = { 0.25, 0.5, 1.0, 1.5, 2.0 }
local Mesh = nil
local Camera = nil
local Hud = nil
local Readouts = {}
local HelpLines = {}
local Mode = 1
local PreviousMode = 1
local Time = 0.0
local HudAge = 0.0
local Yaw = 0.0
local Pitch = -18.0
local Facing = 0.0
local CameraX, CameraY, CameraZ = -9.0, 0.0, 0.03
local VelocityX, VelocityY, VerticalSpeed, Speed, ControlSpeed = 0.0, 0.0, 0.0, 0.0, 0.0
local LastWrittenX, LastWrittenY = -9.0, 0.0
local Grounded, Crouch, Aim, Swim, Dance, Dead, Seated = true, false, false, false, false, false, false
local Action, Hit = false, false
local AimLatch, SwimLatch = false, false
local Busy, BusyAge = "", 0.0
local ClipIndex, RateIndex = 10, 3
local Paused, Reverse, InspectorMotion, HelpVisible = false, false, false, false
local NotifyCount, FootCount = 0, 0
local NotifyText, NotifyAge, AttackWindow, DodgeWindow = "READY", 0.0, false, false
local Automatic, AutoTime, AutoStep = false, 0.0, -1

local function Clamp(Value, Minimum, Maximum)
    return math.max(Minimum, math.min(Maximum, Value))
end

local function AngleDelta(Target, Current)
    return (Target - Current + 180.0) % 360.0 - 180.0
end

local function Readout(Name, Value)
    local Entry = Readouts[Name]
    if Entry and Entry.Value ~= Value then
        Entry.Widget:SetText(Value)
        Entry.Value = Value
    end
end

local function IndexOf(Name)
    for Index, Clip in ipairs(Library.Clips) do
        if Clip == Name then return Index - 1 end
    end
    error("Unknown stage clip: " .. Name)
end

local function UpdateGraph()
    if Mode == 5 then return end
    Mesh:SetAnimGraphFloat("Speed", Mode == 4 and ControlSpeed or Speed)
    Mesh:SetAnimGraphFloat("VerticalSpeed", VerticalSpeed)
    Mesh:SetAnimGraphFloat("AimPitch", Clamp((Pitch + 18.0) / 35.0, -1.0, 1.0))
    Mesh:SetAnimGraphFloat("AimWeight", Clamp(1.0 - Speed / 7.0, 0.38, 1.0))
    Mesh:SetAnimGraphBool("IsInAir", not Grounded)
    Mesh:SetAnimGraphBool("IsCrouching", Crouch)
    Mesh:SetAnimGraphBool("IsAiming", Aim)
    Mesh:SetAnimGraphBool("IsSwimming", Swim)
    Mesh:SetAnimGraphBool("IsDancing", Dance)
    Mesh:SetAnimGraphBool("IsDead", Dead)
    Mesh:SetAnimGraphBool("IsSeated", Seated)
    Mesh:SetAnimGraphBool("WantsAction", Action)
    Mesh:SetAnimGraphBool("WantsHit", Hit)
end

local function PlayClip()
    local Kind = InspectorMotion and "RootMotion" or "InPlace"
    local Clip = Library.Clips[ClipIndex]
    Mesh:SetRootMotionEnabled(InspectorMotion)
    Mesh:PlayAnimation(Library[Kind][Clip], true, Rates[RateIndex] * (Reverse and -1.0 or 1.0))
    Mesh:SetAnimationPlaying(not Paused)
    AttackWindow, DodgeWindow = false, false
end

local function EnterZone(Index)
    Mode = Index
    local Zone = Zones[Index]
    obj.Location = FVector(Zone.X, Zone.Y, Zone.Z)
    obj.Rotation = FRotator(0.0, 0.0, 0.0)
    CameraX, CameraY, CameraZ = Zone.X, Zone.Y, Zone.Z
    LastWrittenX, LastWrittenY = Zone.X, Zone.Y
    VelocityX, VelocityY, VerticalSpeed, Speed, ControlSpeed = 0.0, 0.0, 0.0, 0.0, 0.0
    Grounded, Crouch, Aim, Swim, Dance, Dead, Seated, Action, Hit = true, false, false, false, false, false, false, false, false
    Busy, BusyAge, Facing, Yaw, Pitch = "", 0.0, 0.0, 0.0, -18.0
    AttackWindow, DodgeWindow, AimLatch, SwimLatch = false, false, false, false
    Mesh:SetRootMotionEnabled(Index == 4)
    if Index == 5 then
        Mesh:SetAnimGraph("")
        PlayClip()
    else
        Mesh:SetAnimGraph("LocomotionStage/Graphs/AG_" .. (Index == 4 and "RootMotion" or "InPlace") .. ".animgraph")
        UpdateGraph()
    end
    Readouts.ClipBackdrop.Widget.Visible = Mode == 5
    Readout("Zone", Zone.Name)
end

local function StartAction(Name)
    if Dead or not Grounded or Seated then return end
    Mesh:SetAnimGraphFloat("ActionClip", IndexOf(Name))
    Action, Hit, Dance, Busy, BusyAge = true, false, false, "Action", 0.0
end

local function StartHit()
    if Dead then return end
    Mesh:SetAnimGraphFloat("HitClip", IndexOf("Hit_Chest"))
    Hit, Action, Dance, Seated, Busy, BusyAge = true, false, false, false, "Hit", 0.0
end

local function Jump()
    if Grounded and not Crouch and not Swim and not Dead and not Seated and Busy == "" then
        Grounded, VerticalSpeed, Dance = false, 6.3, false
    end
end

local function UpdateControls()
    if IsKeyPressed("i") then
        HelpVisible = not HelpVisible
        Readouts.Help.Widget.Visible = HelpVisible
        Readouts.HelpBackdrop.Widget.Visible = HelpVisible
        for _, Line in ipairs(HelpLines) do Line.Visible = HelpVisible end
    end
    if IsKeyPressed("backspace") then
        Automatic = false
        EnterZone(Mode)
    end
    for Index = 1, 5 do
        if IsKeyPressed(tostring(Index)) then
            Automatic = false
            EnterZone(Index)
            return
        end
    end
    if IsKeyPressed("tab") then
        if Mode == 5 then EnterZone(PreviousMode) else PreviousMode = Mode; EnterZone(5) end
        return
    end
    if Mode == 5 then
        local Change = (IsKeyPressed("e") and 1 or 0) - (IsKeyPressed("q") and 1 or 0)
        if Change ~= 0 then
            ClipIndex = (ClipIndex - 1 + Change + #Library.Clips) % #Library.Clips + 1
            PlayClip()
        end
        if IsKeyPressed("m") then InspectorMotion = not InspectorMotion; PlayClip() end
        if IsKeyPressed("p") then Paused = not Paused; Mesh:SetAnimationPlaying(not Paused) end
        if IsKeyPressed("n") then
            Reverse = not Reverse
            Mesh:SetAnimationRate(Rates[RateIndex] * (Reverse and -1.0 or 1.0))
            Mesh:SetAnimationPlaying(not Paused)
        end
        if IsKeyPressed("o") then
            RateIndex = RateIndex % #Rates + 1
            Mesh:SetAnimationRate(Rates[RateIndex] * (Reverse and -1.0 or 1.0))
        end
        local Seek = (IsKeyPressed("l") and 1 or 0) - (IsKeyPressed("j") and 1 or 0)
        if Seek ~= 0 then Mesh:SetAnimationPosition(Mesh:GetAnimationTime() + Seek * Mesh:GetAnimationLength() * 0.1) end
        return
    end
    if IsKeyPressed("b") then
        Automatic, AutoTime, AutoStep = not Automatic, 0.0, -1
    end
    if Automatic then return end
    if IsKeyPressed("y") then Dead, Dance, Action, Hit, Seated, Busy = true, false, false, false, false, "" end
    if Dead then return end
    if IsKeyPressed("c") and Grounded then
        local Position = obj.Location
        local Ceiling = Crouch and RaycastBlocking(FVector(Position.x, Position.y, Position.z + 0.78), FVector(0.0, 0.0, 1.0), 0.95) or nil
        if not Ceiling then Crouch, Swim, SwimLatch, Seated = not Crouch, false, false, false end
    end
    if IsKeyPressed("t") then AimLatch = not AimLatch end
    Aim = AimLatch or IsRightMouseButtonDown()
    if IsKeyPressed("v") then SwimLatch, Crouch = not SwimLatch, false; Swim = SwimLatch end
    if IsKeyPressed("space") then Jump() end
    if IsKeyPressed("u") and Grounded and Busy == "" then Seated, Dance = not Seated, false end
    if IsKeyPressed("h") then
        StartHit()
    elseif Grounded and Busy == "" and not Seated and not Swim then
        if IsKeyPressed("f") then Dance = not Dance
        elseif IsKeyPressed("q") then StartAction("Roll")
        elseif IsKeyPressed("e") then StartAction("Interact")
        elseif IsKeyPressed("r") then StartAction("Pistol_Reload")
        elseif IsKeyPressed("x") then StartAction("Sword_Attack")
        elseif IsKeyPressed("k") then StartAction("Spell_Simple_Shoot")
        elseif IsLeftMouseButtonPressed() then StartAction(Aim and "Pistol_Shoot" or "Punch_Jab") end
    end
end

local function UpdateGuided(Step)
    AutoTime = AutoTime + Step
    local Stage = math.floor(AutoTime / 2.5) % 10
    if Stage ~= AutoStep then
        AutoStep = Stage
        Crouch, Aim, Dance, Swim, Seated = Stage == 2, Stage == 4, Stage == 8, Stage == 9, false
        if Stage == 0 then
            local Running = Automatic
            EnterZone(1)
            Automatic = Running
        elseif Stage == 3 then Jump()
        elseif Stage == 5 then StartAction("Roll")
        elseif Stage == 6 then StartAction("Sword_Attack")
        elseif Stage == 7 then StartHit() end
    end
    if Stage == 4 then Pitch = -18.0 + math.sin(AutoTime * 1.6) * 32.0 end
end

local function GroundHeight(X, Y, Z)
    local Point = RaycastBlocking(FVector(X, Y, Z + 0.45), FVector(0.0, 0.0, -1.0))
    return Point and Point.z + 0.025 or -8.0
end

local function Blocked(X, Y, Z, DeltaX, DeltaY)
    local Distance = math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY)
    if Distance < 0.0001 then return false end
    for _, Height in ipairs(Swim and { 1.2 } or { 0.42, Crouch and 0.84 or 1.46 }) do
        local Point = RaycastBlocking(FVector(X, Y, Z + Height), FVector(DeltaX, DeltaY, 0.0))
        if Point and math.sqrt((Point.x - X)^2 + (Point.y - Y)^2) < Distance + 0.27 then return true end
    end
    return false
end

local function MovePlayer(Step)
    local Current = obj.Location
    local Forward = (IsKeyDown("w") and 1 or 0) - (IsKeyDown("s") and 1 or 0)
    local Right = (IsKeyDown("d") and 1 or 0) - (IsKeyDown("a") and 1 or 0)
    local TargetSpeed = Swim and 2.2 or (Crouch or Aim) and 1.8 or IsKeyDown("shift") and 6.0 or IsKeyDown("z") and 1.6 or 3.5
    if Automatic then Forward, Right = AutoStep <= 2 and 1 or 0, 0; TargetSpeed = AutoStep == 1 and 6.0 or AutoStep == 2 and 1.8 or 1.6 end
    if Dead or Dance or Seated or Busy ~= "" or Mesh:GetAnimGraphState():find("Seated", 1, true) then Forward, Right = 0, 0 end
    local Length = math.sqrt(Forward * Forward + Right * Right)
    if Length > 1.0 then Forward, Right = Forward / Length, Right / Length end
    local Radians = math.rad(Yaw)
    local DesiredX = (math.cos(Radians) * Forward - math.sin(Radians) * Right) * TargetSpeed
    local DesiredY = (math.sin(Radians) * Forward + math.cos(Radians) * Right) * TargetSpeed
    local Response = 1.0 - math.exp(-Step * 11.0)
    VelocityX = VelocityX + (DesiredX - VelocityX) * Response
    VelocityY = VelocityY + (DesiredY - VelocityY) * Response
    ControlSpeed = math.sqrt(VelocityX * VelocityX + VelocityY * VelocityY)
    local NextX, NextY = Current.x, Current.y
    if Mode ~= 4 then
        local DeltaX, DeltaY = VelocityX * Step, VelocityY * Step
        if not Blocked(Current.x, Current.y, Current.z, DeltaX, 0.0) then NextX = NextX + DeltaX end
        if not Blocked(NextX, Current.y, Current.z, 0.0, DeltaY) then NextY = NextY + DeltaY end
    end
    local Pool = NextX > 5.0 and NextX < 13.0 and NextY > 10.0 and NextY < 17.0
    local WasPool = Current.x > 5.0 and Current.x < 13.0 and Current.y > 10.0 and Current.y < 17.0
    Swim = SwimLatch or Pool or Automatic and AutoStep == 9
    if Pool then Crouch = false end
    local Height = Pool and -0.72 or GroundHeight(NextX, NextY, WasPool and 0.05 or Current.z)
    local NextZ = WasPool and not Pool and Height or Current.z
    if Pool then
        NextZ = NextZ + (Height - NextZ) * math.min(1.0, Step * 7.0)
        VerticalSpeed, Grounded = 0.0, true
    else
        VerticalSpeed = VerticalSpeed - 17.0 * Step
        NextZ = NextZ + VerticalSpeed * Step
        Grounded = VerticalSpeed <= 0.0 and (NextZ <= Height or Grounded and Current.z - Height < 0.12)
        if Grounded then NextZ, VerticalSpeed = Height, 0.0 end
    end
    if ControlSpeed > 0.1 then
        local Target = Aim and Yaw or math.deg(math.atan(VelocityY, VelocityX))
        Facing = Facing + AngleDelta(Target, Facing) * math.min(1.0, Step * 14.0)
    elseif Aim then Facing = Facing + AngleDelta(Yaw, Facing) * math.min(1.0, Step * 14.0) end
    Speed = Step > 0.0 and math.sqrt((NextX - (Mode == 4 and LastWrittenX or Current.x))^2 + (NextY - (Mode == 4 and LastWrittenY or Current.y))^2) / Step or 0.0
    obj.Location = FVector(NextX, NextY, NextZ)
    obj.Rotation = FRotator(0.0, Facing, 0.0)
    LastWrittenX, LastWrittenY = NextX, NextY
    if NextZ < -5.0 or NextX < -17.0 or NextX > 32.0 or NextY < -19.0 or NextY > 19.0 then EnterZone(Mode) end
end

local function UpdateCamera(Step)
    local MouseX, MouseY = GetMouseDelta()
    Yaw = Yaw + MouseX * 0.16 + ((IsKeyDown("right") and 1 or 0) - (IsKeyDown("left") and 1 or 0)) * 78.0 * Step
    Pitch = Clamp(Pitch - MouseY * 0.13 + ((IsKeyDown("up") and 1 or 0) - (IsKeyDown("down") and 1 or 0)) * 48.0 * Step, -52.0, 20.0)
    local Position = obj.Location
    local Response = 1.0 - math.exp(-Step * 14.0)
    CameraX, CameraY, CameraZ = CameraX + (Position.x - CameraX) * Response, CameraY + (Position.y - CameraY) * Response, CameraZ + (Position.z - CameraZ) * Response
    local Distance = Mode == 5 and 3.9 or Aim and 3.7 or 5.4
    local YawRadians, PitchRadians = math.rad(Yaw), math.rad(Pitch)
    local OffsetX, OffsetY = -math.cos(YawRadians) * math.cos(PitchRadians) * Distance, -math.sin(YawRadians) * math.cos(PitchRadians) * Distance
    local OffsetZ = -math.sin(PitchRadians) * Distance
    local TargetHeight = Crouch and 0.8 or Swim and 0.9 or 1.25
    local Origin = FVector(CameraX, CameraY, CameraZ + TargetHeight)
    local Obstruction = RaycastBlocking(Origin, FVector(OffsetX, OffsetY, OffsetZ))
    if Obstruction then
        local HitDistance = math.sqrt((Obstruction.x - Origin.x)^2 + (Obstruction.y - Origin.y)^2 + (Obstruction.z - Origin.z)^2)
        local Factor = Clamp((HitDistance - 0.25) / Distance, 0.06, 1.0)
        OffsetX, OffsetY, OffsetZ = OffsetX * Factor, OffsetY * Factor, OffsetZ * Factor
    end
    Camera.Location = FVector(CameraX, CameraY, CameraZ)
    SetCameraRelativeTransform(Camera, FVector(OffsetX, OffsetY, TargetHeight + OffsetZ), FRotator(-Pitch, Yaw, 0.0))
end

local function UpdateHud()
    Readout("State", Mode == 5 and "SINGLE SEQUENCE / CLIP LAB" or Mesh:GetAnimGraphState():upper())
    Readout("Speed", string.format("%05.2f m/s", Speed))
    Readout("Motion", Mode == 4 and "ROOT MOTION / ANIMATION DRIVEN" or Mode == 5 and (InspectorMotion and "ROOT MOTION PREVIEW" or "IN-PLACE PREVIEW") or "IN-PLACE / OWNER SPEED")
    Readout("Notify", string.format("%03d EVENTS / %s", NotifyCount, NotifyText))
    Readout("Window", AttackWindow and "ATTACK WINDOW / ACTIVE" or DodgeWindow and "DODGE WINDOW / ACTIVE" or string.format("FOOTSTEPS %03d / WINDOW --", FootCount))
    Readout("Hint", Automatic and "B / STOP GUIDED DEMO" or Mode == 4 and "ROOT TRACK / MOVEMENT COMES FROM THE CLIP" or Mode == 5 and "Q / E CHANGE CLIP     TAB RETURN" or "B GUIDED DEMO / I FULL CONTROLS")
    if Mode == 5 then
        Readout("Clip", string.format("%02d / 43   %s   |   %.2f / %.2f s   %s %.2fx", ClipIndex, Library.Clips[ClipIndex], Mesh:GetAnimationTime(), Mesh:GetAnimationLength(), Paused and "PAUSED" or Reverse and "REVERSE" or "PLAY", Rates[RateIndex]))
        Readout("Controls", "Q / E CLIP   P PAUSE   N REVERSE   O RATE   J / L SEEK   M ROOT MOTION")
    else
        Readout("Clip", "")
        Readout("Controls", Mode == 3 and "LMB PUNCH   Q ROLL   X SWORD   K SPELL   E INTERACT   R RELOAD   U SIT" or "WASD MOVE   SHIFT SPRINT   Z WALK   SPACE JUMP   C CROUCH   RMB/T AIM")
    end
end

function OnAnimNotify(Name)
    NotifyCount, NotifyText, NotifyAge = NotifyCount + 1, Name, 0.0
    if Name == "Foot_L" or Name == "Foot_R" then FootCount = FootCount + 1 end
end

function OnAnimNotifyBegin(Name)
    OnAnimNotify(Name .. " BEGIN")
    if Name == "AttackWindow" then AttackWindow = true end
    if Name == "DodgeWindow" then DodgeWindow = true end
end

function OnAnimNotifyEnd(Name)
    OnAnimNotify(Name .. " END")
    if Name == "AttackWindow" then AttackWindow = false end
    if Name == "DodgeWindow" then DodgeWindow = false end
end

function BeginPlay()
    Mesh = assert(GetComponentByClass("USkeletalMeshComponent"), "Locomotion Stage needs its character mesh")
    Camera = assert(GetActorByName("Stage Camera"), "Locomotion Stage camera is missing")
    SetActiveCamera(Camera)
    Hud = assert(CreateWidget("Assets/LocomotionStage/StageHUD.widget"), "Locomotion Stage HUD is missing")
    for _, Name in ipairs({ "State", "Speed", "Motion", "Notify", "Window", "Zone", "Controls", "Hint", "Clip", "Help", "HelpBackdrop", "ClipBackdrop" }) do
        Readouts[Name] = { Widget = assert(Hud:Find(Name), Name), Value = "" }
    end
    Readouts.Help.Widget.Visible = false
    Readouts.HelpBackdrop.Widget.Visible = false
    Readout("Help", "CONTROLS / I TO CLOSE")
    for Index = 1, 15 do
        HelpLines[Index] = assert(Hud:Find("HelpLine" .. Index))
        HelpLines[Index].Visible = false
    end
    Hud:AddToViewport(10)
    EnterZone(1)
    UpdateCamera(1.0)
    UpdateHud()
end

function Tick(DeltaTime)
    local Step = Clamp(DeltaTime, 0.0, 0.05)
    Time, HudAge, NotifyAge, BusyAge = Time + Step, HudAge + Step, NotifyAge + Step, BusyAge + Step
    UpdateControls()
    if Mode ~= 5 then
        if BusyAge > 0.12 and Busy ~= "" and Mesh:IsAnimGraphPlayerFinished(Busy == "Action" and "Action Player" or "Hit Player") then Action, Hit, Busy = false, false, "" end
        if Automatic then UpdateGuided(Step) end
        MovePlayer(Step)
        UpdateGraph()
    else
        local Position = obj.Location
        Speed = Step > 0.0 and math.sqrt((Position.x - LastWrittenX)^2 + (Position.y - LastWrittenY)^2) / Step or 0.0
        LastWrittenX, LastWrittenY = Position.x, Position.y
        if math.abs(Position.x - Zones[5].X) > 22.0 or math.abs(Position.y - Zones[5].Y) > 12.0 then
            obj.Location = FVector(Zones[5].X, Zones[5].Y, Zones[5].Z)
            LastWrittenX, LastWrittenY = Zones[5].X, Zones[5].Y
        end
    end
    UpdateCamera(Step)
    if HudAge >= 0.08 then HudAge = 0.0; UpdateHud() end
end

function EndPlay()
    if Hud and Hud:IsValid() then Hud:Destroy() end
end
