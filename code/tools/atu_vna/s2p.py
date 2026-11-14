"""Load the saav2 MCP server's .s2p files (S11, S21 only) and basic network maths."""
import numpy as np

Z0 = 50.0
DATA_DIR = "/home/oliver/Sync/Ham/023_SAAV2-VNA_MCP/data"


def load(name, data_dir=DATA_DIR):
    path = name if name.endswith(".s2p") else f"{data_dir}/{name}.s2p"
    d = np.loadtxt(path, comments=("!", "#"))
    f = d[:, 0]
    s11 = d[:, 1] + 1j * d[:, 2]
    s21 = d[:, 3] + 1j * d[:, 4]
    return f, s11, s21


def zin(s11):
    return Z0 * (1 + s11) / (1 - s11)


def series_z(s21):
    """Impedance of a pure series element between two Z0 ports, from S21."""
    return 2 * Z0 * (1 - s21) / s21


def db(x):
    return 20 * np.log10(np.abs(x))
