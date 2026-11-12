$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot

function Get-HipsTrack {
    param([byte[]]$Bytes)

    $Stream = [System.IO.MemoryStream]::new($Bytes, $false)
    $Reader = [System.IO.BinaryReader]::new($Stream)
    try {
        $Magic = [Text.Encoding]::ASCII.GetString($Reader.ReadBytes(4))
        $Version = $Reader.ReadUInt32()
        $Type = $Reader.ReadUInt32()
        if ($Magic -ne 'FBXB' -or $Version -ne 5 -or $Type -ne 6) {
            throw 'Expected a version 5 animation asset.'
        }
        for ($Index = 0; $Index -lt 2; $Index++) {
            $Length = $Reader.ReadUInt32()
            $Ignored = $Stream.Seek($Length, [IO.SeekOrigin]::Current)
        }
        $Flags = $Reader.ReadByte()
        $Length = $Reader.ReadUInt32()
        $Ignored = $Stream.Seek($Length + 16, [IO.SeekOrigin]::Current)
        $Tracks = $Reader.ReadUInt32()
        for ($Index = 0; $Index -lt $Tracks; $Index++) {
            $Length = $Reader.ReadUInt32()
            $Name = [Text.Encoding]::UTF8.GetString($Reader.ReadBytes($Length))
            $Count = $Reader.ReadUInt32()
            $Offset = [int]$Stream.Position
            if ($Name -eq 'Root hips' -and $Count -gt 0) {
                return [PSCustomObject]@{ Offset = $Offset; Count = $Count }
            }
            $Ignored = $Stream.Seek($Count * 12, [IO.SeekOrigin]::Current)
            $Count = $Reader.ReadUInt32()
            $Ignored = $Stream.Seek($Count * 16, [IO.SeekOrigin]::Current)
            $Count = $Reader.ReadUInt32()
            $Ignored = $Stream.Seek($Count * 12, [IO.SeekOrigin]::Current)
        }
        throw 'The animation has no hips translation track.'
    } finally {
        $Reader.Dispose()
    }
}

$Source = [IO.File]::ReadAllBytes((Join-Path $Root 'Assets/DevilDuel/Motion/Standing Up.fbxbin'))
$Idle = [IO.File]::ReadAllBytes((Join-Path $Root 'Assets/DevilDuel/Motion/Bouncing Fight Idle.fbxbin'))
$SourceTrack = Get-HipsTrack $Source
$IdleTrack = Get-HipsTrack $Idle
$FinalHeight = [BitConverter]::ToSingle($Source, $SourceTrack.Offset + ($SourceTrack.Count - 1) * 12 + 4)
$IdleHeight = [BitConverter]::ToSingle($Idle, $IdleTrack.Offset + 4)
$Correction = $FinalHeight - $IdleHeight

for ($Index = 0; $Index -lt $SourceTrack.Count; $Index++) {
    $Offset = $SourceTrack.Offset + $Index * 12 + 4
    $Height = [BitConverter]::ToSingle($Source, $Offset) - $Correction
    [BitConverter]::GetBytes([float]$Height).CopyTo($Source, $Offset)
}

$Destination = Join-Path $Root 'Assets/DevilDuel/AS_GetUp.fbxbin'
[IO.File]::WriteAllBytes($Destination, $Source)
Write-Output ('Created ' + $Destination)
