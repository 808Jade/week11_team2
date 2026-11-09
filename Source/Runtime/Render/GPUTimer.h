#pragma once

#include "Render/RenderDevice.h"
#include <utility>
#include <vector>

// 뷰의 한 패스에서 찍은 구간들을 합산한다. 결과는 GPU가 끝낸 이전 프레임 값이다.
class FGPUTimer
{
public:
	void Begin()
	{
		BeginFrame();
		BeginRange();
	}

	void End()
	{
		EndRange();
		EndFrame();
	}

	void BeginFrame()
	{
		ID3D11DeviceContext* Context = GetContext();
		if (!Context || !EnsureQueries()) return;

		FSlot& Slot = Slots[Current];
		if (Slot.bPending && !Resolve(Context, Slot)) return;

		Slot.RangeCount = 0;
		Slot.bValid = true;
		Context->Begin(Slot.Disjoint.Get());
		bMeasuring = true;
	}

	void BeginRange()
	{
		if (!bMeasuring) return;
		FSlot& Slot = Slots[Current];
		if (!Slot.bValid) return;

		if (Slot.RangeCount == Slot.Ranges.size())
		{
			const D3D11_QUERY_DESC Desc{ D3D11_QUERY_TIMESTAMP, 0 };
			ID3D11Device* Device = FRenderDevice::Get()->GetDevice();
			FRange Range;
			if (FAILED(Device->CreateQuery(&Desc, Range.Start.GetAddressOf())) ||
				FAILED(Device->CreateQuery(&Desc, Range.Stop.GetAddressOf())))
			{
				Slot.bValid = false;
				return;
			}
			Slot.Ranges.push_back(std::move(Range));
		}
		GetContext()->End(Slot.Ranges[Slot.RangeCount].Start.Get());
	}

	void EndRange()
	{
		if (!bMeasuring) return;
		FSlot& Slot = Slots[Current];
		if (!Slot.bValid) return;

		GetContext()->End(Slot.Ranges[Slot.RangeCount].Stop.Get());
		++Slot.RangeCount;
	}

	void EndFrame()
	{
		if (!bMeasuring) return;
		bMeasuring = false;

		FSlot& Slot = Slots[Current];
		GetContext()->End(Slot.Disjoint.Get());
		Slot.bPending = true;
		Current = (Current + 1) % SlotCount;
	}

	float GetLastMs() const { return LastMs; }
	bool HasSample() const { return bHasSample; }

private:
	static constexpr uint32 SlotCount = 4;

	struct FRange
	{
		ComPtr<ID3D11Query> Start;
		ComPtr<ID3D11Query> Stop;
	};

	struct FSlot
	{
		ComPtr<ID3D11Query> Disjoint;
		std::vector<FRange> Ranges;
		size_t RangeCount = 0;
		bool bValid = false;
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

		const D3D11_QUERY_DESC Desc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
		for (FSlot& Slot : Slots)
		{
			if (FAILED(Device->CreateQuery(&Desc, Slot.Disjoint.GetAddressOf())))
			{
				bCreateFailed = true;
				return false;
			}
		}
		bCreated = true;
		return true;
	}

	// 쿼리가 준비되지 않았으면 대기하지 않고 슬롯을 다음 프레임에 다시 확인한다.
	bool Resolve(ID3D11DeviceContext* Context, FSlot& Slot)
	{
		constexpr UINT Flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT DisjointData{};
		const HRESULT Result = Context->GetData(Slot.Disjoint.Get(), &DisjointData, sizeof(DisjointData), Flags);
		if (Result == S_FALSE) return false;

		Slot.bPending = false;
		if (Result != S_OK || !Slot.bValid || DisjointData.Disjoint || DisjointData.Frequency == 0)
			return true;

		UINT64 TotalTicks = 0;
		for (size_t Index = 0; Index < Slot.RangeCount; ++Index)
		{
			const FRange& Range = Slot.Ranges[Index];
			UINT64 Start = 0;
			UINT64 Stop = 0;
			const HRESULT StartResult = Context->GetData(Range.Start.Get(), &Start, sizeof(Start), Flags);
			const HRESULT StopResult = Context->GetData(Range.Stop.Get(), &Stop, sizeof(Stop), Flags);
			if (StartResult == S_FALSE || StopResult == S_FALSE)
			{
				Slot.bPending = true;
				return false;
			}
			if (StartResult != S_OK || StopResult != S_OK || Stop < Start) return true;
			TotalTicks += Stop - Start;
		}
		LastMs = static_cast<float>(1000.0 * static_cast<double>(TotalTicks) / static_cast<double>(DisjointData.Frequency));
		bHasSample = true;
		return true;
	}

	FSlot Slots[SlotCount];
	uint32 Current = 0;
	float LastMs = 0.0f;
	bool bMeasuring = false;
	bool bHasSample = false;
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
