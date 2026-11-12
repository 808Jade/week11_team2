local Duel = dofile("Scripts/devil_duel_rules.lua")
local Checks = 0

local function Check(Condition, Message)
    assert(Condition, Message)
    Checks = Checks + 1
end

local function Fight()
    local Match = Duel.New(9137)
    Match.Phase, Match.AI = "Fight", false
    Match.Fighters[1].X, Match.Fighters[2].X = -0.7, 0.7
    return Match
end

local function Advance(Match, Duration)
    for Frame = 1, math.floor(Duration * 120 + 0.5) do
        Duel.Update(Match, 1.0 / 120.0)
    end
end

local Match = Fight()
Duel.Attack(Match, 1, "Jab")
Advance(Match, 0.26)
Check(Match.Fighters[2].Health == 133, "An active jab hits once")
Advance(Match, 0.4)
Check(Match.Fighters[2].Health == 133, "An attack cannot damage twice")

Match = Fight()
Match.Fighters[2].State, Match.Fighters[2].Guarding = "Guard", true
Duel.Attack(Match, 1, "Jab")
Advance(Match, 0.25)
Check(Match.Fighters[2].Health == 140 and Match.Fighters[2].Guard == 90, "Frontal guard absorbs damage and consumes its meter")

Match = Fight()
Match.Fighters[2].State, Match.Fighters[2].Guarding, Match.Fighters[2].Guard = "Guard", true, 5
Duel.Attack(Match, 1, "Jab")
Advance(Match, 0.25)
Check(Match.Fighters[2].State == "GuardBreak" and Match.Fighters[2].Health < 140, "Depleted guard causes a punishable break")

Match = Fight()
Duel.Attack(Match, 1, "Jab")
Match.Fighters[2].Y = 0.9
Advance(Match, 0.4)
Check(Match.Fighters[2].Health == 140, "Sidestepping avoids a locked narrow attack")

Match = Fight()
Duel.Attack(Match, 1, "SpinKick")
Match.Fighters[2].Y = 0.9
Advance(Match, 0.62)
Check(Match.Fighters[2].Health < 140, "A wide round attack covers the sidestep lane")

Match = Fight()
Duel.Attack(Match, 1, "Jab")
Duel.Attack(Match, 2, "Jab")
Advance(Match, 0.25)
Check(Match.Fighters[1].Health == 133 and Match.Fighters[2].Health == 133, "Same-step attacks trade fairly")

Match = Fight()
Duel.Attack(Match, 1, "Jab")
Advance(Match, 0.20)
Duel.Submit(Match, 1, "J", 0.0)
Advance(Match, 0.14)
Check(Match.Fighters[1].Attack.Name == "Cross", "Buffered J chains jab into cross")
Duel.Submit(Match, 1, "K", 0.0)
Advance(Match, 0.35)
Check(Match.Fighters[1].Attack.Name == "Hook", "Buffered K chains cross into hook")
Advance(Match, 0.4)
Check(Match.Fighters[2].Combo == 3 and Match.Fighters[2].ComboDamage > 20, "A three-hit string records its hits and scaled damage")

Match = Fight()
Match.Fighters[2].X = 4.0
Duel.Attack(Match, 1, "Jab")
Duel.Submit(Match, 1, "J", 0.0)
Advance(Match, 0.7)
Check(Match.Fighters[1].State == "Idle", "Whiffed attacks cannot use on-contact cancels or retain expired input")

Match = Fight()
Duel.Attack(Match, 1, "Uppercut")
Advance(Match, 0.6)
Check(Match.Fighters[2].State == "Air" and Match.Fighters[2].Z > 0.0, "An uppercut launches its victim")
Duel.Submit(Match, 1, "J", 0.0)
Advance(Match, 0.40)
Check(Match.Fighters[2].Combo >= 2, "The launch can be followed by an airborne jab")
Advance(Match, 3.0)
Check(Match.Fighters[2].State == "Idle" and Match.Fighters[2].Z == 0.0, "Airborne victims land, get up and regain control")

Match = Fight()
Match.Fighters[2].State, Match.Fighters[2].StateDuration = "Down", 0.8
Duel.Attack(Match, 1, "Jab")
Advance(Match, 0.3)
Check(Match.Fighters[2].Health == 140, "Downed victims are protected from repeated ground hits")

Match = Fight()
Check(Duel.Dash(Match, 1), "A free fighter can backdash")
Advance(Match, 0.25)
Check(Match.Fighters[1].X < -1.8, "A backdash creates meaningful distance")
Check(not Duel.Dash(Match, 1), "Backdash has a recovery and cooldown")

Match = Fight()
Match.Paused = true
Duel.Update(Match, 1.0)
Check(Match.Time == 60.0, "Pause freezes combat time")

Match = Fight()
Match.Time, Match.Fighters[2].Health = 0.01, 100
Advance(Match, 0.05)
Check(Match.Phase == "RoundOver" and Match.Wins[1] == 1, "Timeout awards the healthier fighter one round")
Advance(Match, 3.1)
Check(Match.Round == 2 and Match.Phase == "Ready" and Match.Fighters[2].Health == 140, "A new round resets combat while preserving wins")
Match.Phase, Match.Fighters[2].Health = "Fight", 0
Advance(Match, 3.2)
Check(Match.Phase == "MatchOver" and Match.Wins[1] == 2, "Two wins end the match")

Match = Fight()
Duel.Attack(Match, 1, "Uppercut")
Advance(Match, 1.05)
Check(Match.Fighters[1].State == "Attack" and Match.Fighters[1].Pose.Finish == 1.25, "An uninterrupted uppercut retains its full recovery animation")
Advance(Match, 0.30)
Check(Match.Fighters[1].State == "Idle", "A completed uppercut returns control")

Match = Fight()
Match.RootMotion = true
Duel.Attack(Match, 1, "Uppercut")
Advance(Match, 1.6)
Check(Match.Fighters[1].State == "Attack", "Native playback must report completion before the attack can end")
Match.Fighters[1].AnimationFinished = true
Advance(Match, 0.02)
Check(Match.Fighters[1].State == "Idle", "Native completion releases the attack state")

local First, Second = Duel.New(777), Duel.New(777)
First.Phase, Second.Phase = "Fight", "Fight"
for Frame = 1, 900 do
    Duel.Update(First, 1.0 / 30.0)
    Duel.Update(Second, 1.0 / 60.0)
    Duel.Update(Second, 1.0 / 60.0)
end
Check(First.Fighters[1].Health < 140 or First.Wins[2] > 0, "CPU approaches and damages a passive player")
Check(First.Fighters[1].Health == Second.Fighters[1].Health and First.Fighters[2].Health == Second.Fighters[2].Health and First.Wins[2] == Second.Wins[2], "Fixed-step combat is independent of render frame rate")
print("PASS: " .. Checks .. " Devil Duel rule checks")
