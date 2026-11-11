-- 입력과 이동은 Lua가 처리하고, Speed와 JumpWeight를 전달해 C++ 포즈 그래프를 구동한다.
local ActionNode <const> = "Air Action"
local JumpDuration <const> = 0.7
local Mesh = nil
local Origin = nil
local Speed = 0.0
local JumpTime = JumpDuration

function BeginPlay()
    Mesh = assert(GetComponentByName("USkeletalMeshComponent"), "X Bot needs a SkeletalMeshComponent")
    Origin = obj.Location
    assert(Mesh:SetAnimGraphFloat("Speed", 0.0), "X Bot needs its animation graph")
    print("Anim Graph Demo | WASD: move | Shift: speed up | Space: jump")
end

function Tick(DeltaTime)
    local X = (IsKeyDown("w") and 1 or 0) - (IsKeyDown("s") and 1 or 0)
    local Y = (IsKeyDown("d") and 1 or 0) - (IsKeyDown("a") and 1 or 0)
    local Magnitude = math.sqrt(X * X + Y * Y)
    local TargetSpeed = 0.0
    if Magnitude > 0.0 then
        TargetSpeed = IsKeyDown("shift") and 1.0 or 0.5
        X = X / Magnitude
        Y = Y / Magnitude
    end

    -- 목표 속도에 부드럽게 접근해 Idle, Walk와 Run 사이의 블렌딩을 확인한다.
    Speed = Speed + (TargetSpeed - Speed) * math.min(DeltaTime * 8.0, 1.0)
    Mesh:SetAnimGraphFloat("Speed", Speed)

    if IsKeyPressed("space") and JumpTime >= JumpDuration then
        JumpTime = 0.0
        Mesh:RestartAnimGraphPlayer(ActionNode)
    end

    JumpTime = math.min(JumpTime + DeltaTime, JumpDuration)
    local Phase = JumpTime / JumpDuration
    -- 동작 시작과 끝은 0, 중간은 1인 곡선으로 점프 포즈와 높이를 함께 조절한다.
    local JumpWeight = 4.0 * Phase * (1.0 - Phase)
    Mesh:SetAnimGraphFloat("JumpWeight", JumpWeight)

    local Position = obj.Location
    local Step = TargetSpeed * DeltaTime * 2.5
    local NewX = math.max(Origin.x - 2.0, math.min(Origin.x + 2.0, Position.x + X * Step))
    local NewY = math.max(Origin.y - 2.0, math.min(Origin.y + 2.0, Position.y + Y * Step))
    obj.Location = FVector(NewX, NewY, Origin.z + 0.45 * JumpWeight)
end
