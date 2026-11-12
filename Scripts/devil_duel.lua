local Rules = dofile("Scripts/devil_duel_rules.lua")
local Clips = dofile("Scripts/devil_duel_clips.lua")
local Match = nil
local Actors = {}
local Meshes = {}
local Revisions = { -1, -1 }
local VisualFighters = {}
local Camera = nil
local Hud = nil
local Widgets = {}
local HealthVisuals = {}
local CameraX, CameraY, CameraYaw, CameraDistance = 0.0, 0.0, -90.0, 7.0
local ManualPause, PlaybackPaused = false, false
local VisualClock = 0.0
local PresentationPhase = ""
local SlowMotionDelay = 0.0

local function Clamp(Value, Minimum, Maximum)
    return math.max(Minimum, math.min(Maximum, Value))
end

local function Text(Name, Value)
    local Entry = Widgets[Name]
    if Entry.Text ~= Value then
        Entry.Widget:SetText(Value)
        Entry.Text = Value
    end
end

local function Restart()
    StopAllCameraShakes(true)
    StopFOVPunch()
    StartHitStop(0.0, 1.0)
    Match = Rules.New(9137, true)
    _G.DevilDuel = Match
    Revisions, VisualFighters, HealthVisuals = { -1, -1 }, {}, {}
    ManualPause, PlaybackPaused, PresentationPhase, SlowMotionDelay = false, false, "", 0.0
    for Index = 1, 2 do
        Meshes[Index]:SetAnimationPlaying(true)
    end
end

local function ReadInput()
    if IsKeyPressed("r") then
        Restart()
    end
    if IsKeyPressed("t") then
        Rules.SetAI(Match, not Match.AI)
    end
    if IsKeyPressed("p") then
        ManualPause = not ManualPause
    end
    Match.Paused = ManualPause
    if PlaybackPaused ~= Match.Paused then
        for Index = 1, 2 do
            Meshes[Index]:SetAnimationPlaying(not Match.Paused)
        end
        if Match.Paused then
            StopAllCameraShakes(true)
            StopFOVPunch()
        end
        PlaybackPaused = Match.Paused
    end
    if Match.Paused then
        return
    end
    local Forward = (IsKeyDown("w") and 1.0 or 0.0) - (IsKeyDown("s") and 1.0 or 0.0)
    Match.Input.Forward = Forward
    Match.Input.Side = (IsKeyDown("d") and 1.0 or 0.0) - (IsKeyDown("a") and 1.0 or 0.0)
    Match.Input.Guard = IsKeyDown("shift") or Forward < 0.0
    for Index, Button in ipairs({ "J", "K", "U", "I" }) do
        if IsKeyPressed(Button) then
            Rules.Submit(Match, 1, Button, Forward)
            break
        end
    end
    if IsKeyPressed("space") then
        Rules.Dash(Match, 1)
    end
end

local function SyncFighters()
    for Index = 1, 2 do
        local Fighter = Match.Fighters[Index]
        Actors[Index].Location = FVector(Fighter.X, Fighter.Y, 0.03 + Fighter.Z)
        Actors[Index].Rotation = FRotator(0.0, math.deg(math.atan(Fighter.FaceY, Fighter.FaceX)), 0.0)
        if VisualFighters[Index] ~= Fighter or Revisions[Index] ~= Fighter.Revision then
            local Pose = Fighter.Pose
            local Clip = assert(Clips[Pose.Clip], "Missing duel animation: " .. Pose.Clip)
            local Start = Pose.Start or 0.0
            local Rate = Pose.Rate or ((Pose.Finish or Clip.Length) - Start) / Pose.Duration
            local RootMotion = Fighter.State == "Attack" or Fighter.State == "Walk" or Fighter.State == "Dash" or Fighter.State == "Guard" and Fighter.WalkPose ~= "Guard" or Fighter.State == "Down" or Fighter.State == "Rise" or Fighter.State == "KO"
            Meshes[Index]:SetRootMotionEnabled(RootMotion, false)
            local Blend = (Fighter.State == "Hit" or Fighter.State == "KO") and 0.035 or (Fighter.State == "Walk" or Fighter.State == "Idle") and 0.14 or 0.085
            Meshes[Index]:PlayAnimation(Clip.Path, Pose.Loop, Rate, Blend, Start)
            Fighter.AnimationFinished = false
            Revisions[Index], VisualFighters[Index] = Fighter.Revision, Fighter
        end
    end
end

local function SyncCamera(RealDeltaTime)
    local A, B = Match.Fighters[1], Match.Fighters[2]
    local X, Y = (A.X + B.X) * 0.5, (A.Y + B.Y) * 0.5
    local Distance = math.sqrt((B.X - A.X)^2 + (B.Y - A.Y)^2)
    local Intro = Match.Phase == "Ready" and math.max(0.0, 1.0 - Match.PhaseAge / 1.8) or 0.0
    local DesiredYaw = math.deg(math.atan(B.Y - A.Y, B.X - A.X)) - 90.0 - Intro * 7.0
    local Response = 1.0 - math.exp(-RealDeltaTime * 5.0)
    CameraYaw = CameraYaw + ((DesiredYaw - CameraYaw + 180.0) % 360.0 - 180.0) * Response
    CameraX, CameraY = CameraX + (X - CameraX) * Response, CameraY + (Y - CameraY) * Response
    CameraDistance = CameraDistance + (Clamp(4.0 + Distance * 0.60, 5.2, 12.0) + Intro * 1.1 - CameraDistance) * Response
    local Radians = math.rad(CameraYaw)
    Camera.Location = FVector(CameraX, CameraY, 0.8 + math.max(A.Z, B.Z) * 0.18)
    SetCameraRelativeTransform(Camera, FVector(-math.cos(Radians) * CameraDistance, -math.sin(Radians) * CameraDistance, 0.5 + CameraDistance * 0.10 + Intro * 0.12), FRotator(8.0, CameraYaw, 0.0))
    if PresentationPhase ~= Match.Phase then
        PresentationPhase = Match.Phase
        SetCameraPresentation(0.12, Match.Phase == "Ready" and 2.10 or Match.Phase == "RoundOver" and 2.25 or 0.0, 0.28)
    end
end

local function Effects()
    for Index, Event in ipairs(Match.Events) do
        if Event.Kind == "Hit" or Event.Kind == "Counter" or Event.Kind == "GuardBreak" then
            local Attacker = Match.Fighters[Event.Fighter]
            local Move = Attacker.Attack and Attacker.Attack.Move
            local Heavy = Event.Value >= 14 or Event.Kind ~= "Hit" or Match.Fighters[Event.Target].State == "Air" or Move and (Move.Launch or Move.Knockdown)
            local Strength = Clamp(Event.Value / 24.0, 0.35, 1.2)
            PlaySound2D("Assets/Sounds/Hit.wav", Heavy and 0.60 or 0.42, Heavy and 0.86 or 1.05)
            StartHitStop(Heavy and 0.085 or 0.050, 0.001)
            assert(StartCameraShake(Heavy and 0.95 + Strength * 0.50 or 0.50 + Strength * 0.25, Heavy and 0.27 or 0.19))
            assert(PlayFOVPunch(Heavy and -7.5 or -3.2, 0.018, 0.030, Heavy and 0.30 or 0.19))
        elseif Event.Kind == "Block" then
            PlaySound2D("Assets/Sounds/Click.wav", 0.28, 0.72)
            StartHitStop(0.028, 0.08)
            StartCameraShake(0.25, 0.13)
            PlayFOVPunch(-1.0, 0.015, 0.010, 0.12)
        elseif Event.Kind == "Finish" then
            PlaySound2D("Assets/Sounds/Clear.mp3", 0.38)
            if Match.Fighters[1].Health <= 0 or Match.Fighters[2].Health <= 0 then
                StartHitStop(0.14, 0.001)
                SlowMotionDelay = 0.14
                StartCameraShake(1.90, 0.40)
                PlayFOVPunch(-10.0, 0.035, 0.15, 0.65)
            end
        elseif Event.Kind == "Round" then
            HealthVisuals = {}
            StopAllCameraShakes(true)
            StopFOVPunch()
        end
    end
    Match.Events = {}
end

local function UpdateHud(RealDeltaTime)
    Text("Timer", string.format("%02d", math.ceil(Match.Time)))
    Text("TimerShadow", string.format("%02d", math.ceil(Match.Time)))
    Text("RoundNumber", tostring(Match.Round))
    Text("RivalRole", Match.AI and "CPU" or "P2")
    local Banner = Match.Paused and "PAUSE" or Match.Phase == "Ready" and (Match.PhaseAge < 1.25 and "ROUND " .. Match.Round or "FIGHT") or Match.Phase == "RoundOver" and Match.Message or Match.Phase == "MatchOver" and (Match.Wins[1] >= 2 and "YOU WIN" or "YOU LOSE") or ""
    Text("Banner", Banner)
    Text("BannerShadow", Banner)
    local BannerOpacity = Match.Phase == "Ready" and Clamp(Match.PhaseAge * 6.0, 0.0, 1.0) or 1.0
    Widgets.Banner.Widget:SetOpacity(BannerOpacity)
    Widgets.BannerShadow.Widget:SetOpacity(BannerOpacity)
    Widgets.PauseShade.Widget.Visible = Match.Paused
    for Index, Prefix in ipairs({ "Player", "Rival" }) do
        local Fighter = Match.Fighters[Index]
        local Visual = HealthVisuals[Index]
        if not Visual or Visual.Fighter ~= Fighter then
            Visual = { Fighter = Fighter, Health = Fighter.Health, Trail = Fighter.Health, Delay = 0.0, Flash = 0.0 }
            HealthVisuals[Index] = Visual
        end
        if Fighter.Health < Visual.Health then
            Visual.Delay, Visual.Flash = 0.32, 0.10
        elseif Fighter.Health > Visual.Health then
            Visual.Trail = Fighter.Health
        end
        Visual.Health = Fighter.Health
        Visual.Delay, Visual.Flash = math.max(0.0, Visual.Delay - RealDeltaTime), math.max(0.0, Visual.Flash - RealDeltaTime)
        if Visual.Delay <= 0.0 then
            Visual.Trail = math.max(Visual.Health, Visual.Trail - RealDeltaTime * 48.0)
        end
        local Width = 718.0 * Fighter.Health / 140.0
        Widgets[Prefix .. "Bar"].Widget:SetWidth(Width)
        Widgets[Prefix .. "Sheen"].Widget:SetWidth(Width)
        Widgets[Prefix .. "Flash"].Widget:SetWidth(Width)
        Widgets[Prefix .. "Flash"].Widget:SetOpacity(Visual.Flash / 0.10 * 0.70)
        Widgets[Prefix .. "Danger"].Widget:SetWidth(Width)
        Widgets[Prefix .. "Danger"].Widget.Visible = Fighter.Health <= 35
        Widgets[Prefix .. "Danger"].Widget:SetOpacity(0.8 + math.sin(VisualClock * 5.5) * 0.2)
        Widgets[Prefix .. "Trail"].Widget:SetWidth(718.0 * Visual.Trail / 140.0)
        Widgets[Prefix .. "Guard"].Widget:SetWidth(210.0 * Fighter.Guard / 100.0)
        for Win = 1, 2 do
            Widgets[Prefix .. "Win" .. Win].Widget.Visible = Match.Wins[Index] >= Win
        end
    end
end

function BeginPlay()
    for Index, Name in ipairs({ "Duel Player", "Duel Rival" }) do
        Actors[Index] = assert(GetActorByName(Name), Name)
        Meshes[Index] = assert(Actors[Index]:GetComponentByClass("USkeletalMeshComponent"), "Fighter mesh missing")
    end
    Camera = assert(GetActorByName("Duel Camera"), "Duel camera missing")
    SetCameraInputEnabled(Camera, false)
    assert(SetViewTargetWithBlend(Camera, 0.0), "Duel camera manager unavailable")
    Hud = assert(CreateWidget("Assets/DevilDuel/DuelHUD.widget"), "Duel HUD missing")
    for Index, Name in ipairs({ "Timer", "TimerShadow", "RoundNumber", "RivalRole", "Banner", "BannerShadow", "PauseShade" }) do
        Widgets[Name] = { Widget = assert(Hud:Find(Name), Name), Text = "" }
    end
    for Index, Prefix in ipairs({ "Player", "Rival" }) do
        for SuffixIndex, Suffix in ipairs({ "Bar", "Trail", "Sheen", "Flash", "Danger", "Guard", "Win1", "Win2" }) do
            local Name = Prefix .. Suffix
            Widgets[Name] = { Widget = assert(Hud:Find(Name), Name), Text = "" }
        end
    end
    Hud:AddToViewport(10)
    Restart()
    SyncFighters()
    SyncCamera(1.0)
    UpdateHud(0.0)
end

function Tick(DeltaTime)
    local RealDeltaTime = Clamp(GetUnscaledDeltaTime(), 0.0, 0.10)
    VisualClock = VisualClock + RealDeltaTime
    for Index = 1, 2 do
        local Delta = Meshes[Index]:ConsumeRootMotionTranslation()
        if VisualFighters[Index] == Match.Fighters[Index] then
            Rules.ApplyRootMotion(Match, Index, Delta.x, Delta.y)
            Match.Fighters[Index].AnimationFinished = Meshes[Index]:IsAnimationFinished()
        end
    end
    ReadInput()
    Rules.Update(Match, DeltaTime)
    if not Match.Paused then
        if SlowMotionDelay > 0.0 then
            SlowMotionDelay = math.max(0.0, SlowMotionDelay - RealDeltaTime)
            if SlowMotionDelay == 0.0 then
                StartHitStop(0.55, 0.24)
            end
        end
        SyncFighters()
        SyncCamera(RealDeltaTime)
        Effects()
    end
    UpdateHud(Match.Paused and 0.0 or RealDeltaTime)
end

function EndPlay()
    if Hud and Hud:IsValid() then
        Hud:Destroy()
    end
    if _G.DevilDuel == Match then
        _G.DevilDuel = nil
    end
end

function OnReload()
    EndPlay()
    Widgets = {}
    BeginPlay()
end
