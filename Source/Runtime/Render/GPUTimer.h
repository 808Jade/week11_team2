#pragma once

#include "Render/RenderDevice.h"

// D3D11 타임스탬프 쿼리로 GPU가 한 구간을 실행하는 데 걸린 시간을 잰다.
// GPU는 CPU가 명령을 넣은 뒤에야 그 구간을 실행하므로 결과가 바로 나오지 않는다.
// 슬롯을 여러 개 돌려 쓰고, 슬롯을 다시 쓸 차례가 왔을 때 기다리지 않고 지난 결과를 읽는다.
// 그래서 GetLastMs()는 몇 프레임 전 값이다. 프레임마다 튀는 값이 아니라 평균을 보는 용도.
class FGPUTimer
{
public:
	void Begin()
	{
		ID3D11DeviceContext* Context = GetContext();
		if (!Context || !EnsureQueries()) return;

		FSlot& Slot = Slots[Current];
		if (Slot.bPending) Resolve(Context, Slot);

		Context->Begin(Slot.Disjoint.Get());
		Context->End(Slot.Start.Get());
		bMeasuring = true;
	}

	void End()
	{
		if (!bMeasuring) return;
		bMeasuring = false;

		ID3D11DeviceContext* Context = GetContext();
		if (!Context) return;

		FSlot& Slot = Slots[Current];
		Context->End(Slot.Stop.Get());
		Context->End(Slot.Disjoint.Get());
		Slot.bPending = true;
		Current = (Current + 1) % SlotCount;
	}

	float GetLastMs() const { return LastMs; }

private:
	static constexpr uint32 SlotCount = 4;

	struct FSlot
	{
		ComPtr<ID3D11Query> Disjoint;
		ComPtr<ID3D11Query> Start;
		ComPtr<ID3D11Query> Stop;
		bool bPending = false;
	};

	static ID3D11DeviceContext* GetContext()
	{
		FRenderDevice* RenderDevice = FRenderDevice::Get();
		return RenderDevice ? RenderDevice->GetContext() : nullptr;
	}

	bool EnsureQueries()
	{
		if (bCreated) return true;
		if (bCreateFailed) return false;

		FRenderDevice* RenderDevice = FRenderDevice::Get();
		ID3D11Device* Device = RenderDevice ? RenderDevice->GetDevice() : nullptr;
		if (!Device)
		{
			bCreateFailed = true;
			return false;
		}

		const D3D11_QUERY_DESC DisjointDesc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
		const D3D11_QUERY_DESC TimestampDesc{ D3D11_QUERY_TIMESTAMP, 0 };
		bool bSucceeded = true;
		for (FSlot& Slot : Slots)
		{
			bSucceeded = bSucceeded
				&& SUCCEEDED(Device->CreateQuery(&DisjointDesc, Slot.Disjoint.GetAddressOf()))
				&& SUCCEEDED(Device->CreateQuery(&TimestampDesc, Slot.Start.GetAddressOf()))
				&& SUCCEEDED(Device->CreateQuery(&TimestampDesc, Slot.Stop.GetAddressOf()));
		}

		bCreated = bSucceeded;
		bCreateFailed = !bSucceeded;
		return bCreated;
	}

	// 아직 GPU가 끝내지 않았으면 기다리지 않고 그냥 버린다. 슬롯이 4개라 보통은 이미 끝나 있다.
	void Resolve(ID3D11DeviceContext* Context, FSlot& Slot)
	{
		Slot.bPending = false;

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT DisjointData{};
		UINT64 StartTicks = 0;
		UINT64 StopTicks = 0;
		if (Context->GetData(Slot.Disjoint.Get(), &DisjointData, sizeof(DisjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
		if (Context->GetData(Slot.Start.Get(), &StartTicks, sizeof(StartTicks), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
		if (Context->GetData(Slot.Stop.Get(), &StopTicks, sizeof(StopTicks), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;

		// Disjoint면 그 사이 GPU 클럭이 바뀌어 타임스탬프를 믿을 수 없다.
		if (DisjointData.Disjoint || DisjointData.Frequency == 0 || StopTicks < StartTicks) return;

		LastMs = static_cast<float>(static_cast<double>(StopTicks - StartTicks) * 1000.0 / static_cast<double>(DisjointData.Frequency));
	}

	FSlot Slots[SlotCount];
	uint32 Current = 0;
	float LastMs = 0.0f;
	bool bMeasuring = false;
	bool bCreated = false;
	bool bCreateFailed = false;
};

// 스코프를 벗어날 때 End를 부른다. 중간에 return이 끼어도 Begin/End 짝이 맞는다.
struct FScopedGPUTimer
{
	explicit FScopedGPUTimer(FGPUTimer& InTimer) : Timer(InTimer) { Timer.Begin(); }
	~FScopedGPUTimer() { Timer.End(); }

	FScopedGPUTimer(const FScopedGPUTimer&) = delete;
	FScopedGPUTimer& operator=(const FScopedGPUTimer&) = delete;

	FGPUTimer& Timer;
};
