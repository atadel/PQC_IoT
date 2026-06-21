from datetime import datetime

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

import argparse
from pathlib import Path

###### argumenty programu ######
parser = argparse.ArgumentParser(
    description="Calculate peak energies from oscilloscope CSV file."
)

parser.add_argument(
    "csv_file",
    help="Path to oscilloscope CSV file"
)

parser.add_argument(
    "-r", "--resistance",
    type=float,
    default=None,
    help="Load/sense resistance in ohms. If omitted, results are J/ohm."
)

parser.add_argument(
    "-s", "--supply-voltage",
    type=float,
    default=None,
    help="Supply voltage in volts. If provided together with resistance, calculates device energy from shunt voltage."
)

parser.add_argument(
    "--threshold-offset",
    type=float,
    default=0.015,
    help="Threshold offset used after the first peak is detected [V]."
)
parser.add_argument(
    "--min-peak-duration",
    type=float,
    default=0.00,
    help="Minimum peak duration in seconds."
)

parser.add_argument(
    "--merge-gap",
    type=int,
    default=5,
    help="Merge peaks separated by less than this number of samples."
)

parser.add_argument(
    "--expand-samples",
    type=int,
    default=2,
    help="Number of samples added before and after each detected peak."
)

parser.add_argument(
    "--min-peak-voltage",
    type=float,
    default=0.085,
    help="Minimum raw peak voltage required to accept a detected peak [V]."
)

parser.add_argument(
    "--output-dir",
    default="results",
    help="Folder where output CSV files will be saved."
)

args = parser.parse_args()

file_path = Path(args.csv_file)
input_name = file_path.stem

if not file_path.exists():
    raise FileNotFoundError(f"File not found: {file_path}")

##### plik csv #####
with open(file_path, "r") as f:
    lines = f.read().splitlines() #X,CH1,Start,Increment

meta = lines[1].split(",") #Sequence,Volt,-5.88E-02,1.00E-04
start_time = float(meta[2])
dt = float(meta[3])

# Read data
df = pd.read_csv(
    file_path,
    skiprows=2, #0,1 
    header=None,
    usecols=[0, 1], #tyle jest kolumn 
    names=["seq", "voltage"] #pozycja, value
)

df["time"] = start_time + df["seq"] * dt #czas = start + pozycja * krok czasowy

#t i v to teraz numpy arrays
t = df["time"].to_numpy() #timestampy pomiarów
v = df["voltage"].to_numpy() #wartości pomiarów


##### baseline #####

baseline = np.median(v[v < 0.07])
threshold = baseline + args.threshold_offset

print(f"baseline = {baseline:.3f} V")
print(f"threshold = {threshold:.3f} V")
print(f"min peak voltage = {args.min_peak_voltage:.3f} V")

##### detekcja pików #####

mask = v > threshold

raw_runs = []
start = None

for i, is_peak in enumerate(mask):
    if is_peak and start is None:
        start = i

    if (not is_peak or i == len(mask) - 1) and start is not None:
        end = i - 1 if not is_peak else i
        raw_runs.append((start, end))
        start = None

##### łączenie pików rozdzielonych krótkimi przerwami #####

runs = []

for a, b in raw_runs:
    if runs and a - runs[-1][1] <= args.merge_gap:
        runs[-1] = (runs[-1][0], b)
    else:
        runs.append((a, b))


##### odrzucanie zbyt krótkich pików #####

runs = [
    (a, b)
    for a, b in runs
    if (t[b] - t[a]) >= args.min_peak_duration
    and v[a:b + 1].max() >= args.min_peak_voltage
]


##### rozszerzenie pików o kilka próbek #####

expanded_runs = []

for a, b in runs:
    a = max(0, a - args.expand_samples)
    b = min(len(v) - 1, b + args.expand_samples)
    expanded_runs.append((a, b))


##### obliczanie energii pików #####

if args.resistance is None or args.supply_voltage is None:
    raise ValueError("Provide both -r/--resistance and -s/--supply-voltage.")

R = args.resistance
V_supply = args.supply_voltage

results = []

for peak_number, (a, b) in enumerate(expanded_runs, start=1):
    tt = t[a:b + 1]
    vv_raw = v[a:b + 1]

    duration = tt[-1] - tt[0]

    peak_voltage_raw = vv_raw.max()
    peak_time = tt[np.argmax(vv_raw)]

    # napięcie na shuncie ponad baseline [V]
    vv_net = vv_raw - baseline

    # moc ponad baseline [W]
    # P(t) = V_supply * (V_shunt(t) - baseline) / R
    power_net = (V_supply / R) * vv_net

    # energia piku [J]
    total_peaks_energy = np.trapezoid(power_net, tt)

    results.append({
        "peak": peak_number,
        "start_s": tt[0],
        "end_s": tt[-1],
        "peak_time_s": peak_time,
        "duration_s": duration,
        "peak_voltage_V": peak_voltage_raw,
        "baseline_V": baseline,
        "threshold_V": threshold,
        "total_peaks_energy_J": total_peaks_energy,
        "total_peaks_energy_mJ": total_peaks_energy * 1000,
    })


##### tabela wynikowa #####

results_df = pd.DataFrame(results)

if len(results_df) == 0:
    raise ValueError("No peaks detected. Try lowering threshold or min_peak_duration.")

first_peak_energy = results_df.loc[0, "total_peaks_energy_J"]

results_df["total_peaks_minus_first_J"] = (
    results_df["total_peaks_energy_J"] - first_peak_energy
)

results_df["total_peaks_minus_first_mJ"] = (
    results_df["total_peaks_minus_first_J"] * 1000
)

print(results_df)

timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
output_dir = Path(args.output_dir)
output_dir.mkdir(parents=True, exist_ok=True)

output_csv = output_dir / f"results_{input_name}.csv"
results_df.to_csv(output_csv, index=False)

print(f"Saved results to: {output_csv}")



# Plot
plt.figure(figsize=(11, 5))
plt.plot(t, v)
plt.axhline(baseline, linestyle="--", label=f"baseline = {baseline:.3f} V")
plt.axhline(threshold, linestyle=":", label=f"threshold = {threshold:.3f} V")

for _, row in results_df.iterrows():
    plt.axvspan(row["start_s"], row["end_s"], alpha=0.15)
    plt.scatter(row["peak_time_s"], row["peak_voltage_V"], s=18, zorder=3)
    plt.text(
        row["peak_time_s"],
        row["peak_voltage_V"] + 0.005,
        str(int(row["peak"])),
        ha="center"
    )

plt.xlabel("Time [s]")
plt.ylabel("Voltage [V]")
plt.grid(True)
plt.legend()
plt.tight_layout()
plt.savefig(output_dir / f"peaks_{input_name}.pdf", format="pdf", dpi=150)
plt.show()