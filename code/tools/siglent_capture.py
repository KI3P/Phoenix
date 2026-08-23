#!/usr/bin/env python3
"""
Capture trace data from SIGLENT oscilloscope using pyvisa.
Retrieves waveform data from channels 1 and 2 without modifying settings.
"""

import pyvisa
import numpy as np
import sys

SCOPE_IP = "192.168.86.101"

# Global scope connection
scope = None

def connect_scope():
    """Connect to the oscilloscope."""
    global scope
    resource_str = f'TCPIP0::{SCOPE_IP}::INSTR'
    rm = pyvisa.ResourceManager()
    scope = rm.open_resource(resource_str)
    scope.timeout = 20000
    scope.chunk_size = 1024 * 1024
    scope.write("CHDR OFF")  # Turn off headers for clean numeric responses
    return scope

def close_scope():
    """Close the oscilloscope connection."""
    global scope
    if scope:
        scope.close()
        scope = None

def get_channel_data(channel):
    """
    Retrieve waveform data from specified channel.
    Returns tuple of (time_array, voltage_array) or (None, None) on error.
    """
    ch = f"C{channel}"

    try:
        # Get vertical scale and offset (CHDR OFF gives clean numeric values)
        vdiv = float(scope.query(f"{ch}:VDIV?"))
        voffset = float(scope.query(f"{ch}:OFFSET?"))

        # Get time base
        tdiv = float(scope.query("TDIV?"))

        # Request waveform data
        scope.write(f"{ch}:WF? DAT2")
        raw_data = scope.read_raw()

        # Strip the header (Standard for Siglent binary blocks)
        # The header format is #N followed by N digits of length, then data
        header_start = raw_data.find(b'#')
        if header_start == -1:
            print(f"Could not find waveform header in response")
            return None, None

        # The byte after '#' tells you how many digits are in the length field
        length_digits = int(raw_data[header_start+1:header_start+2])
        data_start = header_start + 2 + length_digits

        # Convert binary bytes to integers (signed 8-bit)
        samples = np.frombuffer(raw_data[data_start:-1], dtype=np.int8)

        # Convert to Voltage using the manual's formula
        # Voltage = code * (vdiv / 25) - voffset
        voltages = samples * (vdiv / 25) - voffset

        # Create time array
        num_points = len(voltages)
        total_time = 14 * tdiv  # 14 divisions on SIGLENT scopes
        times = np.linspace(-total_time/2, total_time/2, num_points)

        return times, voltages

    except Exception as e:
        print(f"Failed to get data for channel {channel}: {e}")
        return None, None

def main():
    try:
        # Connect to scope
        connect_scope()

        # Identify the scope
        idn = scope.query("*IDN?")
        print(f"Connected to: {idn}")
        print()

        # Get channel 1 data
        print("Retrieving Channel 1 data...")
        time1, volt1 = get_channel_data(1)

        # Get channel 2 data
        print("Retrieving Channel 2 data...")
        time2, volt2 = get_channel_data(2)

        # Print summary
        print()
        print("=" * 50)

        if time1 is not None and volt1 is not None:
            print(f"Channel 1: {len(volt1)} points")
            print(f"  Time range: {time1[0]*1e6:.2f} µs to {time1[-1]*1e6:.2f} µs")
            print(f"  Voltage range: {volt1.min():.3f} V to {volt1.max():.3f} V")
            print(f"  Vpp: {volt1.max() - volt1.min():.3f} V")
        else:
            print("Channel 1: No data retrieved")

        print()

        if time2 is not None and volt2 is not None:
            print(f"Channel 2: {len(volt2)} points")
            print(f"  Time range: {time2[0]*1e6:.2f} µs to {time2[-1]*1e6:.2f} µs")
            print(f"  Voltage range: {volt2.min():.3f} V to {volt2.max():.3f} V")
            print(f"  Vpp: {volt2.max() - volt2.min():.3f} V")
        else:
            print("Channel 2: No data retrieved")

        # Save data to CSV files
        if time1 is not None and volt1 is not None:
            filename = "channel1_data.csv"
            np.savetxt(filename, np.column_stack((time1, volt1)),
                       delimiter=',', header='time_s,voltage_v', comments='')
            print(f"\nChannel 1 data saved to {filename}")

        if time2 is not None and volt2 is not None:
            filename = "channel2_data.csv"
            np.savetxt(filename, np.column_stack((time2, volt2)),
                       delimiter=',', header='time_s,voltage_v', comments='')
            print(f"Channel 2 data saved to {filename}")

        return time1, volt1, time2, volt2

    except Exception as e:
        print(f"Failed to connect to oscilloscope: {e}")
        sys.exit(1)

    finally:
        close_scope()

if __name__ == "__main__":
    main()
