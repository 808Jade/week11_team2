local Duel = {}
local Clips = dofile("Scripts/devil_duel_clips.lua")

Duel.Moves = {
    Jab = { Name = "FLASH JAB", Clip = "Punching", Frames = 31, Startup = 10, Active = 5, Damage = 7, GuardDamage = 10, Reach = 1.65, Width = 0.46, Advance = 0.16, Stun = 0.34, Cancel = 0.27, Follow = { J = "Cross", K = "Hook", I = "SpinKick" } },
    Cross = { Name = "DEMON CROSS", Clip = "Jab Cross", Frames = 36, Startup = 12, Active = 5, Damage = 10, GuardDamage = 13, Reach = 1.78, Width = 0.50, Advance = 0.22, Stun = 0.39, Cancel = 0.30, Follow = { K = "Hook", U = "Knee", I = "SpinKick" } },
    Hook = { Name = "CRUSHING HOOK", Clip = "Hook Punch", Frames = 45, Startup = 17, Active = 7, Damage = 15, GuardDamage = 20, Reach = 1.72, Width = 0.90, Advance = 0.24, Stun = 0.47, Cancel = 0.44, Follow = { I = "SpinKick" } },
    Kick = { Name = "BODY KICK", Clip = "Mma Kick", Frames = 47, Startup = 20, Active = 7, Damage = 14, GuardDamage = 17, Reach = 2.05, Width = 0.56, Advance = 0.20, Stun = 0.43, Cancel = 0.43, Follow = { U = "Knee", I = "RoundKick" } },
    RoundKick = { Name = "ROUNDHOUSE", Clip = "Mma Kick (1)", Frames = 58, Startup = 24, Active = 8, Damage = 21, GuardDamage = 23, Reach = 2.12, Width = 1.05, Advance = 0.28, Stun = 0.55, Knockdown = true },
    Uppercut = { Name = "RISING DEVIL", Clip = "leftupper", Frames = 49, Startup = 19, Active = 6, Damage = 16, GuardDamage = 20, Reach = 1.70, Width = 0.48, Advance = 0.30, Stun = 0.48, Launch = true, Cancel = 0.42, Follow = { J = "Jab", U = "Knee", I = "SpinKick" } },
    Knee = { Name = "DEMON KNEE", Clip = "Illegal Knee", Frames = 42, Startup = 15, Active = 5, Damage = 14, GuardDamage = 21, Reach = 1.48, Width = 0.58, Advance = 0.24, Stun = 0.42, Cancel = 0.35, Follow = { K = "Elbow", I = "RoundKick" } },
    Elbow = { Name = "IRON ELBOW", Clip = "Illegal Elbow Punch", Frames = 46, Startup = 18, Active = 6, Damage = 18, GuardDamage = 27, Reach = 1.52, Width = 0.72, Advance = 0.20, Stun = 0.52 },
    FlyingKick = { Name = "DEVIL FLIGHT", Clip = "Flying Kick", Frames = 64, Startup = 29, Active = 7, Damage = 25, GuardDamage = 28, Reach = 2.20, Width = 0.55, Advance = 0.95, Stun = 0.58, Knockdown = true },
    SpinKick = { Name = "HELL SWEEP", Clip = "Hurricane Kick", Frames = 67, Startup = 30, Active = 10, Damage = 24, GuardDamage = 30, Reach = 2.05, Width = 1.55, Advance = 0.30, Stun = 0.60, Knockdown = true },
    DropKick = { Name = "DEVIL BREAKER", Clip = "Goalkeeper Drop Kick", Frames = 73, Startup = 32, Active = 8, Damage = 31, GuardDamage = 39, Reach = 2.30, Width = 0.65, Advance = 0.65, Stun = 0.65, Knockdown = true }
}

Duel.MotionWindows = {
    Jab = { Start = 0.08, Rate = 1.20, Startup = 14, Active = 6, Cancel = 0.32 },
    Cross = { Start = 0.40, Rate = 1.25, Startup = 13, Active = 7, Cancel = 0.34 },
    Hook = { Start = 0.65, Rate = 1.30, Startup = 22, Active = 8, Cancel = 0.50 },
    Kick = { Start = 0.20, Rate = 1.15, Startup = 25, Active = 8, Cancel = 0.54 },
    RoundKick = { Start = 0.30, Rate = 1.10, Startup = 36, Active = 10 },
    Uppercut = { Start = 0.0, Rate = 1.0, Startup = 32, Active = 8, Cancel = 0.68 },
    Knee = { Start = 0.45, Rate = 1.20, Startup = 20, Active = 7, Cancel = 0.48 },
    Elbow = { Start = 0.04, Rate = 1.25, Startup = 18, Active = 8 },
    FlyingKick = { Start = 0.0, Rate = 1.0, Startup = 32, Active = 8 },
    SpinKick = { Start = 0.0, Rate = 1.0, Startup = 26, Active = 40 },
    DropKick = { Start = 0.0, Rate = 1.0, Startup = 27, Active = 9 }
}

for Name, Motion in pairs(Duel.MotionWindows) do
    local Move = Duel.Moves[Name]
    Motion.Finish = Clips[Move.Clip].Length
    Move.Frames = math.ceil((Motion.Finish - Motion.Start) / Motion.Rate * 60.0) + 1
    Move.Startup, Move.Active, Move.Cancel = Motion.Startup, Motion.Active, Motion.Cancel
end
Duel.Moves.Jab.Stun, Duel.Moves.Cross.Stun = 0.42, 0.55

local function Clamp(Value, Minimum, Maximum)
    return math.max(Minimum, math.min(Maximum, Value))
end

local function Emit(Match, Kind, Fighter, Target, Value)
    Match.Events[#Match.Events + 1] = { Kind = Kind, Fighter = Fighter, Target = Target, Value = Value or 0 }
end

local function Random(Match)
    Match.Seed = (Match.Seed * 48271) % 2147483647
    return Match.Seed / 2147483647
end

local function Face(Fighter, Opponent)
    local X = Opponent.X - Fighter.X
    local Y = Opponent.Y - Fighter.Y
    local Length = math.sqrt(X * X + Y * Y)
    if Length > 0.001 then
        Fighter.FaceX, Fighter.FaceY = X / Length, Y / Length
    end
    return Length
end

local function SetPose(Fighter, Clip, Duration, Loop, Start, Rate)
    Fighter.Pose = { Clip = Clip, Duration = Duration or 1.0, Loop = Loop or false, Start = Start or 0.0, Rate = Rate }
    Fighter.Revision = Fighter.Revision + 1
end

local function SetState(Fighter, State, Duration)
    Fighter.State, Fighter.Age, Fighter.StateDuration = State, 0.0, Duration or 0.0
    Fighter.Attack = nil
    Fighter.Buffer = nil
    Fighter.Guarding = false
    if State == "Idle" then
        SetPose(Fighter, "Bouncing Fight Idle", 2.9667, true, 0.0, 1.0)
    elseif State == "Hit" or State == "GuardBreak" then
        SetPose(Fighter, State == "GuardBreak" and "Big Hit To Head" or Fighter.ReactionClip or "Stomach Hit", math.max(Duration, 0.28), false, 0.03)
        Fighter.Pose.Finish = State == "GuardBreak" and 1.2 or Fighter.Pose.Clip == "Big Hit To Head" and 0.90 or 0.70
    elseif State == "Air" then
        SetPose(Fighter, "Big Hit To Head", 0.8)
    elseif State == "Down" or State == "KO" then
        SetPose(Fighter, "Falling Down", 0.78)
        Fighter.Pose.Finish = 1.25
    elseif State == "Rise" then
        SetPose(Fighter, "Standing Up", Duration, false, 0.65)
        Fighter.Pose.Finish = 4.3
    elseif State == "Block" then
        SetPose(Fighter, "Rib Hit", math.max(Duration, 0.2), false, 0.05)
        Fighter.Pose.Finish = 0.34
    elseif State == "Dash" then
        SetPose(Fighter, "Long Step Forward", Duration, false, 1.0, -1.45)
    end
end

local function NewFighter(Index)
    local Fighter = { Index = Index, X = Index == 1 and -1.65 or 1.65, Y = 0.0, Z = 0.0, Health = 140, Guard = 100, FaceX = Index == 1 and 1.0 or -1.0, FaceY = 0.0, Age = 0.0, StateDuration = 0.0, State = "Idle", Attack = nil, Buffer = nil, Guarding = false, Revision = 0, Pose = nil, PushX = 0.0, PushY = 0.0, VerticalSpeed = 0.0, Combo = 0, ComboDamage = 0, ComboAge = 9.0, GuardAge = 9.0, Think = 0.0, Plan = nil, DashCooldown = 0.0, LastMove = "", LastResult = "", WalkPose = "" }
    SetState(Fighter, "Idle")
    Fighter.AnimationFinished = false
    SetPose(Fighter, "Action Idle To Fight Idle", 1.0, false, 0.0, 1.0)
    return Fighter
end

function Duel.New(Seed, RootMotion)
    return { Fighters = { NewFighter(1), NewFighter(2) }, Wins = { 0, 0 }, Round = 1, Time = 60.0, Phase = "Ready", PhaseAge = 0.0, Winner = 0, Events = {}, Seed = Seed or 9137, AI = true, Paused = false, Accumulator = 0.0, Input = { Forward = 0.0, Side = 0.0, Guard = false }, Message = "GET READY", RootMotion = RootMotion or false, RootMotionDistance = 0.0 }
end

function Duel.ResetRound(Match)
    Match.Fighters = { NewFighter(1), NewFighter(2) }
    Match.Phase, Match.PhaseAge, Match.Time, Match.Winner = "Ready", 0.0, 60.0, 0
    Match.Accumulator = 0.0
    Match.Message = "GET READY"
    Emit(Match, "Round", 0, 0)
end

local function CanAct(Fighter)
    return Fighter.State == "Idle" or Fighter.State == "Walk" or Fighter.State == "Guard"
end

function Duel.SetAI(Match, Enabled)
    Match.AI = Enabled
    local Fighter = Match.Fighters[2]
    Fighter.Plan, Fighter.Guarding, Fighter.Think = nil, false, 0.0
    if CanAct(Fighter) or not Enabled and Fighter.State == "Attack" then
        SetState(Fighter, "Idle")
    end
end

function Duel.Attack(Match, Index, Name)
    local Fighter = Match.Fighters[Index]
    local Move = Duel.Moves[Name]
    if Match.Phase ~= "Fight" or not Move or not CanAct(Fighter) and Fighter.State ~= "Attack" then
        return false
    end
    Face(Fighter, Match.Fighters[3 - Index])
    Fighter.State, Fighter.Age, Fighter.Guarding = "Attack", 0.0, false
    Fighter.StateDuration = Move.Frames / 60.0
    Fighter.Attack = { Name = Name, Move = Move, Hit = false, Connected = false, DirX = Fighter.FaceX, DirY = Fighter.FaceY }
    Fighter.Buffer = nil
    Fighter.LastMove, Fighter.LastResult = Move.Name, ""
    local Motion = Duel.MotionWindows[Name]
    SetPose(Fighter, Move.Clip, Fighter.StateDuration, false, Motion.Start, Motion.Rate)
    Fighter.Pose.Finish = Motion.Finish
    Fighter.AnimationFinished = false
    Emit(Match, "Attack", Index, 3 - Index)
    return true
end

function Duel.Submit(Match, Index, Button, Forward)
    if Match.Phase ~= "Fight" then
        return
    end
    local Fighter = Match.Fighters[Index]
    Fighter.Buffer = { Button = Button, Forward = Forward or 0.0, Life = Fighter.State == "Attack" and 0.45 or 0.18 }
end

function Duel.Dash(Match, Index)
    local Fighter = Match.Fighters[Index]
    if Match.Phase == "Fight" and CanAct(Fighter) and Fighter.DashCooldown <= 0.0 then
        SetState(Fighter, "Dash", 0.70)
        Fighter.DashCooldown = 0.95
        return true
    end
    return false
end

local function ResolveBuffer(Match, Index)
    local Fighter = Match.Fighters[Index]
    local Buffer = Fighter.Buffer
    if not Buffer then
        return
    end
    if Fighter.State == "Attack" then
        local Attack = Fighter.Attack
        local Follow = Attack.Move.Follow
        if Attack.Connected and Follow and Follow[Buffer.Button] and Fighter.Age >= Attack.Move.Cancel then
            Duel.Attack(Match, Index, Follow[Buffer.Button])
        end
    elseif CanAct(Fighter) then
        local Name = Buffer.Button == "J" and "Jab" or Buffer.Button == "K" and (Buffer.Forward > 0 and "Uppercut" or Buffer.Forward < 0 and "Elbow" or "Hook") or Buffer.Button == "U" and (Buffer.Forward > 0 and "Knee" or "Kick") or (Buffer.Forward > 0 and "FlyingKick" or Buffer.Forward < 0 and "DropKick" or "RoundKick")
        Duel.Attack(Match, Index, Name)
    end
end

local function MoveFighter(Fighter, Forward, Side, Guard, DeltaTime)
    if not CanAct(Fighter) then
        return
    end
    Fighter.Guarding = Guard
    local Moving = math.abs(Forward) + math.abs(Side) > 0.01
    local State = Guard and "Guard" or Moving and "Walk" or "Idle"
    local Pose = Moving and (math.abs(Side) > 0.1 and (Side > 0 and "SideRight" or "SideLeft") or Forward < 0 and "Back" or "Forward") or State
    if Fighter.State ~= State or Fighter.WalkPose ~= Pose then
        Fighter.State, Fighter.Age, Fighter.WalkPose = State, 0.0, Pose
        if Pose == "Guard" then
            SetPose(Fighter, "Bouncing Fight Idle", 2.9667, true, 0.0, 0.70)
        elseif Pose == "SideLeft" or Pose == "SideRight" then
            SetPose(Fighter, "Short Left Side Step", 0.7833, true, Pose == "SideRight" and 0.7833 or 0.0, Pose == "SideRight" and -1.05 or 1.05)
        elseif Pose == "Back" then
            SetPose(Fighter, "Long Step Forward", 1.0, true, 1.0, Guard and -0.70 or -0.95)
        elseif Pose == "Forward" then
            SetPose(Fighter, "Long Step Forward", 1.0, true, 0.0, Guard and 0.70 or 1.10)
        else
            SetPose(Fighter, "Bouncing Fight Idle", 2.9667, true, 0.0, 1.0)
        end
    end
    local Length = math.sqrt(Forward * Forward + Side * Side)
    local Speed = Guard and 0.75 or 2.45
    if Length > 1.0 then
        Forward, Side = Forward / Length, Side / Length
    end
    if not Fighter.RootMotion then
        Fighter.X = Fighter.X + (Fighter.FaceX * Forward - Fighter.FaceY * Side) * Speed * DeltaTime
        Fighter.Y = Fighter.Y + (Fighter.FaceY * Forward + Fighter.FaceX * Side) * Speed * DeltaTime
    end
end

local function ThinkAI(Match, DeltaTime)
    local Fighter, Opponent = Match.Fighters[2], Match.Fighters[1]
    Fighter.Think = Fighter.Think - DeltaTime
    local Distance = math.sqrt((Fighter.X - Opponent.X)^2 + (Fighter.Y - Opponent.Y)^2)
    if Fighter.Think <= 0.0 then
        Fighter.Think = 0.32 + Random(Match) * 0.30
        local Roll = Random(Match)
        Fighter.Plan = { Forward = Distance > 1.9 and 1.0 or Distance < 1.0 and -0.6 or 0.0, Side = 0.0, Guard = false }
        if Opponent.State == "Attack" and Distance < 2.8 and Opponent.Age > 0.08 then
            if Roll < 0.58 then
                Fighter.Plan.Guard = true
                Fighter.Plan.Forward = -0.35
            elseif Roll < 0.88 then
                Fighter.Plan.Side = Random(Match) < 0.5 and -1.0 or 1.0
            end
        elseif CanAct(Fighter) and Distance < 2.15 then
            if Roll < 0.66 then
                local Choices = { "Jab", "Kick", "Hook", "Uppercut", "Knee", "RoundKick", "Elbow" }
                local Choice = Choices[math.floor(Random(Match) * #Choices) + 1]
                Duel.Attack(Match, 2, Choice)
            elseif Roll < 0.82 then
                Fighter.Plan.Guard = true
            else
                Fighter.Plan.Side = Random(Match) < 0.5 and -1.0 or 1.0
            end
        elseif Distance > 3.4 and Roll < 0.20 then
            Duel.Attack(Match, 2, "FlyingKick")
        end
        if Fighter.State == "Attack" and Fighter.Attack.Connected and Fighter.Attack.Move.Follow and Roll < 0.80 then
            local Buttons = { "J", "K", "U", "I" }
            for Offset = 0, 3 do
                local Button = Buttons[(math.floor(Roll * 10) + Offset) % 4 + 1]
                if Fighter.Attack.Move.Follow[Button] then
                    Duel.Submit(Match, 2, Button, 0.0)
                    break
                end
            end
        end
    end
    local Plan = Fighter.Plan or { Forward = 0.0, Side = 0.0, Guard = false }
    MoveFighter(Fighter, Plan.Forward, Plan.Side, Plan.Guard, DeltaTime)
end

local function AdvanceFighter(Match, Fighter, DeltaTime)
    Fighter.Age = Fighter.Age + DeltaTime
    Fighter.ComboAge = Fighter.ComboAge + DeltaTime
    Fighter.GuardAge = Fighter.GuardAge + DeltaTime
    Fighter.DashCooldown = math.max(0.0, Fighter.DashCooldown - DeltaTime)
    if Fighter.Buffer then
        Fighter.Buffer.Life = Fighter.Buffer.Life - DeltaTime
        if Fighter.Buffer.Life <= 0.0 then
            Fighter.Buffer = nil
        end
    end
    if Fighter.GuardAge > 1.0 and Fighter.State ~= "GuardBreak" then
        Fighter.Guard = math.min(100.0, Fighter.Guard + 13.0 * DeltaTime)
    end
    if Fighter.ComboAge > 1.3 and Fighter.State ~= "Air" then
        Fighter.Combo, Fighter.ComboDamage = 0, 0
    end
    local Drag = math.exp(-10.0 * DeltaTime)
    Fighter.X, Fighter.Y = Fighter.X + Fighter.PushX * DeltaTime, Fighter.Y + Fighter.PushY * DeltaTime
    Fighter.PushX, Fighter.PushY = Fighter.PushX * Drag, Fighter.PushY * Drag
    if Fighter.State == "Attack" then
        local Attack = Fighter.Attack
        if not Match.RootMotion and Fighter.Age <= Attack.Move.Startup / 60.0 then
            local Speed = Attack.Move.Advance / (Attack.Move.Startup / 60.0)
            Fighter.X, Fighter.Y = Fighter.X + Attack.DirX * Speed * DeltaTime, Fighter.Y + Attack.DirY * Speed * DeltaTime
        end
        if Fighter.Age >= Fighter.StateDuration and (not Match.RootMotion or Fighter.AnimationFinished) then
            Fighter.LastResult = Attack.Connected and Fighter.LastResult or "WHIFF"
            SetState(Fighter, "Idle")
        end
    elseif Fighter.State == "Dash" then
        if not Match.RootMotion then
            Fighter.X, Fighter.Y = Fighter.X - Fighter.FaceX * 6.0 * DeltaTime, Fighter.Y - Fighter.FaceY * 6.0 * DeltaTime
        end
        if Fighter.Age >= Fighter.StateDuration then
            SetState(Fighter, "Idle")
        end
    elseif Fighter.State == "Air" then
        Fighter.VerticalSpeed = Fighter.VerticalSpeed - 13.5 * DeltaTime
        Fighter.Z = Fighter.Z + Fighter.VerticalSpeed * DeltaTime
        if Fighter.Z <= 0.0 and Fighter.VerticalSpeed < 0.0 then
            Fighter.Z, Fighter.VerticalSpeed = 0.0, 0.0
            SetState(Fighter, "Down", 0.90)
        end
    elseif Fighter.State == "Down" and Fighter.Age >= Fighter.StateDuration then
        SetState(Fighter, "Rise", 1.15)
    elseif (Fighter.State == "Hit" or Fighter.State == "Block" or Fighter.State == "GuardBreak" or Fighter.State == "Rise") and Fighter.Age >= Fighter.StateDuration then
        SetState(Fighter, "Idle")
    end
end

local function FindContact(Fighter, Target)
    local Attack = Fighter.Attack
    if Fighter.State ~= "Attack" or not Attack or Attack.Hit or Target.State == "Down" or Target.State == "Rise" or Target.State == "KO" then
        return nil
    end
    local Move = Attack.Move
    if Fighter.Age < Move.Startup / 60.0 or Fighter.Age > (Move.Startup + Move.Active) / 60.0 or Target.Z > 1.8 then
        return nil
    end
    local X, Y = Target.X - Fighter.X, Target.Y - Fighter.Y
    local Forward = X * Attack.DirX + Y * Attack.DirY
    local Side = math.abs(-X * Attack.DirY + Y * Attack.DirX)
    if Forward < 0.1 or Forward > Move.Reach or Side > Move.Width then
        return nil
    end
    return { Attack = Attack, Attacker = Fighter.Index, Target = Target.Index, Counter = Target.State == "Attack" and Target.Age < Target.Attack.Move.Startup / 60.0, Block = (Target.Guarding or Target.State == "Block") and Target.FaceX * -Attack.DirX + Target.FaceY * -Attack.DirY > 0.25 }
end

local function ApplyContact(Match, Contact)
    local Fighter, Target = Match.Fighters[Contact.Attacker], Match.Fighters[Contact.Target]
    local Attack, Move = Contact.Attack, Contact.Attack.Move
    Target.ReactionClip = Contact.Counter and "Big Hit To Head" or (Attack.Name == "Kick" or Attack.Name == "Knee" or Attack.Name == "Elbow") and "Stomach Hit" or Target.Combo % 2 == 0 and "Rib Hit" or "Big Hit To Head"
    Attack.Hit, Attack.Connected = true, true
    Target.GuardAge = 0.0
    if Contact.Block then
        Target.Guard = math.max(0.0, Target.Guard - Move.GuardDamage)
        if Target.Guard > 0.0 then
            SetState(Target, "Block", 0.18 + Move.GuardDamage * 0.003)
            Target.PushX, Target.PushY = Attack.DirX * 1.5, Attack.DirY * 1.5
            Fighter.LastResult = "GUARDED"
            Emit(Match, "Block", Fighter.Index, Target.Index)
            return
        end
        SetState(Target, "GuardBreak", 1.05)
        Fighter.LastResult = "GUARD BREAK"
        Emit(Match, "GuardBreak", Fighter.Index, Target.Index)
    end
    local WasAir = Target.State == "Air"
    local Continued = Target.ComboAge < 1.0 and (Target.State == "Hit" or WasAir)
    Target.Combo = Continued and Target.Combo + 1 or 1
    Target.ComboDamage = Continued and Target.ComboDamage or 0
    local Damage = math.max(3, math.floor(Move.Damage * (Contact.Counter and 1.25 or 1.0) * math.max(0.40, 1.0 - (Target.Combo - 1) * 0.13)))
    Target.Health, Target.ComboAge, Target.ComboDamage = math.max(0, Target.Health - Damage), 0.0, Target.ComboDamage + Damage
    Target.PushX, Target.PushY = Attack.DirX * (WasAir and 2.0 or 1.8), Attack.DirY * (WasAir and 2.0 or 1.8)
    if Target.Health <= 0 then
        Target.Z = 0.0
        SetState(Target, "KO")
    elseif Contact.Block then
        Target.LastResult = "GUARD BROKEN"
    elseif (Move.Launch or WasAir) and Target.Combo <= 5 then
        local Height = Target.Z
        SetState(Target, "Air", 1.0)
        Target.Z = math.max(0.03, Height)
        Target.VerticalSpeed = WasAir and 3.2 or 5.6
    elseif Move.Knockdown or Target.Combo > 5 then
        Target.Z = 0.0
        SetState(Target, "Down", 0.95)
    else
        SetState(Target, "Hit", Move.Stun + (Contact.Counter and 0.12 or 0.0))
    end
    Fighter.LastResult = Contact.Counter and "COUNTER HIT" or "HIT"
    Emit(Match, Contact.Counter and "Counter" or "Hit", Fighter.Index, Target.Index, Damage)
end

local function Constrain(Match)
    local A, B = Match.Fighters[1], Match.Fighters[2]
    local X, Y = B.X - A.X, B.Y - A.Y
    local Length = math.sqrt(X * X + Y * Y)
    if Length < 0.92 and A.Z < 0.5 and B.Z < 0.5 then
        local NX, NY = Length > 0.001 and X / Length or 1.0, Length > 0.001 and Y / Length or 0.0
        local Push = (0.92 - Length) * 0.5
        A.X, A.Y, B.X, B.Y = A.X - NX * Push, A.Y - NY * Push, B.X + NX * Push, B.Y + NY * Push
    end
    for Index = 1, 2 do
        local Fighter = Match.Fighters[Index]
        Fighter.X, Fighter.Y = Clamp(Fighter.X, -6.5, 6.5), Clamp(Fighter.Y, -4.5, 4.5)
        if CanAct(Fighter) or Fighter.State == "Block" then
            Face(Fighter, Match.Fighters[3 - Index])
        end
    end
end

function Duel.ApplyRootMotion(Match, Index, X, Y)
    local Fighter = Match.Fighters[Index]
    local Moving = Fighter.State == "Walk" or Fighter.State == "Attack" or Fighter.State == "Dash" or Fighter.State == "Guard" and Fighter.WalkPose ~= "Guard" or Fighter.State == "Down" or Fighter.State == "Rise" or Fighter.State == "KO"
    local Active = Match.Phase == "Fight" or Match.Phase == "RoundOver" and Fighter.State == "KO"
    if not Match.RootMotion or Match.Paused or not Active or not Moving then
        return
    end
    local Length = math.sqrt(X * X + Y * Y)
    if Length > 0.35 then
        X, Y = X * 0.35 / Length, Y * 0.35 / Length
    end
    Fighter.X, Fighter.Y = Fighter.X + X, Fighter.Y + Y
    Match.RootMotionDistance = Match.RootMotionDistance + math.sqrt(X * X + Y * Y)
    Constrain(Match)
end

local function FinishRound(Match)
    local A, B = Match.Fighters[1], Match.Fighters[2]
    local Winner = A.Health == B.Health and 0 or A.Health > B.Health and 1 or 2
    Match.Winner, Match.Phase, Match.PhaseAge = Winner, "RoundOver", 0.0
    if Winner > 0 then
        Match.Wins[Winner] = Match.Wins[Winner] + 1
    end
    if A.Health <= 0 or B.Health <= 0 then
        Match.Message = "K.O."
    else
        Match.Message = Winner == 0 and "DRAW" or "TIME UP"
    end
    Emit(Match, "Finish", Winner, 0)
end

local function Step(Match, DeltaTime)
    Match.PhaseAge = Match.PhaseAge + DeltaTime
    if Match.Phase == "Ready" then
        Match.Message = Match.PhaseAge < 1.25 and "ROUND " .. Match.Round or "FIGHT"
        if Match.PhaseAge >= 1.0 then
            for Index = 1, 2 do
                if Match.Fighters[Index].Pose.Clip == "Action Idle To Fight Idle" then
                    SetState(Match.Fighters[Index], "Idle")
                end
            end
        end
        if Match.PhaseAge >= 2.0 then
            Match.Phase, Match.PhaseAge, Match.Message = "Fight", 0.0, ""
        end
        return
    elseif Match.Phase == "RoundOver" then
        for Index = 1, 2 do
            Match.Fighters[Index].Age = Match.Fighters[Index].Age + DeltaTime
        end
        if Match.PhaseAge >= 3.0 then
            if Match.Wins[1] >= 2 or Match.Wins[2] >= 2 then
                Match.Phase = "MatchOver"
                Match.Message = Match.Wins[1] >= 2 and "YOU WIN" or "YOU LOSE"
            else
                Match.Round = Match.Round + 1
                Duel.ResetRound(Match)
            end
        end
        return
    elseif Match.Phase ~= "Fight" then
        return
    end
    Match.Time = math.max(0.0, Match.Time - DeltaTime)
    for Index = 1, 2 do
        Match.Fighters[Index].RootMotion = Match.RootMotion
        AdvanceFighter(Match, Match.Fighters[Index], DeltaTime)
    end
    MoveFighter(Match.Fighters[1], Match.Input.Forward, Match.Input.Side, Match.Input.Guard, DeltaTime)
    if Match.AI then
        ThinkAI(Match, DeltaTime)
    end
    for Index = 1, 2 do
        ResolveBuffer(Match, Index)
    end
    Constrain(Match)
    local First, Second = FindContact(Match.Fighters[1], Match.Fighters[2]), FindContact(Match.Fighters[2], Match.Fighters[1])
    if First then
        ApplyContact(Match, First)
    end
    if Second then
        ApplyContact(Match, Second)
    end
    if Match.Fighters[1].Health <= 0 or Match.Fighters[2].Health <= 0 or Match.Time <= 0.0 then
        FinishRound(Match)
    end
end

function Duel.Update(Match, DeltaTime)
    if Match.Paused then
        return
    end
    Match.Accumulator = Match.Accumulator + Clamp(DeltaTime, 0.0, 0.15)
    while Match.Accumulator >= 1.0 / 120.0 do
        Match.Accumulator = Match.Accumulator - 1.0 / 120.0
        Step(Match, 1.0 / 120.0)
    end
end

return Duel
