-- AnimNotify 발동 테스트 (에디터 Notify 트랙이 생기기 전 임시)
-- SkeletalMeshComponent가 있는 액터의 ScriptPath에 이 파일을 넣고 PIE를 실행한 뒤 콘솔 로그를 봅니다.
-- 애니메이션: Details의 AnimToPlay로 이미 재생 중이면 그것을 씁니다.
--             재생 중인 게 없으면 아래 anim 경로를 재생합니다. (메시와 같은 Skeleton이어야 함)
local anim = nil        -- 예: "Imported/Hype_boy/AS_Hype_boy_Take 001.fbxbin"
local PLAY_RATE = -1.0   -- -1.0 이면 역재생 (AnimToPlay로 재생 중이어도 적용)
local LOOP = false       -- false 면 끝에서 멈춤 (AnimToPlay로 재생 중이어도 적용)

local elapsed = 0.0

local function log(kind, name)
    print(string.format("[AnimNotify] t=%6.2f  %-6s %s", elapsed, kind, name))
end

local function addNotifies(mesh)
    mesh:AddAnimNotify("Start", 0.0)          -- 0초: 재생 시작·루프마다 발동해야 함
    mesh:AddAnimNotify("Footstep", 1.0)       -- 1초
    mesh:AddAnimNotify("Attack", 1.5, 0.5)    -- 1.5~2.0초 NotifyState
end

function BeginPlay()
    local mesh = assert(GetComponentByClass("USkeletalMeshComponent"), "needs a SkeletalMeshComponent")

    if anim then
        mesh:PlayAnimation(anim, LOOP, PLAY_RATE)
    end
    -- AddAnimNotify는 재생 중인 애니메이션이 없으면 에러를 냅니다.
    local ok, err = pcall(addNotifies, mesh)
    if not ok then
        print("[AnimNotify] 재생 중인 애니메이션이 없습니다. Details의 AnimToPlay를 지정하거나 anim 경로를 넣으세요: " .. tostring(err))
        return
    end
    -- 재생 중인 애니메이션에 PLAY_RATE·LOOP를 적용하고 다시 시작합니다. (역재생이면 끝에서 시작)
    mesh:RestartAnimation(PLAY_RATE, LOOP)
    print(string.format("[AnimNotify] notifies added (rate=%.2f, loop=%s)", PLAY_RATE, tostring(LOOP)))
end

function Tick(dt)
    elapsed = elapsed + dt
end

function OnAnimNotify(name)      log("Notify", name) end
function OnAnimNotifyBegin(name) log("Begin", name) end
function OnAnimNotifyEnd(name)   log("End", name) end
