<#
.SYNOPSIS
    Set the Phoenix radio's clock over USB serial.

.DESCRIPTION
    Sends a PJRC-standard time packet - 'T' + 10-digit timestamp + newline - to
    the radio, which sets both the coin-cell-backed hardware RTC and the software
    clock that drives the front-panel display.

    WHAT THE RADIO DOES WITH THE NUMBER

    The firmware feeds the timestamp straight to TimeLib and the display reads
    hour()/minute()/second() back out of it. Nothing applies a time-zone offset.
    The MY_TIMEZONE setting in Config.h is only a label - "EST: " and friends are
    pasted in front of the digits as a string, and changing it does not shift the
    clock.

    So the radio shows the UTC decomposition of whatever number it is given. To
    make the display read local wall-clock time, the timestamp has to be shifted
    by the local UTC offset before it is sent. That is what this script does by
    default. Use -Utc to send a true UTC timestamp instead, which is what you
    want if MY_TIMEZONE is set to "UTC: ".

    Either way this clock is cosmetic: it drives the display and nothing else.
    FT8 timing comes from the PC's clock, not the radio's, so WSJT-X is
    unaffected by what you set here.

    This is the Windows counterpart of set_radio_time.py, for people who would
    rather not install Python. It needs no modules beyond what ships with
    Windows PowerShell 5.1.

.PARAMETER Port
    Serial port to use, e.g. COM5. Autodetected if omitted.

.PARAMETER Utc
    Send a true UTC timestamp. Use if MY_TIMEZONE is set to "UTC: ".

.PARAMETER Offset
    Override the local UTC offset, e.g. +05:30 or -08:00.

.PARAMETER List
    List candidate ports and exit.

.EXAMPLE
    .\Set-RadioTime.ps1
    Sets the clock so the display reads local wall-clock time.

.EXAMPLE
    .\Set-RadioTime.ps1 -Utc
    Sets the clock so the display reads UTC.

.EXAMPLE
    .\Set-RadioTime.ps1 -Port COM5
    Uses COM5 rather than autodetecting.

.NOTES
    If PowerShell refuses to run this because of the execution policy, either
    unblock the file:
        Unblock-File .\Set-RadioTime.ps1
    or run it for this session only:
        powershell -ExecutionPolicy Bypass -File .\Set-RadioTime.ps1
#>

[CmdletBinding()]
param(
    [string]$Port,
    [switch]$Utc,
    [string]$Offset,
    [switch]$List
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$TeensyVid     = 'VID_16C0'   # PJRC
$PacketDigits  = 10           # the firmware accepts exactly 10, nothing else

function Stop-WithError {
    param([string]$Message, [string]$Hint)
    Write-Host "error: $Message" -ForegroundColor Red
    if ($Hint) { Write-Host "       $Hint" -ForegroundColor DarkGray }
    exit 1
}

function Get-PortSortKey {
    # Natural sort, so COM9 comes before COM10.
    param([string]$Name)
    if ($Name -match '(\d+)') { return [int]$Matches[1] }
    return [int]::MaxValue
}

function Get-TeensyPort {
    <#
        With Dual Serial the radio presents two ports: the lower-numbered one is
        Serial, which is the port the firmware's own time-sync reader watches.
        With the serial+midi+audio USB type there is only one, shared with CAT -
        the CAT reader recognises the time packet there, so either way the first
        port works.
    #>
    $found = @()
    try {
        # Match inside the ForEach-Object rather than relying on $Matches set by a
        # Where-Object in a different scope, which is not reliably visible here.
        $found = @(Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction Stop |
            Where-Object { $_.PNPDeviceID -like "*$TeensyVid*" } |
            ForEach-Object {
                if ($_.Name -match '\(COM(\d+)\)') {
                    [pscustomobject]@{
                        Device      = "COM$($Matches[1])"
                        Description = $_.Name
                    }
                }
            })
    } catch {
        # Not Windows, or WMI unavailable - fall back to bare port names below.
    }
    return @($found | Sort-Object { Get-PortSortKey $_.Device })
}

function Get-AnyPort {
    $names = @()
    try { $names = @([System.IO.Ports.SerialPort]::GetPortNames()) } catch { }
    return @($names | Sort-Object { Get-PortSortKey $_ } |
        ForEach-Object { [pscustomobject]@{ Device = $_; Description = 'serial port' } })
}

function Format-Offset {
    # Render an offset as UTC+HH:MM. Custom TimeSpan format strings drop the sign
    # entirely, so a negative offset would otherwise read as "+04:00".
    param([int]$Seconds)
    if ($Seconds -eq 0) { return 'UTC' }
    $sign      = if ($Seconds -gt 0) { '+' } else { '-' }
    $magnitude = [Math]::Abs($Seconds)
    # [int] rounds - and rounds to even - so a half-hour zone like +05:30 would
    # come out as +06:00. Truncate explicitly.
    $hours   = [Math]::Floor($magnitude / 3600)
    $minutes = [Math]::Floor(($magnitude % 3600) / 60)
    return 'UTC{0}{1:00}:{2:00}' -f $sign, $hours, $minutes
}

function ConvertFrom-OffsetString {
    # Accept +HH:MM, -HH:MM, +HH, or a bare number of hours.
    param([string]$Text)
    if ($Text.Trim() -notmatch '^([+-]?)(\d{1,2})(?::?([0-5]\d))?$') {
        Stop-WithError "cannot read time-zone offset '$Text'" 'use a form like +05:30, -08:00 or +1'
    }
    $sign    = if ($Matches[1] -eq '-') { -1 } else { 1 }
    $hours   = [int]$Matches[2]
    # An optional group that did not participate is simply absent from $Matches,
    # so test for the key rather than indexing it under Set-StrictMode.
    $minutes = if ($Matches.ContainsKey(3)) { [int]$Matches[3] } else { 0 }
    if ($hours -gt 14) { Stop-WithError "time-zone offset '$Text' is out of range" }
    return $sign * ($hours * 3600 + $minutes * 60)
}

# ---------------------------------------------------------------- list and exit
if ($List) {
    $teensy = @(Get-TeensyPort)
    if ($teensy.Count -gt 0) {
        Write-Host 'Teensy ports:'
        $teensy | ForEach-Object { Write-Host "  $($_.Device)  ($($_.Description))" }
    } else {
        Write-Host 'no Teensy ports found'
    }
    $every = @(Get-AnyPort)
    if ($every.Count -gt 0) {
        Write-Host 'all serial ports:'
        $every | ForEach-Object { Write-Host "  $($_.Device)" }
    }
    exit 0
}

if ($Utc -and $Offset) {
    Stop-WithError '-Utc and -Offset contradict each other' 'pass one or the other'
}

# ------------------------------------------------------------------ pick a port
if ($Port) {
    $device = $Port
} else {
    $teensy = @(Get-TeensyPort)
    if ($teensy.Count -eq 0) {
        $every = @(Get-AnyPort)
        if ($every.Count -eq 0) {
            Stop-WithError 'no Teensy found' 'no serial ports at all - check the USB cable and that the radio is on'
        }
        $names = ($every | ForEach-Object { $_.Device }) -join ', '
        Stop-WithError 'no Teensy found' "ports seen, none of them a Teensy: $names`n       pass one explicitly with -Port if you know which it is"
    }
    if ($teensy.Count -gt 1) {
        Write-Host "found $($teensy.Count) Teensy ports, using the first:"
        $teensy | ForEach-Object { Write-Host "  $($_.Device)  ($($_.Description))" }
    }
    $device = $teensy[0].Device
}

# ----------------------------------------------------------------- open and set
$serial = New-Object System.IO.Ports.SerialPort $device, 115200, 'None', 8, 'One'
$serial.ReadTimeout  = 300
$serial.WriteTimeout = 1000

try {
    $serial.Open()
} catch {
    Stop-WithError "cannot open ${device}: $($_.Exception.Message)" `
        ("if the radio is on the serial+midi+audio USB type, WSJT-X or rigctld may be holding`n" +
         '       the only port - close it and try again')
}

try {
    Start-Sleep -Milliseconds 300
    $serial.DiscardInBuffer()          # drop any boot chatter

    # Transmit on a second boundary so the radio's seconds line up with the PC's
    # rather than landing mid-tick.
    $epoch  = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 1
    $sendAt = [DateTimeOffset]::FromUnixTimeSeconds($epoch)

    if ($Utc) {
        $offsetSeconds = 0
    } elseif ($Offset) {
        $offsetSeconds = ConvertFrom-OffsetString $Offset
    } else {
        $offsetSeconds = [int][TimeZoneInfo]::Local.GetUtcOffset($sendAt.UtcDateTime).TotalSeconds
    }

    $stamp = $epoch + $offsetSeconds
    if ("$stamp".Length -ne $PacketDigits) {
        Stop-WithError "timestamp $stamp is not $PacketDigits digits" `
            "the firmware accepts exactly 10 - check the PC's clock"
    }

    while ([DateTimeOffset]::UtcNow.ToUnixTimeSeconds() -lt $epoch) {
        Start-Sleep -Milliseconds 1
    }
    $serial.Write("T$stamp`n")

    $shown = [DateTimeOffset]::FromUnixTimeSeconds($stamp).UtcDateTime
    $zone  = Format-Offset $offsetSeconds
    Write-Host "sent T$stamp on $device"
    Write-Host ("radio should now display {0:yyyy-MM-dd HH:mm:ss}  ($zone)" -f $shown)

    # Read whatever comes back for about a second.
    $reply    = ''
    $deadline = (Get-Date).AddSeconds(1)
    while ((Get-Date) -lt $deadline) {
        if ($serial.BytesToRead -gt 0) {
            $reply   += $serial.ReadExisting()
            $deadline = (Get-Date).AddMilliseconds(250)
        } else {
            Start-Sleep -Milliseconds 20
        }
    }

    if ($reply -match 'Time set') {
        Write-Host "radio confirmed: $($reply.Trim())"
    } elseif ($reply.Trim()) {
        Write-Host "radio said: $($reply.Trim())"
    } else {
        Write-Host ("no confirmation - expected on the serial+midi+audio USB type, where the`n" +
                    'confirmation is suppressed to keep it out of the CAT stream. Check the display.')
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
