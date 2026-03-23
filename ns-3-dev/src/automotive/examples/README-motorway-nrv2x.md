# NR V2X Mode 2 SB-SPS Motorway Experiment

Motorway scenario with ~100 vehicles transmitting periodic CAMs over
5G NR Sidelink (Mode 2) with Sensing-Based Semi-Persistent Scheduling.
Instrumented to export per-vehicle CSV logs of the full SB-SPS resource
selection pipeline.

## Prerequisites

- ns-3 built with the NR and automotive modules
- SUMO (Simulation of Urban Mobility) installed and on `$PATH`
- The `data-extraction` branch checked out

## Directory Structure

```
sumo_files_motorway/
  motorway.net.xml        # 3-lane bidirectional motorway, ~2 km
  vehicles.rou.xml        # 100 vehicles (50 east + 50 west), 130 km/h
  motorway.sumo.cfg       # SUMO config (80 s, step 0.1 s)

v2v-motorway-nrv2x.cc    # Simulation script
```

## Building

From the ns-3 root directory (`ns-3-dev/`):

```bash
./ns3 configure --enable-examples
./ns3 build v2v-motorway-nrv2x
```

## Running

### Step 1 — Verify the scenario works (logging OFF)

```bash
./ns3 run v2v-motorway-nrv2x -- --sumo-gui=false --simTime=20
```

This runs a quick 20-second simulation with no SPS logging overhead.
You should see CAM exchange statistics and PRR/latency output at the end.

Useful flags for the first run:

| Flag | Default | Description |
|------|---------|-------------|
| `--sumo-gui` | `true` | Set `false` for headless runs |
| `--simTime` | `60` | Simulation duration in seconds |
| `--enableSensing` | `true` | SB-SPS sensing (keep `true`) |
| `--penetrationRate` | `1.0` | Fraction of equipped vehicles |
| `--met-sup` | `true` | Enable PRR/latency metrics |

### Step 2 — Run with full SPS logging

```bash
./ns3 run v2v-motorway-nrv2x -- \
  --sumo-gui=false \
  --simTime=60 \
  --enableSpsLog=true \
  --spsLogDir=sps_logs/
```

This creates the `sps_logs/` directory and populates it with per-vehicle CSVs.

## Output CSV Files

All written to the directory specified by `--spsLogDir` (default: `sps_logs/`).

### `sps_selection_imsi_<IMSI>.csv`

One row per resource selection or keep decision.

| Column | Type | Description |
|--------|------|-------------|
| timestamp_ms | int64 | Simulator time (ms) |
| sfn_normalized | uint64 | Absolute slot number |
| resource_reused | 0/1 | 1 = kept via probResourceKeep |
| csrA_total | uint16 | Total candidate slots (mTotal) |
| csrA_after_exclusion | uint16 | Remaining after RSRP exclusion (L_a) |
| threshold_iterations | uint8 | Number of 3 dB relaxation steps |
| final_threshold_dBm | int | Last RSRP threshold applied |
| sensing_window_size | uint16 | Entries in sensing buffer |
| selected_slot_norm | uint64 | Normalized SfnSf of selected slot |
| filtered_slot_indexes | string | Remaining L_a slots (semicolon-delimited) |

### `sps_sensing_imsi_<IMSI>.csv`

Sensing buffer dump before each new selection.

| Column | Type | Description |
|--------|------|-------------|
| dump_timestamp_ms | int64 | When dump was taken |
| dump_sfn_norm | uint64 | Slot at dump time |
| sensed_sfn_norm | uint64 | Sensed transmission slot |
| sensed_sbch_start | uint16 | Subchannel start index |
| sensed_sbch_length | uint16 | Subchannel length |
| sensed_rsrp | double | SL-RSRP (dBm) |
| sensed_prio | uint8 | Priority |
| sensed_rsvp_ms | uint16 | Reservation period (ms) |

### `sps_prng_imsi_<IMSI>.csv`

MRG32k3a PRNG 6-element state at MAC-level decision points.

| Column | Type | Description |
|--------|------|-------------|
| timestamp_ms | int64 | Simulator time (ms) |
| sfn_normalized | uint64 | Absolute slot number |
| event | string | Decision point label |
| state_0..state_5 | double | MRG32k3a internal state vector |

Events: `before_selection`, `after_sensing_filter`, `before_scheduler`, `after_scheduler`

### `sched_prng_imsi_<IMSI>.csv`

PRNG state at scheduler-level random draws (slot pick, subchannel pick).

Same schema as `sps_prng_imsi_*.csv` but with events:
`before_slot_pick`, `after_slot_pick`, `before_sbch_pick`, `after_sbch_pick`

### `sps_distances_imsi_<IMSI>.csv`

Inter-vehicle distances at each L_a selection event.

| Column | Type | Description |
|--------|------|-------------|
| timestamp_ms | int64 | Simulation time of L_a selection |
| sfn_normalized | uint64 | Absolute slot number |
| source_imsi | uint64 | IMSI of the selecting UE |
| target_node_id | uint32 | ns-3 node ID of the other vehicle |
| target_imsi | uint64 | IMSI of the other vehicle |
| distance_m | double | Euclidean distance (meters) |

Join with `sps_selection_*.csv` on `(timestamp_ms, sfn_normalized)` to
correlate selection decisions with spatial context.

## Key NR Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Frequency | 5.89 GHz | Band n47 |
| Bandwidth | 400 MHz | |
| Numerology | 2 | 60 kHz SCS |
| MCS | 14 | Fixed |
| Sensing | Enabled | SB-SPS Mode 2 |
| slProbResourceKeep | 0.4 | Keep probability |
| Reservation period | 20 ms | |
| T1 | 2 slots | Selection window start |
| T2 | 81 slots | Selection window end |
| Sensing window | 100 ms | T0 |
| Max reservations/SCI | 3 | |
| Max PSSCH Tx | 5 | Including retransmissions |
| RSRP threshold | -128 dBm | Initial; relaxed by +3 dB per iteration |

## All Command-Line Flags

```
./ns3 run v2v-motorway-nrv2x -- --PrintHelp
```

Key flags beyond the NR parameters:

```
--enableSpsLog=true          Enable SB-SPS CSV logging (default: false)
--spsLogDir=sps_logs/        Output directory for CSV files
--sumo-gui=false             Headless SUMO
--simTime=60                 Simulation duration (seconds)
--penetrationRate=1.0        Fraction of V2X-equipped vehicles
--enableSensing=true         Enable sensing-based selection
--slProbResourceKeep=0.4     Resource keep probability
--csv-log=results            Per-packet CSV log prefix
--csv-log-cumulative=cum     Cumulative PRR/latency CSV
```

## Troubleshooting

- **SUMO not found**: Ensure `sumo` is on your `$PATH` or set
  `SUMO_HOME` and use `--sumo-folder` to point to the SUMO files.
- **Build fails**: Make sure you configured with `--enable-examples`
  and all NR/automotive dependencies are present.
- **No output CSVs**: Check that `--enableSpsLog=true` was passed.
  The logs directory is created automatically.
- **Port conflict (3400)**: SUMO TraCI uses port 3400. If another
  instance is running, kill it first or change the port in the script.
